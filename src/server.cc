#include "src/server.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

extern "C" {
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
}

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/language/wstring.h"
#include "src/lazy_string.h"
#include "src/vm/public/vm.h"

namespace afc {
namespace editor {

using infrastructure::FileDescriptor;
using infrastructure::FileSystemDriver;
using infrastructure::Path;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::NonNull;
using language::PossibleError;
using language::Success;
using language::ToByteString;
using language::ValueOrError;

using namespace afc::vm;

using std::cerr;
using std::pair;
using std::shared_ptr;
using std::string;

struct Environment;

namespace {
ValueOrError<Path> CreateFifo(std::optional<Path> input_path) {
  while (true) {
    // Using mktemp here is secure: if the attacker creates the file, mkfifo
    // will fail.
    Path output =
        input_path.has_value()
            ? input_path.value()
            : Path::FromString(
                  FromByteString(mktemp(strdup("/tmp/edge-server-XXXXXX"))))
                  .value();

    char* path_str = strdup(ToByteString(output.read()).c_str());
    int mkfifo_result = mkfifo(path_str, 0600);
    free(path_str);
    if (mkfifo_result != -1) {
      return output;
    }

    if (input_path.has_value()) {
      // No point retrying.
      return Error(output.read() + L": " + FromByteString(strerror(errno)));
    }
  }
}
}  // namespace

ValueOrError<int> MaybeConnectToParentServer() {
  const char* variable = "EDGE_PARENT_ADDRESS";
  if (char* server_address = getenv(variable); server_address != nullptr) {
    auto path = Path::FromString(FromByteString(server_address));
    if (path.IsError()) {
      return Error::Augment(
          L"Value from environment variable EDGE_PARENT_ADDRESS", path.error());
    }
    return MaybeConnectToServer(path.value());
  }
  return Error(
      L"Unable to find remote address (through environment variable "
      L"EDGE_PARENT_ADDRESS).");
}

ValueOrError<int> MaybeConnectToServer(const Path& path) {
  LOG(INFO) << "Connecting to server: " << path.read();
  int fd = open(ToByteString(path.read()).c_str(), O_WRONLY);
  if (fd == -1) {
    return Error(path.read() + L": Connecting to server: open failed: " +
                 FromByteString(strerror(errno)));
  }
  ValueOrError<Path> private_fifo = CreateFifo({});
  if (private_fifo.IsError()) {
    return Error::Augment(
        L"Unable to create fifo for communication with server",
        private_fifo.error());
  }
  LOG(INFO) << "Fifo created: " << private_fifo.value().read();
  string command = "editor.ConnectTo(\"" +
                   ToByteString(CppEscapeString(private_fifo.value().read())) +
                   "\");\n";
  LOG(INFO) << "Sending connection command: " << command;
  if (write(fd, command.c_str(), command.size()) == -1) {
    return Error(path.read() + L": write failed: " +
                 FromByteString(strerror(errno)));
  }
  close(fd);

  LOG(INFO) << "Opening private fifo: " << private_fifo.value().read();
  int private_fd =
      open(ToByteString(private_fifo.value().read()).c_str(), O_RDWR);
  LOG(INFO) << "Connection fd: " << private_fd;
  if (private_fd == -1) {
    return Error(private_fifo.value().read() + L": open failed: " +
                 FromByteString(strerror(errno)));
  }
  CHECK_GT(private_fd, -1);
  return private_fd;
}

void Daemonize(const std::unordered_set<FileDescriptor>& surviving_fds) {
  pid_t pid;

  pid = fork();
  CHECK_GE(pid, 0) << "fork failed: " << strerror(errno);
  if (pid > 0) {
    LOG(INFO) << "Parent exits.";
    exit(0);
  }

  CHECK_GT(setsid(), 0);
  signal(SIGHUP, SIG_IGN);

  pid = fork();
  CHECK_GE(pid, 0) << "fork failed: " << strerror(errno);
  if (pid > 0) {
    LOG(INFO) << "Parent exits.";
    exit(0);
  }

  for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
    if (surviving_fds.find(FileDescriptor(fd)) == surviving_fds.end()) {
      close(fd);
    }
  }
}

futures::Value<PossibleError> GenerateContents(OpenBuffer& target) {
  wstring address_str = target.Read(buffer_variables::path);
  ValueOrError<Path> path = Path::FromString(address_str);
  if (path.IsError()) {
    return futures::Past(PossibleError(path.error()));
  }

  LOG(INFO) << L"Server starts: " << path.value();
  return OnError(target.file_system_driver().Open(path.value(),
                                                  O_RDONLY | O_NDELAY, 0),
                 [path = path.value()](Error error) {
                   LOG(ERROR)
                       << path << ": Server: GenerateContents: Open failed: "
                       << error.description;
                   return error;
                 })
      .Transform([target = target.shared_from_this()](FileDescriptor fd) {
        LOG(INFO) << "Server received connection: " << fd;
        target->SetInputFiles(fd, FileDescriptor(-1), false, -1);
        return Success();
      });
}

ValueOrError<Path> StartServer(EditorState& editor_state,
                               std::optional<Path> address) {
  auto output = CreateFifo(address);
  if (output.IsError()) {
    return Error(L"Error creating fifo: " + output.error().description);
  }

  LOG(INFO) << "Starting server: " << output.value().read();
  setenv("EDGE_PARENT_ADDRESS", ToByteString(output.value().read()).c_str(), 1);
  auto buffer = OpenServerBuffer(editor_state, output.value());
  return output;
}

NonNull<std::shared_ptr<OpenBuffer>> OpenServerBuffer(EditorState& editor_state,
                                                      const Path& address) {
  NonNull<std::shared_ptr<OpenBuffer>> buffer = OpenBuffer::New(
      OpenBuffer::Options{.editor = editor_state,
                          .name = editor_state.GetUnusedBufferName(L"- server"),
                          .path = address,
                          .generate_contents = GenerateContents});

  buffer->NewCloseFuture().Transform([buffer, address](EmptyValue) {
    return buffer->file_system_driver().Unlink(address);
  });

  // We need to trigger the call to `handle_save` in order to unlink the file
  // in `address`.
  buffer->SetDiskState(OpenBuffer::DiskState::kStale);

  buffer->Set(buffer_variables::allow_dirty_delete, true);
  buffer->Set(buffer_variables::clear_on_reload, false);
  buffer->Set(buffer_variables::default_reload_after_exit, true);
  buffer->Set(buffer_variables::display_progress, false);
  buffer->Set(buffer_variables::persist_state, false);
  buffer->Set(buffer_variables::reload_after_exit, true);
  buffer->Set(buffer_variables::save_on_close, true);
  buffer->Set(buffer_variables::show_in_buffers_list, false);
  buffer->Set(buffer_variables::vm_exec, true);

  editor_state.buffers()->insert({buffer->name(), buffer.get_shared()});
  buffer->Reload();
  return buffer;
}

}  // namespace editor
}  // namespace afc

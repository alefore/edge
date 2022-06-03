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
#include "src/vm/public/escape.h"
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

namespace gc = language::gc;

using namespace afc::vm;

struct Environment;

namespace {
ValueOrError<Path> CreateFifo(std::optional<Path> input_path) {
  while (true) {
    // Using mktemp here is secure: if the attacker creates the file, mkfifo
    // will fail.
    Path output = input_path.has_value()
                      ? input_path.value()
                      : ValueOrDie(Path::FromString(FromByteString(
                            mktemp(strdup("/tmp/edge-server-XXXXXX")))));

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

ValueOrError<FileDescriptor> SyncConnectToParentServer() {
  static const std::string variable = "EDGE_PARENT_ADDRESS";
  if (char* server_address = getenv(variable.c_str());
      server_address != nullptr) {
    ASSIGN_OR_RETURN(
        Path path,
        AugmentErrors(
            L"Value from environment variable " + FromByteString(variable),
            Path::FromString(FromByteString(server_address))));
    return SyncConnectToServer(path);
  }
  return Error(
      L"Unable to find remote address (through environment variable "
      L"EDGE_PARENT_ADDRESS).");
}

ValueOrError<FileDescriptor> SyncConnectToServer(const Path& path) {
  LOG(INFO) << "Connecting to server: " << path.read();
  int fd = open(ToByteString(path.read()).c_str(), O_WRONLY);
  if (fd == -1) {
    return Error(path.read() + L": Connecting to server: open failed: " +
                 FromByteString(strerror(errno)));
  }
  ASSIGN_OR_RETURN(
      Path private_fifo,
      AugmentErrors(L"Unable to create fifo for communication with server",
                    CreateFifo({})));
  LOG(INFO) << "Fifo created: " << private_fifo.read();
  string command =
      "editor.ConnectTo(" +
      ToByteString(
          EscapedString::FromString(private_fifo.read()).CppRepresentation()) +
      ");\n";
  LOG(INFO) << "Sending connection command: " << command;
  if (write(fd, command.c_str(), command.size()) == -1) {
    return Error(path.read() + L": write failed: " +
                 FromByteString(strerror(errno)));
  }
  close(fd);

  LOG(INFO) << "Opening private fifo: " << private_fifo.read();
  int private_fd = open(ToByteString(private_fifo.read()).c_str(), O_RDWR);
  LOG(INFO) << "Connection fd: " << private_fd;
  if (private_fd == -1) {
    return Error(private_fifo.read() + L": open failed: " +
                 FromByteString(strerror(errno)));
  }
  CHECK_GT(private_fd, -1);
  return FileDescriptor(private_fd);
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
  FUTURES_ASSIGN_OR_RETURN(Path path, Path::FromString(address_str));

  LOG(INFO) << L"Server starts: " << path;
  return OnError(target.file_system_driver().Open(path, O_RDONLY | O_NDELAY, 0),
                 [path](Error error) {
                   LOG(ERROR)
                       << path
                       << ": Server: GenerateContents: Open failed: " << error;
                   return futures::Past(error);
                 })
      .Transform([target = target.NewRoot()](FileDescriptor fd) {
        LOG(INFO) << "Server received connection: " << fd;
        target.ptr()->SetInputFiles(fd, FileDescriptor(-1), false, -1);
        return Success();
      });
}

ValueOrError<Path> StartServer(EditorState& editor_state,
                               std::optional<Path> address) {
  ASSIGN_OR_RETURN(Path output,
                   AugmentErrors(L"Creating Fifo", CreateFifo(address)));
  LOG(INFO) << "Starting server: " << output.read();
  setenv("EDGE_PARENT_ADDRESS", ToByteString(output.read()).c_str(), 1);
  OpenServerBuffer(editor_state, output);
  return output;
}

gc::Root<OpenBuffer> OpenServerBuffer(EditorState& editor_state,
                                      const Path& address) {
  gc::Root<OpenBuffer> buffer_root = OpenBuffer::New(
      OpenBuffer::Options{.editor = editor_state,
                          .name = editor_state.GetUnusedBufferName(L"- server"),
                          .path = address,
                          .generate_contents = GenerateContents});
  OpenBuffer& buffer = buffer_root.ptr().value();
  buffer.NewCloseFuture().Transform([buffer_root, address](EmptyValue) {
    return buffer_root.ptr()->file_system_driver().Unlink(address);
  });

  // We need to trigger the call to `handle_save` in order to unlink the file
  // in `address`.
  buffer.SetDiskState(OpenBuffer::DiskState::kStale);

  buffer.Set(buffer_variables::allow_dirty_delete, true);
  buffer.Set(buffer_variables::clear_on_reload, false);
  buffer.Set(buffer_variables::default_reload_after_exit, true);
  buffer.Set(buffer_variables::display_progress, false);
  buffer.Set(buffer_variables::persist_state, false);
  buffer.Set(buffer_variables::reload_after_exit, true);
  buffer.Set(buffer_variables::save_on_close, true);
  buffer.Set(buffer_variables::show_in_buffers_list, false);
  buffer.Set(buffer_variables::vm_exec, true);
  buffer.Set(buffer_variables::vm_lines_evaluation, false);

  editor_state.buffers()->insert_or_assign(buffer.name(), buffer_root);
  buffer.Reload();
  return buffer_root;
}

}  // namespace editor
}  // namespace afc

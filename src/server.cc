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
#include "src/file_system_driver.h"
#include "src/lazy_string.h"
#include "src/vm/public/vm.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

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

    char* path_str = strdup(ToByteString(output.ToString()).c_str());
    int mkfifo_result = mkfifo(path_str, 0600);
    free(path_str);
    if (mkfifo_result != -1) {
      return Success(output);
    }

    if (input_path.has_value()) {
      // No point retrying.
      return Error(output.ToString() + L": " + FromByteString(strerror(errno)));
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
  LOG(INFO) << "Connecting to server: " << path.ToString();
  int fd = open(ToByteString(path.ToString()).c_str(), O_WRONLY);
  if (fd == -1) {
    return Error(path.ToString() + L": Connecting to server: open failed: " +
                 FromByteString(strerror(errno)));
  }
  ValueOrError<Path> private_fifo = CreateFifo({});
  if (private_fifo.IsError()) {
    return Error::Augment(
        L"Unable to create fifo for communication with server",
        private_fifo.error());
  }
  LOG(INFO) << "Fifo created: " << private_fifo.value().ToString();
  string command =
      "editor.ConnectTo(\"" +
      ToByteString(CppEscapeString(private_fifo.value().ToString())) + "\");\n";
  LOG(INFO) << "Sending connection command: " << command;
  if (write(fd, command.c_str(), command.size()) == -1) {
    return Error(path.ToString() + L": write failed: " +
                 FromByteString(strerror(errno)));
  }
  close(fd);

  LOG(INFO) << "Opening private fifo: " << private_fifo.value().ToString();
  int private_fd =
      open(ToByteString(private_fifo.value().ToString()).c_str(), O_RDWR);
  LOG(INFO) << "Connection fd: " << private_fd;
  if (private_fd == -1) {
    return Error(private_fifo.value().ToString() + L": open failed: " +
                 FromByteString(strerror(errno)));
  }
  CHECK_GT(private_fd, -1);
  return Success(private_fd);
}

void Daemonize(const std::unordered_set<int>& surviving_fds) {
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
    if (surviving_fds.find(fd) == surviving_fds.end()) {
      close(fd);
    }
  }
}

futures::Value<PossibleError> GenerateContents(
    std::shared_ptr<FileSystemDriver> file_system_driver, OpenBuffer& target) {
  wstring address_str = target.Read(buffer_variables::path);
  auto path = Path::FromString(address_str);
  if (path.IsError()) {
    return futures::Past(PossibleError(path.error()));
  }

  LOG(INFO) << L"Server starts: " << path.value();
  return OnError(file_system_driver->Open(path.value(), O_RDONLY | O_NDELAY, 0),
                 [path](Error error) {
                   LOG(ERROR) << path.value()
                              << ": Server: GenerateContents: Open failed: "
                              << error.description;
                   return error;
                 })
      .Transform([&target](int fd) {
        LOG(INFO) << "Server received connection: " << fd;
        target.SetInputFiles(fd, -1, false, -1);
        return Success();
      });
}

ValueOrError<Path> StartServer(EditorState& editor_state,
                               std::optional<Path> address) {
  auto output = CreateFifo(address);
  if (output.IsError()) {
    return Error(L"Error creating fifo: " + output.error().description);
  }

  LOG(INFO) << "Starting server: " << output.value().ToString();
  setenv("EDGE_PARENT_ADDRESS", ToByteString(output.value().ToString()).c_str(),
         1);
  auto buffer = OpenServerBuffer(editor_state, output.value());
  buffer->Set(buffer_variables::reload_after_exit, true);
  buffer->Set(buffer_variables::default_reload_after_exit, true);
  return output;
}

shared_ptr<OpenBuffer> OpenServerBuffer(EditorState& editor_state,
                                        const Path& address) {
  auto buffer = OpenBuffer::New(
      {.editor = editor_state,
       .name = editor_state.GetUnusedBufferName(L"- server"),
       .path = address,
       .generate_contents =
           [file_system_driver = std::make_shared<FileSystemDriver>(
                editor_state.work_queue())](OpenBuffer& target) {
             return GenerateContents(file_system_driver, target);
           }});
  buffer->Set(buffer_variables::clear_on_reload, false);
  buffer->Set(buffer_variables::vm_exec, true);
  buffer->Set(buffer_variables::show_in_buffers_list, false);
  buffer->Set(buffer_variables::allow_dirty_delete, true);
  buffer->Set(buffer_variables::display_progress, false);

  editor_state.buffers()->insert({buffer->name(), buffer});
  buffer->Reload();
  return buffer;
}

}  // namespace editor
}  // namespace afc

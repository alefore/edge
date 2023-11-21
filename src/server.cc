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
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"
#include "src/vm/escape.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;

using afc::infrastructure::FileDescriptor;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Path;
using afc::infrastructure::ProcessId;
using afc::infrastructure::execution::ExecutionEnvironment;
using afc::infrastructure::execution::ExecutionEnvironmentOptions;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ToByteString;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::vm::EscapedString;

namespace afc::editor {
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

// Sends an `editor.ConnectTo(input_path)` command to the server in `server_fd`.
PossibleError SendPathToServer(FileDescriptor server_fd,
                               const Path& input_path) {
  LOG(INFO) << "Sending path to server: " << input_path;
  std::string command =
      "editor.ConnectTo(" +
      ToByteString(EscapedString::FromString(NewLazyString(input_path.read()))
                       .CppRepresentation()) +
      ");\n";
  LOG(INFO) << "Sending connection command: " << command;
  if (write(server_fd.read(), command.c_str(), command.size()) == -1) {
    return Error(input_path.read() + L": write failed: " +
                 FromByteString(strerror(errno)));
  }
  return Success();
}
}  // namespace

PossibleError SyncSendCommandsToServer(FileDescriptor server_fd,
                                       std::string commands_to_run) {
  // We write the command to a temporary file and then instruct the server to
  // load the file. Otherwise, if the command is too long, it may not fit in the
  // size limit that the reader uses.
  CHECK_NE(server_fd, FileDescriptor(-1));
  size_t pos = 0;
  char* path = strdup("/tmp/edge-initial-commands-XXXXXX");
  int tmp_fd = mkstemp(path);
  NonNull<std::shared_ptr<LazyString>> path_str =
      NewLazyString(FromByteString(path));
  free(path);

  commands_to_run =
      commands_to_run + "\n;Unlink(" +
      ToByteString(
          vm::EscapedString::FromString(path_str).CppRepresentation()) +
      ");\n";
  LOG(INFO) << "Sending commands to fd: " << server_fd << " through path "
            << path_str->ToString() << ": " << commands_to_run;
  while (pos < commands_to_run.size()) {
    VLOG(5) << commands_to_run.substr(pos);
    int bytes_written = write(tmp_fd, commands_to_run.c_str() + pos,
                              commands_to_run.size() - pos);
    if (bytes_written == -1)
      return Error(L"write: " + FromByteString(strerror(errno)));
    pos += bytes_written;
  }
  // TODO(trivial, P2, 2023-11-10): Check return value of `close`.
  close(tmp_fd);
  DECLARE_OR_RETURN(Path input_path, Path::FromString(path_str));
  std::string command =
      "#include \"" + ToByteString(path_str->ToString()) + "\"\n";
  if (write(server_fd.read(), command.c_str(), command.size()) !=
      static_cast<int>(command.size())) {
    std::cerr << "write: " << strerror(errno);
    exit(1);
  }
  return Success();
}

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
  int server_fd = open(ToByteString(path.read()).c_str(), O_WRONLY);
  if (server_fd == -1) {
    return Error(path.read() + L": Connecting to server: open failed: " +
                 FromByteString(strerror(errno)));
  }
  auto fd_deleter_callback = [](int* value) {
    close(*value);
    delete value;
  };
  std::unique_ptr<int, decltype(fd_deleter_callback)> fd_deleter(
      new int(server_fd), fd_deleter_callback);

  ASSIGN_OR_RETURN(
      Path private_fifo,
      AugmentErrors(L"Unable to create fifo for communication with server",
                    CreateFifo({})));
  RETURN_IF_ERROR(SendPathToServer(FileDescriptor(server_fd), private_fifo));
  fd_deleter = nullptr;

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

  for (int fd = 0; fd < sysconf(_SC_OPEN_MAX); ++fd)
    if (!surviving_fds.contains(FileDescriptor(fd))) close(fd);
}

futures::Value<PossibleError> GenerateContents(OpenBuffer& target) {
  std::wstring address_str = target.Read(buffer_variables::path);
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
        target.ptr()->SetInputFiles(fd, FileDescriptor(-1), false,
                                    std::optional<ProcessId>());
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

  editor_state.buffers()->insert_or_assign(buffer.name(),
                                           buffer_root.ptr().ToRoot());
  buffer.Reload();
  return buffer_root;
}

namespace {
bool server_tests_registration = tests::Register(
    L"Server",
    {{.name = L"StartServer", .callback = [] {
        language::NonNull<std::unique_ptr<EditorState>> editor =
            EditorForTests();
        CHECK_EQ(editor->buffers()->size(), 0ul);
        infrastructure::Path server_address =
            ValueOrDie(StartServer(editor.value(), std::nullopt));
        CHECK_EQ(editor->buffers()->size(), 1ul);
        CHECK(!editor->exit_value().has_value());
        size_t iteration = 0;
        ExecutionEnvironment(
            ExecutionEnvironmentOptions{
                .stop_check = [&] { return editor->exit_value().has_value(); },
                .get_next_alarm =
                    [&] { return editor->WorkQueueNextExecution(); },
                .on_signals = [] {},
                .on_iteration =
                    [&](afc::infrastructure::execution::IterationHandler&
                            handler) {
                      LOG(INFO) << "Iteration: " << iteration;
                      editor->ExecutionIteration(handler);
                      if (iteration == 10) {
                        FileDescriptor client_fd =
                            ValueOrDie(SyncConnectToServer(server_address));
                        CHECK(!IsError(SyncSendCommandsToServer(
                            client_fd, "editor.set_exit_value(567);")));
                      }
                      iteration++;
                    }})
            .Run();
        CHECK_GT(iteration, 10ul);
        CHECK(editor->exit_value().has_value());
        CHECK_EQ(editor->exit_value().value(), 567);
      }}});
}
}  // namespace afc::editor

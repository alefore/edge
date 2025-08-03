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
#include "src/buffer_registry.h"
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
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::vm::EscapedString;

namespace afc::editor {
namespace {
ValueOrError<Path> CreateFifo(std::optional<Path> input_path) {
  while (true) {
    // Using mktemp here is secure: if the attacker creates the file, mkfifo
    // will fail.
    Path output = input_path.has_value()
                      ? input_path.value()
                      : ValueOrDie(Path::New(LazyString{FromByteString(
                            mktemp(strdup("/tmp/edge-server-XXXXXX")))}));

    char* path_str = strdup(output.ToBytes().c_str());
    int mkfifo_result = mkfifo(path_str, 0600);
    free(path_str);
    if (mkfifo_result != -1) {
      return output;
    }

    if (input_path.has_value()) {
      // No point retrying.
      return Error{output.read() + LazyString{L": "} +
                   LazyString{FromByteString(strerror(errno))}};
    }
  }
}

// Sends an `editor.ConnectTo(input_path)` command to the server in `server_fd`.
PossibleError SendPathToServer(FileDescriptor server_fd,
                               const Path& input_path) {
  LOG(INFO) << "Sending path to server: " << input_path;
  LazyString command =
      LazyString{L"editor.ConnectTo("} +
      ToLazyString(
          EscapedString::FromString(input_path.read()).CppRepresentation()) +
      LazyString{L");\n"};
  LOG(INFO) << "Sending connection command: " << command;
  std::string command_str = command.ToBytes();
  if (write(server_fd.read(), command_str.c_str(), command_str.size()) == -1) {
    return Error{input_path.read() + LazyString{L": write failed: "} +
                 LazyString{FromByteString(strerror(errno))}};
  }
  return Success();
}
}  // namespace

PossibleError SyncSendCommandsToServer(FileDescriptor server_fd,
                                       LazyString commands_to_run) {
  // We write the command to a temporary file and then instruct the server to
  // load the file. Otherwise, if the command is too long, it may not fit in the
  // size limit that the reader uses.
  ColumnNumber pos{0};
  char* path = strdup("/tmp/edge-initial-commands-XXXXXX");
  int tmp_fd = mkstemp(path);
  LazyString path_str = LazyString{FromByteString(path)};
  free(path);

  commands_to_run +=
      LazyString{L"\n;Unlink("} +
      vm::EscapedString::FromString(path_str).CppRepresentation().read() +
      LazyString{L");\n"};
  LOG(INFO) << "Sending commands to fd: " << server_fd << " through path "
            << path_str << ": " << commands_to_run;
  const std::string commands_to_run_str = commands_to_run.ToBytes();
  while (pos.ToDelta() < ColumnNumberDelta(commands_to_run_str.size())) {
    VLOG(5) << commands_to_run_str.substr(pos.read());
    int bytes_written = write(tmp_fd, commands_to_run_str.c_str() + pos.read(),
                              commands_to_run_str.size() - pos.read());
    if (bytes_written == -1)
      return Error{LazyString{L"write: "} +
                   LazyString{FromByteString(strerror(errno))}};
    pos += ColumnNumberDelta(bytes_written);
  }
  if (close(tmp_fd) != 0) {
    std::string failure = strerror(errno);
    return Error{LazyString{L"close("} + path_str + LazyString{L"): "} +
                 LazyString{FromByteString(failure)}};
  }
  DECLARE_OR_RETURN(Path input_path, Path::New(path_str));
  std::string command = "#include \"" + path_str.ToBytes() + "\"\n";
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
        AugmentError(LazyString{L"Value from environment variable " +
                                FromByteString(variable)},
                     Path::New(LazyString{FromByteString(server_address)})));
    return SyncConnectToServer(path);
  }
  return Error{
      LazyString{L"Unable to find remote address (through environment variable "
                 L"EDGE_PARENT_ADDRESS)."}};
}

ValueOrError<FileDescriptor> SyncConnectToServer(const Path& path) {
  LOG(INFO) << "Connecting to server: " << path.read();
  ASSIGN_OR_RETURN(
      FileDescriptor server_fd,
      AugmentError(
          path.read() + LazyString{L": Connecting to server: open failed: "} +
              LazyString{FromByteString(strerror(errno))},
          FileDescriptor::New(open(path.ToBytes().c_str(), O_WRONLY))));
  auto fd_deleter_callback = [](int* value) {
    close(*value);
    delete value;
  };
  std::unique_ptr<int, decltype(fd_deleter_callback)> fd_deleter(
      new int(server_fd.read()), fd_deleter_callback);

  ASSIGN_OR_RETURN(
      Path private_fifo,
      AugmentError(
          LazyString{L"Unable to create fifo for communication with server"},
          CreateFifo({})));
  RETURN_IF_ERROR(SendPathToServer(server_fd, private_fifo));
  fd_deleter = nullptr;

  LOG(INFO) << "Opening private fifo: " << private_fifo.read();
  return AugmentError(
      private_fifo.read() + LazyString{L": open failed: "} +
          LazyString{FromByteString(strerror(errno))},
      FileDescriptor::New(open(private_fifo.ToBytes().c_str(), O_RDWR)));
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
  FUTURES_ASSIGN_OR_RETURN(Path path,
                           Path::New(target.Read(buffer_variables::path)));

  LOG(INFO) << L"Server starts: " << path;
  return target.SetInputFromPath(path);
}

ValueOrError<Path> StartServer(EditorState& editor_state,
                               std::optional<Path> address) {
  ASSIGN_OR_RETURN(Path output, AugmentError(LazyString{L"Creating Fifo"},
                                             CreateFifo(address)));
  LOG(INFO) << "Starting server: " << output.read();
  setenv("EDGE_PARENT_ADDRESS", output.ToBytes().c_str(), 1);
  OpenServerBuffer(editor_state, output);
  return output;
}

void OpenServerBuffer(EditorState& editor_state, const Path& address) {
  gc::Root<OpenBuffer> buffer_root = OpenBuffer::New(
      OpenBuffer::Options{.editor = editor_state,
                          .name = ServerBufferName{address},
                          .path = address,
                          .generate_contents = GenerateContents});
  OpenBuffer& buffer = buffer_root.ptr().value();
  buffer.NewCloseFuture().Transform(
      [file_system_driver = buffer.file_system_driver(), address](EmptyValue) {
        return file_system_driver->Unlink(address);
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

  editor_state.buffer_registry().Add(buffer.name(),
                                     buffer_root.ptr().ToWeakPtr());
  buffer.Reload();
}

namespace {
bool server_tests_registration = tests::Register(
    L"Server",
    {{.name = L"StartServer", .callback = [] {
        language::NonNull<std::unique_ptr<EditorState>> editor =
            EditorForTests(std::nullopt);
        CHECK_EQ(editor->buffer_registry().buffers().size(), 0ul);
        infrastructure::Path server_address =
            ValueOrDie(StartServer(editor.value(), std::nullopt));
        CHECK_EQ(editor->buffer_registry().buffers().size(), 1ul);
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
                            client_fd,
                            LazyString{L"editor.set_exit_value(567);"})));
                      }
                      iteration++;
                    }})
            .Run();
        CHECK_GT(iteration, 10ul);
        CHECK(editor->exit_value().has_value());
        CHECK_EQ(editor->exit_value().value(), 567);
      }}});
}  // namespace
}  // namespace afc::editor

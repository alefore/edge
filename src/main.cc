#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

extern "C" {
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
}

#include <glog/logging.h>

#include "config.h"
#include "src/args.h"
#include "src/audio.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/command_line.h"
#include "src/infrastructure/time.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/run_command_handler.h"
#include "src/screen.h"
#include "src/screen_curses.h"
#include "src/screen_vm.h"
#include "src/server.h"
#include "src/terminal.h"
#include "src/tests/benchmarks.h"
#include "src/tests/tests.h"
#include "src/vm/public/escape.h"
#include "src/vm/public/value.h"

namespace {

using namespace afc::editor;
using afc::infrastructure::FileDescriptor;
using afc::infrastructure::MillisecondsBetween;
using afc::infrastructure::Now;
using afc::infrastructure::Path;
using afc::infrastructure::Tracker;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IgnoreErrors;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ToByteString;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::NewLazyString;
namespace gc = afc::language::gc;

static const char* kEdgeParentAddress = "EDGE_PARENT_ADDRESS";
static std::unique_ptr<EditorState> global_editor_state;

EditorState& editor_state() { return *global_editor_state; }

void (*default_interrupt_handler)(int sig);
void (*default_stop_handler)(int sig);

void SignalHandler(int sig) {
  if (sig == SIGINT) {
    if (!editor_state().handling_interrupts()) {
      signal(SIGINT, default_interrupt_handler);
      raise(SIGINT);
      return;
    }

    // Normally, when the buffer consumes the signal, it'll overwrite the
    // status right away. So we just put a default message in case the signal is
    // not consumed.
    editor_state().status().SetWarningText(
        L"'*aq' to quit -- pending changes won't be saved.");
  } else if (sig == SIGTSTP) {
    if (!editor_state().handling_stop_signals()) {
      signal(SIGINT, default_stop_handler);
      raise(SIGINT);
      signal(SIGINT, &SignalHandler);
      return;
    }
  }

  editor_state().PushSignal(UnixSignal(sig));
}

static const wchar_t* kDefaultCommandsToRun =
    L"ForkCommandOptions options = ForkCommandOptions();\n"
    L"options.set_command(\"sh -l\");\n"
    L"options.set_insertion_type(\"search_or_create\");\n"
    L"options.set_name(\"💻shell\");\n"
    L"editor.ForkCommand(options);";

std::wstring CommandsToRun(CommandLineValues args) {
  using afc::vm::EscapedString;
  std::wstring commands_to_run = args.commands_to_run;
  std::vector<std::wstring> buffers_to_watch;
  for (auto& path : args.naked_arguments) {
    std::wstring full_path;
    if (!path.empty() &&
        std::wstring(L"/~").find(path[0]) != std::wstring::npos) {
      LOG(INFO) << L"Will open an absolute path: " << path;
      full_path = path;
    } else {
      LOG(INFO) << L"Will open a relative path: " << path;
      switch (args.initial_path_resolution_behavior) {
        case CommandLineValues::LocalPathResolutionBehavior::kSimple: {
          char* dir = get_current_dir_name();
          full_path = FromByteString(dir) + L"/" + path;
          free(dir);
          break;
        }
        case CommandLineValues::LocalPathResolutionBehavior::kAdvanced:
          full_path = path;
      }
    }
    commands_to_run += L"editor.OpenFile(" +
                       EscapedString::FromString(NewLazyString(full_path))
                           .CppRepresentation() +
                       L", true);\n";
    buffers_to_watch.push_back(full_path);
  }
  for (auto& command_to_fork : args.commands_to_fork) {
    commands_to_run +=
        L"ForkCommandOptions options = ForkCommandOptions();\n"
        L"options.set_command(" +
        EscapedString::FromString(NewLazyString(command_to_fork))
            .CppRepresentation() +
        L");\noptions.set_insertion_type(\"" +
        (args.background ? L"skip" : L"search_or_create") +
        L"\");\neditor.ForkCommand(options);";
  }
  switch (args.view_mode) {
    case CommandLineValues::ViewMode::kAllBuffers:
      commands_to_run += L"editor.set_multiple_buffers(true);\n";
      commands_to_run += L"editor.SetHorizontalSplitsWithAllBuffers();\n";
      break;
    case CommandLineValues::ViewMode::kDefault:
      break;
  }
  if (args.client.has_value()) {
    commands_to_run +=
        L"Screen screen = RemoteScreen(" +
        EscapedString::FromString(
            NewLazyString(FromByteString(getenv(kEdgeParentAddress))))
            .CppRepresentation() +
        L");\n";
  } else if (!buffers_to_watch.empty() &&
             args.nested_edge_behavior ==
                 CommandLineValues::NestedEdgeBehavior::kWaitForClose) {
    commands_to_run += L"SetString buffers_to_watch = SetString();\n";
    for (auto& block : buffers_to_watch) {
      commands_to_run +=
          L"buffers_to_watch.insert(" +
          EscapedString::FromString(NewLazyString(block)).CppRepresentation() +
          L");\n";
    }
    commands_to_run += L"editor.WaitForClose(buffers_to_watch);\n";
  }
  if (args.prompt_for_path) {
    commands_to_run += L"editor.PromptAndOpenFile();";
  }
  if (commands_to_run.empty()) {
    commands_to_run = kDefaultCommandsToRun;
  }
  return commands_to_run;
}

void SendCommandsToParent(FileDescriptor fd,
                          const std::string commands_to_run) {
  // We write the command to a temporary file and then instruct the server to
  // load the file. Otherwise, if the command is too long, it may not fit in the
  // size limit that the reader uses.
  CHECK_NE(fd, FileDescriptor(-1));
  using std::cerr;
  size_t pos = 0;
  char* path = strdup("/tmp/edge-initial-commands-XXXXXX");
  int tmp_fd = mkstemp(path);
  while (pos < commands_to_run.size()) {
    VLOG(5) << commands_to_run.substr(pos);
    int bytes_written = write(tmp_fd, commands_to_run.c_str() + pos,
                              commands_to_run.size() - pos);
    if (bytes_written == -1) {
      cerr << "write: " << strerror(errno);
      exit(1);
    }
    pos += bytes_written;
  }
  close(tmp_fd);
  std::string command = "#include \"" + std::string(path) + "\"\n";
  free(path);
  if (write(fd.read(), command.c_str(), command.size()) !=
      static_cast<int>(command.size())) {
    cerr << "write: " << strerror(errno);
    exit(1);
  }
}

Path StartServer(const CommandLineValues& args, bool connected_to_parent) {
  LOG(INFO) << "Starting server.";

  std::unordered_set<FileDescriptor> surviving_fds = {FileDescriptor(1),
                                                      FileDescriptor(2)};
  if (args.server && args.server_path.has_value()) {
    // We can't close stdout until we've printed the address in which the server
    // will run.
    Daemonize(surviving_fds);
  }
  Path server_address =
      ValueOrDie(StartServer(editor_state(), args.server_path),
                 args.binary_name + L"Unable to start server");
  if (args.server) {
    if (!connected_to_parent) {
      std::cout << args.binary_name
                << ": Server starting at: " << server_address << std::endl;
    }
    for (FileDescriptor fd : surviving_fds) {
      close(fd.read());
    }
  }
  return server_address;
}

std::wstring GetGreetingMessage() {
  static std::vector<std::wstring> errors({
      L"Welcome to Edge!",
      L"Edge, your favorite text editor.",
      L"It looks like you're writing a letter. Would you like help?",
      L"Edge, a text editor.",
      L"All modules are now active.",
      L"Booting up Edge. . . . . . . . . . . . . DONE",
      L"What are you up to today?",
      L"Stop trying to calm the storm. Calm yourself, the storm will pass.",
      L"Learn to be indifferent to what makes no difference.",
      L"Whatever can happen at any time can happen today.",
      L"The trouble is, you think you have time.",
      L"Happiness is here, and now.",
      L"The journey of a thousand miles begins with a single step.",
      L"Every moment is a fresh beginning.",
      L"Action is the foundational key to all success.",
  });
  return errors[rand() % errors.size()];
}

void RedrawScreens(const CommandLineValues& args,
                   std::optional<FileDescriptor> remote_server_fd,
                   std::optional<LineColumnDelta>* last_screen_size,
                   Terminal* terminal, Screen* screen_curses) {
  static Tracker tracker(L"Main::RedrawScreens");
  auto call = tracker.Call();

  // Precondition.
  CHECK(!args.client.has_value() || remote_server_fd.has_value());

  auto screen_state = editor_state().FlushScreenState();
  if (!screen_state.has_value()) return;
  if (screen_curses != nullptr) {
    if (!args.client.has_value()) {
      terminal->Display(editor_state(), *screen_curses, screen_state.value());
    } else {
      CHECK(remote_server_fd.has_value());
      screen_curses->Refresh();  // Don't want this to be buffered!
      LineColumnDelta screen_size = screen_curses->size();
      if (last_screen_size->has_value() &&
          screen_size != last_screen_size->value()) {
        LOG(INFO) << "Sending screen size update to server.";
        SendCommandsToParent(
            remote_server_fd.value(),
            "screen.set_size(" + std::to_string(screen_size.column.read()) +
                "," + std::to_string(screen_size.line.read()) + ");" +
                "editor.set_screen_needs_hard_redraw(true);\n");
        *last_screen_size = screen_size;
      }
    }
  }
  VLOG(5) << "Updating remote screens.";
  for (auto& buffer : *editor_state().buffers()) {
    static const afc::vm::Namespace kEmptyNamespace;
    std::optional<gc::Root<afc::vm::Value>> value =
        buffer.second.ptr()->environment()->Lookup(editor_state().gc_pool(),
                                                   kEmptyNamespace, L"screen",
                                                   GetScreenVmType());
    if (!value.has_value() || value.value().ptr()->type != GetScreenVmType()) {
      continue;
    }
    auto buffer_screen =
        afc::vm::VMTypeMapper<NonNull<std::shared_ptr<Screen>>>::get(
            value.value().ptr().value());
    if (&buffer_screen.value() == screen_curses) {
      continue;
    }
    LOG(INFO) << "Remote screen for buffer: " << buffer.first;
    terminal->Display(editor_state(), buffer_screen.value(),
                      screen_state.value());
  }
}
}  // namespace

int main(int argc, const char** argv) {
  using namespace afc::editor;
  using std::cerr;
  using std::unique_ptr;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  srand(time(NULL));

  std::string locale = std::setlocale(LC_ALL, "");
  LOG(INFO) << "Using locale: " << locale;

  LOG(INFO) << "Initializing command line arguments.";
  auto args = afc::command_line_arguments::Parse(afc::editor::CommandLineArgs(),
                                                 argc, argv);

  LOG(INFO) << "Setting up audio_player.";
  const NonNull<std::unique_ptr<audio::Player>> audio_player =
      getenv(kEdgeParentAddress) != nullptr || args.mute
          ? audio::NewNullPlayer()
          : audio::NewPlayer();

  LOG(INFO) << "Creating editor.";
  global_editor_state =
      std::make_unique<EditorState>(args, audio_player.value());

  switch (args.tests_behavior) {
    case CommandLineValues::TestsBehavior::kRunAndExit:
      afc::tests::Run();
      exit(0);
    case CommandLineValues::TestsBehavior::kListAndExit:
      afc::tests::List();
      exit(0);
    case CommandLineValues::TestsBehavior::kIgnore:
      break;
  }

  if (!args.benchmark.empty()) {
    afc::tests::RunBenchmark(args.benchmark);
    exit(0);
  }

  bool connected_to_parent = false;
  const std::optional<FileDescriptor> remote_server_fd =
      args.client.has_value()
          ? ValueOrDie(
                SyncConnectToServer(args.client.value()),
                args.binary_name + L": Unable to connect to remote server")
          : std::visit(
                overload{[](Error) { return std::optional<FileDescriptor>(); },
                         [&](FileDescriptor fd) {
                           args.server = true;
                           connected_to_parent = true;
                           return std::optional<FileDescriptor>(fd);
                         }},
                SyncConnectToParentServer());

  std::shared_ptr<Screen> screen_curses;
  if (!args.server) {
    LOG(INFO) << "Creating curses screen.";
    screen_curses = std::move(NewScreenCurses().get_unique());
  }
  RegisterScreenType(editor_state(),
                     editor_state().environment().ptr().value());
  VisitPointer(
      screen_curses,
      [](NonNull<std::shared_ptr<Screen>> input_screen_curses) {
        editor_state().environment().ptr()->Define(
            L"screen", afc::vm::Value::NewObject(editor_state().gc_pool(),
                                                 GetScreenVmType().object_type,
                                                 input_screen_curses));
      },
      [] {});

  LOG(INFO) << "Starting server.";
  auto server_path = StartServer(args, connected_to_parent);
  while (editor_state().WorkQueueNextExecution().has_value() &&
         editor_state().WorkQueueNextExecution().value() < Now()) {
    editor_state().ExecutePendingWork();
  }

  auto commands_to_run = CommandsToRun(args);
  if (!commands_to_run.empty()) {
    if (connected_to_parent) {
      commands_to_run +=
          L"editor.SendExitTo(" +
          afc::vm::EscapedString::FromString(NewLazyString(server_path.read()))
              .CppRepresentation() +
          L");";
    }

    LOG(INFO) << "Sending commands.";
    FileDescriptor self_fd =
        remote_server_fd.has_value()
            ? remote_server_fd.value()
            : ValueOrDie(args.server && args.server_path.has_value()
                             ? SyncConnectToServer(args.server_path.value())
                             : SyncConnectToParentServer());
    CHECK_NE(self_fd, FileDescriptor(-1));
    SendCommandsToParent(self_fd, ToByteString(commands_to_run));
  }

  LOG(INFO) << "Creating terminal.";
  std::mbstate_t mbstate = std::mbstate_t();
  Terminal terminal;
  if (!args.server) {
    default_interrupt_handler = signal(SIGINT, &SignalHandler);
    default_stop_handler = signal(SIGTSTP, &SignalHandler);
  }

  // This is only meaningful if we're running with args.client: it contains the
  // last observed size of our screen (to detect that we need to propagate
  // changes to the server).
  std::optional<LineColumnDelta> last_screen_size;

  audio::BeepFrequencies(audio_player.value(), 0.1,
                         {audio::Frequency(783.99), audio::Frequency(723.25),
                          audio::Frequency(783.99)});
  editor_state().status().SetInformationText(GetGreetingMessage());

  LOG(INFO) << "Main loop starting.";
  while (!editor_state().exit_value().has_value()) {
    // We execute pending work before updating screens, since we expect that the
    // pending work updates may have visible effects.
    VLOG(5) << "Executing pending work.";
    {
      static Tracker tracker(L"Main::ExecutePendingWork");
      auto call = tracker.Call();
      editor_state().ExecutePendingWork();
    }

    VLOG(5) << "Updating screens.";
    RedrawScreens(args, remote_server_fd, &last_screen_size, &terminal,
                  screen_curses.get());

    std::vector<std::optional<gc::Root<OpenBuffer>>> buffers;

    // The file descriptor at position i will be either fd or fd_error of
    // buffers[i]. The exception to this is fd 0 (at the end).
    struct pollfd fds[editor_state().buffers()->size() * 2 + 3];
    buffers.reserve(sizeof(fds) / sizeof(fds[0]));

    for (std::pair<BufferName, gc::Root<OpenBuffer>> entry :
         *editor_state().buffers()) {
      gc::Root<OpenBuffer>& buffer = entry.second;
      auto register_reader = [&](const FileDescriptorReader* reader) {
        auto pollfd = reader == nullptr ? std::nullopt : reader->GetPollFd();
        if (pollfd.has_value()) {
          fds[buffers.size()] = pollfd.value();
          buffers.push_back(buffer);
        }
      };
      register_reader(buffer.ptr()->fd());
      register_reader(buffer.ptr()->fd_error());
    }

    if (screen_curses != nullptr) {
      fds[buffers.size()].fd = 0;
      fds[buffers.size()].events = POLLIN | POLLPRI | POLLERR;
      buffers.push_back(std::nullopt);
    }

    fds[buffers.size()].fd =
        editor_state().fd_to_detect_internal_events().read();
    fds[buffers.size()].events = POLLIN | POLLPRI;
    buffers.push_back(std::nullopt);

    auto now = Now();
    auto next_execution = editor_state().WorkQueueNextExecution();
    int timeout_ms = next_execution.has_value()
                         ? static_cast<int>(ceil(std::min(
                               std::max(0.0, MillisecondsBetween(
                                                 now, next_execution.value())),
                               1000.0)))
                         : 1000;
    VLOG(5) << "Timeout: " << timeout_ms << " has value "
            << (next_execution.has_value() ? "yes" : "no");
    if (poll(fds, buffers.size(), timeout_ms) == -1) {
      CHECK_EQ(errno, EINTR) << "poll failed: " << strerror(errno);

      LOG(INFO) << "Received signals.";
      if (!args.client.has_value()) {
        // We schedule a redraw in case the signal was SIGWINCH (the screen
        // size has changed). Ideally we'd only do that for that signal, to
        // avoid spurious refreshes, but... who cares.
        editor_state().set_screen_needs_hard_redraw(true);

        editor_state().ProcessSignals();
      }

      continue;
    }

    for (size_t i = 0; i < buffers.size(); i++) {
      if (!(fds[i].revents & (POLLIN | POLLPRI | POLLHUP))) {
        continue;
      }
      if (fds[i].fd == 0) {
        if (fds[i].revents & POLLHUP) {
          LOG(INFO) << "POLLHUP enabled in fd 0. AttemptTermination(0).";
          editor_state().Terminate(
              EditorState::TerminationType::kIgnoringErrors, 0);
        } else {
          CHECK(screen_curses != nullptr);
          std::vector<wint_t> input;
          input.reserve(10);
          {
            wint_t c;
            while (input.size() < 1024 &&
                   (c = ReadChar(&mbstate)) != static_cast<wint_t>(-1)) {
              input.push_back(c);
            }
          }
          for (auto& c : input) {
            static Tracker tracker(L"Main::ProcessInput");
            auto call = tracker.Call();
            if (remote_server_fd.has_value()) {
              SendCommandsToParent(
                  remote_server_fd.value(),
                  "ProcessInput(" + std::to_string(c) + ");\n");
            } else {
              editor_state().ProcessInput(c);
            }
          }
        }
        continue;
      }

      if (FileDescriptor(fds[i].fd) ==
          editor_state().fd_to_detect_internal_events()) {
        editor_state().ResetInternalEventNotifications();
        continue;
      }

      CHECK_LE(i, buffers.size());
      CHECK(buffers[i]);
      OpenBuffer& buffer = buffers[i]->ptr().value();
      if (buffer.fd() != nullptr &&
          FileDescriptor(fds[i].fd) == buffer.fd()->fd()) {
        LOG(INFO) << "Reading (normal): "
                  << buffer.Read(buffer_variables::name);
        static Tracker tracker(L"Main::ReadData");
        auto call = tracker.Call();
        buffer.ReadData();
      } else if (buffer.fd_error() != nullptr &&
                 FileDescriptor(fds[i].fd) == buffer.fd_error()->fd()) {
        LOG(INFO) << "Reading (error): " << buffer.Read(buffer_variables::name);
        static Tracker tracker(L"Main::ReadErrorData");
        auto call = tracker.Call();
        buffer.ReadErrorData();
      } else {
        LOG(FATAL) << "Invalid file descriptor.";
      }
    }
  }

  return editor_state().exit_value().value();
}

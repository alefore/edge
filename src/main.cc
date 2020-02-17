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
#include "src/command_line.h"
#include "src/editor.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/lazy_string.h"
#include "src/run_command_handler.h"
#include "src/screen.h"
#include "src/screen_curses.h"
#include "src/screen_vm.h"
#include "src/server.h"
#include "src/terminal.h"
#include "src/tests/tests.h"
#include "src/time.h"
#include "src/vm/public/value.h"
#include "src/wstring.h"

namespace {

using namespace afc::editor;

static const char* kEdgeParentAddress = "EDGE_PARENT_ADDRESS";
static std::unique_ptr<EditorState> global_editor_state;

EditorState* editor_state() { return global_editor_state.get(); }

void (*default_interrupt_handler)(int sig);
void (*default_stop_handler)(int sig);

void SignalHandler(int sig) {
  if (sig == SIGINT) {
    if (!editor_state()->handling_interrupts()) {
      signal(SIGINT, default_interrupt_handler);
      raise(SIGINT);
      return;
    }

    // Normally, when the buffer consumes the signal, it'll overwrite the
    // status right away. So we just put a default message in case the signal is
    // not consumed.
    editor_state()->status()->SetWarningText(
        L"'*aq' to quit -- pending changes won't be saved.");
  } else if (sig == SIGTSTP) {
    if (!editor_state()->handling_stop_signals()) {
      signal(SIGINT, default_stop_handler);
      raise(SIGINT);
      signal(SIGINT, &SignalHandler);
      return;
    }
  }

  editor_state()->PushSignal(sig);
}

static const wchar_t* kDefaultCommandsToRun =
    L"ForkCommandOptions options = ForkCommandOptions();\n"
    L"options.set_command(\"sh -l\");\n"
    L"options.set_insertion_type(\"search_or_create\");\n"
    L"options.set_name(\"💻shell\");\n"
    L"ForkCommand(options);";

wstring CommandsToRun(CommandLineValues args) {
  wstring commands_to_run = args.commands_to_run;
  std::vector<wstring> buffers_to_watch;
  for (auto& path : args.naked_arguments) {
    wstring full_path;
    if (!path.empty() && wstring(L"/~").find(path[0]) != wstring::npos) {
      LOG(INFO) << L"Will open an absolute path: " << path;
      full_path = path;
    } else {
      LOG(INFO) << L"Will open a relative path: " << path;
      char* dir = get_current_dir_name();
      full_path = FromByteString(dir) + L"/" + path;
      free(dir);
    }
    commands_to_run +=
        L"OpenFile(\"" + CppEscapeString(full_path) + L"\", true);\n";
    buffers_to_watch.push_back(full_path);
  }
  for (auto& command_to_fork : args.commands_to_fork) {
    commands_to_run +=
        L"ForkCommandOptions options = ForkCommandOptions();\n"
        L"options.set_command(\"" +
        CppEscapeString(command_to_fork) +
        L"\");\noptions.set_insertion_type(\"" +
        (args.background ? L"skip" : L"search_or_create") +
        L"\");\nForkCommand(options);";
  }
  if (!args.client.empty()) {
    commands_to_run +=
        L"Screen screen = RemoteScreen(\"" +
        CppEscapeString(FromByteString(getenv(kEdgeParentAddress))) + L"\");\n";
  } else if (!buffers_to_watch.empty() &&
             args.nested_edge_behavior ==
                 CommandLineValues::NestedEdgeBehavior::kWaitForClose) {
    commands_to_run += L"SetString buffers_to_watch = SetString();\n";
    for (auto& block : buffers_to_watch) {
      commands_to_run +=
          L"buffers_to_watch.insert(\"" + CppEscapeString(block) + L"\");\n";
    }
    commands_to_run += L"WaitForClose(buffers_to_watch);\n";
  }
  if (args.prompt_for_path) {
    commands_to_run += L"PromptAndOpenFile();";
  }
  if (commands_to_run.empty()) {
    commands_to_run = kDefaultCommandsToRun;
  }
  return commands_to_run;
}

void SendCommandsToParent(int fd, const string commands_to_run) {
  CHECK_NE(fd, -1);
  using std::cerr;
  LOG(INFO) << "Sending commands to parent: " << commands_to_run;
  if (write(fd, commands_to_run.c_str(), commands_to_run.size()) == -1) {
    cerr << "write: " << strerror(errno);
    exit(1);
  }
}

wstring StartServer(const CommandLineValues& args, bool connected_to_parent) {
  LOG(INFO) << "Starting server.";

  wstring address;
  std::unordered_set<int> surviving_fds = {1, 2};
  if (args.server && !args.server_path.empty()) {
    address = args.server_path;
    // We can't close stdout until we've printed the address in which the server
    // will run.
    Daemonize(surviving_fds);
  }
  wstring actual_address;
  wstring error;
  if (!StartServer(editor_state(), address, &actual_address, &error)) {
    LOG(FATAL) << args.binary_name << ": Unable to start server: " << error;
  }
  if (args.server) {
    if (!connected_to_parent) {
      std::cout << args.binary_name
                << ": Server starting at: " << actual_address << std::endl;
    }
    for (int fd : surviving_fds) {
      close(fd);
    }
  }
  return actual_address;
}

std::wstring GetGreetingMessage() {
  static std::vector<wstring> errors({
      L"Welcome to Edge!",
      L"Edge, your favorite text editor.",
      L"It looks like you're writing a letter. Would you like help?",
      L"Edge, a text editor.",
      L"All modules are now active.",
      L"Booting up Edge. . . . . . . . . . . . . DONE",
      L"What are you up to today?",
      L"The trouble is, you think you have time.",
      L"Happiness is here, and now.",
      L"The journey of a thousand miles begins with a single step.",
      L"Every moment is a fresh beginning.",
      L"Action is the foundational key to all success.",
  });
  return errors[rand() % errors.size()];
}
}  // namespace

int main(int argc, const char** argv) {
  using namespace afc::editor;
  using std::cerr;
  using std::unique_ptr;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  srand(time(NULL));

  string locale = std::setlocale(LC_ALL, "");
  LOG(INFO) << "Using locale: " << locale;

  LOG(INFO) << "Initializing command line arguments.";
  auto args = afc::command_line_arguments::Parse(afc::editor::CommandLineArgs(),
                                                 argc, argv);

  LOG(INFO) << "Setting up audio_player.";
  auto audio_player = args.mute ? NewNullAudioPlayer() : NewAudioPlayer();

  LOG(INFO) << "Creating editor.";
  global_editor_state = std::make_unique<EditorState>(args, audio_player.get());

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

  int remote_server_fd = -1;
  bool connected_to_parent = false;
  if (!args.client.empty()) {
    wstring parent_server_error;
    remote_server_fd =
        MaybeConnectToServer(ToByteString(args.client), &parent_server_error);
    if (remote_server_fd == -1) {
      cerr << args.binary_name
           << ": Unable to connect to remote server: " << parent_server_error
           << std::endl;
      exit(1);
    }
  } else {
    remote_server_fd = MaybeConnectToParentServer(nullptr);
    if (remote_server_fd != -1) {
      args.server = true;
      connected_to_parent = true;
    }
  }

  std::shared_ptr<Screen> screen_curses;
  if (!args.server) {
    LOG(INFO) << "Creating curses screen.";
    screen_curses = NewScreenCurses();
  }
  RegisterScreenType(editor_state()->environment().get());
  editor_state()->environment()->Define(
      L"screen", afc::vm::Value::NewObject(L"Screen", screen_curses));

  LOG(INFO) << "Starting server.";
  auto server_path = StartServer(args, connected_to_parent);
  while (editor_state()->WorkQueueNextExecution().has_value() &&
         editor_state()->WorkQueueNextExecution().value() < Now()) {
    editor_state()->ExecutePendingWork();
  }

  auto commands_to_run = CommandsToRun(args);
  if (!commands_to_run.empty()) {
    if (connected_to_parent) {
      commands_to_run += L"SendExitTo(\"" + server_path + L"\");";
    }

    LOG(INFO) << "Sending commands.";
    int self_fd;
    wstring errors;
    if (remote_server_fd != -1) {
      self_fd = remote_server_fd;
    } else if (args.server && !args.server_path.empty()) {
      self_fd = MaybeConnectToServer(ToByteString(args.server_path), &errors);
    } else {
      self_fd = MaybeConnectToParentServer(&errors);
    }
    if (self_fd == -1) {
      std::cerr << args.binary_name << ": " << errors << std::endl;
      exit(1);
    }
    CHECK_NE(self_fd, -1);
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

  BeepFrequencies(audio_player.get(), {783.99, 723.25, 783.99});
  editor_state()->status()->SetInformationText(GetGreetingMessage());

  LOG(INFO) << "Main loop starting.";
  while (!editor_state()->exit_value().has_value()) {
    // We execute pending work before updating screens, since we expect that the
    // pending work updates may have visible effects.
    VLOG(5) << "Executing pending work.";
    editor_state()->ExecutePendingWork();

    VLOG(5) << "Updating screens.";
    auto screen_state = editor_state()->FlushScreenState();
    if (screen_curses != nullptr) {
      if (args.client.empty()) {
        terminal.Display(editor_state(), screen_curses.get(), screen_state);
      } else {
        screen_curses->Refresh();  // Don't want this to be buffered!
        auto screen_size =
            LineColumnDelta{screen_curses->lines(), screen_curses->columns()};
        if (last_screen_size.has_value() &&
            screen_size != last_screen_size.value()) {
          LOG(INFO) << "Sending screen size update to server.";
          SendCommandsToParent(
              remote_server_fd,
              "screen.set_size(" +
                  std::to_string(screen_size.column.column_delta) + "," +
                  std::to_string(screen_size.line.line_delta) + ");" +
                  "set_screen_needs_hard_redraw(true);\n");
          last_screen_size = screen_size;
        }
      }
    }
    VLOG(5) << "Updating remote screens.";
    for (auto& buffer : *editor_state()->buffers()) {
      auto value =
          buffer.second->environment()->Lookup(L"screen", GetScreenVmType());
      if (value->type.type != VMType::OBJECT_TYPE ||
          value->type.object_type != L"Screen") {
        continue;
      }
      auto buffer_screen = static_cast<Screen*>(value->user_value.get());
      if (buffer_screen == nullptr) {
        continue;
      }
      if (buffer_screen == screen_curses.get()) {
        continue;
      }
      LOG(INFO) << "Remote screen for buffer: " << buffer.first;
      terminal.Display(editor_state(), buffer_screen, screen_state);
    }

    std::vector<std::shared_ptr<OpenBuffer>> buffers;

    // The file descriptor at position i will be either fd or fd_error of
    // buffers[i]. The exception to this is fd 0 (at the end).
    struct pollfd fds[editor_state()->buffers()->size() * 2 + 3];
    buffers.reserve(sizeof(fds) / sizeof(fds[0]));

    for (auto& buffer : *editor_state()->buffers()) {
      auto register_reader = [&](const FileDescriptorReader* reader) {
        auto pollfd = reader == nullptr ? std::nullopt : reader->GetPollFd();
        if (pollfd.has_value()) {
          fds[buffers.size()] = pollfd.value();
          buffers.push_back(buffer.second);
        }
      };
      register_reader(buffer.second->fd());
      register_reader(buffer.second->fd_error());
    }

    if (screen_curses != nullptr) {
      fds[buffers.size()].fd = 0;
      fds[buffers.size()].events = POLLIN | POLLPRI | POLLERR;
      buffers.push_back(nullptr);
    }

    fds[buffers.size()].fd = editor_state()->fd_to_detect_internal_events();
    fds[buffers.size()].events = POLLIN | POLLPRI;
    buffers.push_back(nullptr);

    auto now = Now();
    auto next_execution = editor_state()->WorkQueueNextExecution();
    int timeout_ms =
        next_execution.has_value()
            ? static_cast<int>(ceil(min(
                  max(0.0, MillisecondsBetween(now, next_execution.value())),
                  1000.0)))
            : 1000;
    VLOG(5) << "Timeout: " << timeout_ms << " has value "
            << (next_execution.has_value() ? "yes" : "no");
    if (poll(fds, buffers.size(), timeout_ms) == -1) {
      CHECK_EQ(errno, EINTR) << "poll failed: " << strerror(errno);

      LOG(INFO) << "Received signals.";
      if (args.client.empty()) {
        // We schedule a redraw in case the signal was SIGWINCH (the screen
        // size has changed). Ideally we'd only do that for that signal, to
        // avoid spurious refreshes, but... who cares.
        editor_state()->set_screen_needs_hard_redraw(true);

        editor_state()->ProcessSignals();
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
          editor_state()->Terminate(
              EditorState::TerminationType::kIgnoringErrors, 0);
        } else {
          CHECK(screen_curses != nullptr);
          wint_t c;
          while ((c = ReadChar(&mbstate)) != static_cast<wint_t>(-1)) {
            if (remote_server_fd == -1) {
              editor_state()->ProcessInput(c);
            } else {
              SendCommandsToParent(
                  remote_server_fd,
                  "ProcessInput(" + std::to_string(c) + ");\n");
            }
          }
        }
        continue;
      }

      if (fds[i].fd == editor_state()->fd_to_detect_internal_events()) {
        char buffer[4096];
        VLOG(5) << "Internal events detected.";
        while (read(fds[i].fd, buffer, sizeof(buffer)) > 0) continue;
        continue;
      }

      CHECK_LE(i, buffers.size());
      CHECK(buffers[i] != nullptr);
      if (buffers[i] && buffers[i]->fd() != nullptr &&
          fds[i].fd == buffers[i]->fd()->fd()) {
        LOG(INFO) << "Reading (normal): "
                  << buffers[i]->Read(buffer_variables::name);
        buffers[i]->ReadData();
      } else if (buffers[i] && buffers[i]->fd_error() != nullptr &&
                 fds[i].fd == buffers[i]->fd_error()->fd()) {
        LOG(INFO) << "Reading (error): "
                  << buffers[i]->Read(buffer_variables::name);
        buffers[i]->ReadErrorData();
      } else {
        LOG(FATAL) << "Invalid file descriptor.";
      }
    }
  }

  LOG(INFO) << "Removing server file: " << server_path;
  unlink(ToByteString(server_path).c_str());
  return editor_state()->exit_value().value();
}

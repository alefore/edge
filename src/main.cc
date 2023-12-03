#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>

extern "C" {
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
}

#include <glog/logging.h>

#include "src/args.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/audio.h"
#include "src/infrastructure/command_line.h"
#include "src/infrastructure/execution.h"
#include "src/infrastructure/file_descriptor_reader.h"
#include "src/infrastructure/screen/screen.h"
#include "src/infrastructure/time.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/run_command_handler.h"
#include "src/screen_curses.h"
#include "src/screen_vm.h"
#include "src/server.h"
#include "src/terminal.h"
#include "src/tests/benchmarks.h"
#include "src/tests/tests.h"
#include "src/vm/escape.h"
#include "src/vm/value.h"

namespace {

using namespace afc::editor;
namespace audio = afc::infrastructure::audio;

using afc::infrastructure::ExtendedChar;
using afc::infrastructure::FileDescriptor;
using afc::infrastructure::MillisecondsBetween;
using afc::infrastructure::Now;
using afc::infrastructure::Path;
using afc::infrastructure::Tracker;
using afc::infrastructure::UnixSignal;
using afc::infrastructure::execution::ExecutionEnvironment;
using afc::infrastructure::execution::ExecutionEnvironmentOptions;
using afc::infrastructure::screen::Screen;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IgnoreErrors;
using afc::language::IsError;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ToByteString;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineColumnDelta;

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
    editor_state().status().InsertError(
        Error(L"'*aq' to quit -- pending changes won't be saved."));
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
    if (!surviving_fds.empty()) {
      LOG(INFO) << "Closing file descriptors.";
      FileDescriptor dev_null = FileDescriptor(open("/dev/null", O_WRONLY));
      CHECK_NE(dev_null, FileDescriptor(-1));
      for (FileDescriptor fd : surviving_fds) dup2(dev_null.read(), fd.read());
      close(dev_null.read());
    }
  }
  LOG(INFO) << "Server address: " << server_address;
  return server_address;
}

NonNull<std::shared_ptr<Line>> GetGreetingMessage() {
  static const std::vector<NonNull<std::shared_ptr<Line>>> errors({
      MakeNonNullShared<Line>(L"Welcome to Edge!"),
      MakeNonNullShared<Line>(L"Edge, your favorite text editor."),
      MakeNonNullShared<Line>(
          L"📎 It looks like you're writing a letter. Would you like help?"),
      MakeNonNullShared<Line>(L"Edge, a text editor."),
      MakeNonNullShared<Line>(L"All modules are now active."),
      MakeNonNullShared<Line>(L"Booting up Edge. . . . . . . . . . . . . DONE"),
      MakeNonNullShared<Line>(L"What are you up to today?"),
      MakeNonNullShared<Line>(
          L"Stop trying to calm the storm. Calm yourself, the storm "
          L"will pass."),
      MakeNonNullShared<Line>(
          L"Learn to be indifferent to what makes no difference."),
      MakeNonNullShared<Line>(
          L"Whatever can happen at any time can happen today."),
      MakeNonNullShared<Line>(L"The trouble is, you think you have time."),
      MakeNonNullShared<Line>(L"Happiness is here, and now."),
      MakeNonNullShared<Line>(
          L"The journey of a thousand miles begins with a single step."),
      MakeNonNullShared<Line>(L"Every moment is a fresh beginning."),
      MakeNonNullShared<Line>(
          L"Action is the foundational key to all success."),
  });
  return errors.at(rand() % errors.size());
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
        CHECK(!IsError(SyncSendCommandsToServer(
            remote_server_fd.value(),
            "screen.set_size(" + std::to_string(screen_size.column.read()) +
                "," + std::to_string(screen_size.line.read()) + ");" +
                "editor.set_screen_needs_hard_redraw(true);\n")));
        *last_screen_size = screen_size;
      }
    }
  }
  VLOG(5) << "Updating remote screens.";
  for (OpenBuffer& buffer :
       *editor_state().buffers() | std::views::values | gc::view::Value) {
    static const afc::vm::Namespace kEmptyNamespace;
    std::optional<gc::Root<afc::vm::Value>> value =
        buffer.environment()->Lookup(editor_state().gc_pool(), kEmptyNamespace,
                                     L"screen", GetScreenVmType());
    if (!value.has_value() ||
        value.value().ptr()->type != afc::vm::Type{GetScreenVmType()}) {
      continue;
    }
    auto buffer_screen =
        afc::vm::VMTypeMapper<NonNull<std::shared_ptr<Screen>>>::get(
            value.value().ptr().value());
    if (&buffer_screen.value() == screen_curses) {
      continue;
    }
    LOG(INFO) << "Remote screen for buffer: " << buffer.name();
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
            L"screen",
            afc::vm::Value::NewObject(editor_state().gc_pool(),
                                      GetScreenVmType(), input_screen_curses));
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
    if (remote_server_fd.has_value()) {
      CHECK_NE(remote_server_fd.value(), FileDescriptor(-1));
      CHECK(!IsError(SyncSendCommandsToServer(remote_server_fd.value(),
                                              ToByteString(commands_to_run))));
    } else {
      BufferName name =
          editor_state().GetUnusedBufferName(L"- initial-commands");
      gc::Root<OpenBuffer> buffer_root = OpenBuffer::New(
          OpenBuffer::Options{.editor = editor_state(), .name = name});
      buffer_root.ptr()->EvaluateString(commands_to_run);
      editor_state().buffers()->insert_or_assign(name, std::move(buffer_root));
    }
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
  ExecutionEnvironment(
      ExecutionEnvironmentOptions{
          .stop_check = [] { return editor_state().exit_value().has_value(); },
          .get_next_alarm =
              [] { return editor_state().WorkQueueNextExecution(); },
          .on_signals =
              [&] {
                LOG(INFO) << "Received signals.";
                if (!args.client.has_value()) {
                  // We schedule a redraw in case the signal was SIGWINCH (the
                  // screen size has changed). Ideally we'd only do that for
                  // that signal, to avoid spurious refreshes, but... who cares.
                  editor_state().set_screen_needs_hard_redraw(true);

                  editor_state().ProcessSignals();
                }
              },
          .on_iteration =
              [&](afc::infrastructure::execution::IterationHandler& handler) {
                editor_state().ExecutionIteration(handler);

                VLOG(5) << "Updating screens.";
                RedrawScreens(args, remote_server_fd, &last_screen_size,
                              &terminal, screen_curses.get());

                if (screen_curses != nullptr)
                  handler.AddHandler(
                      FileDescriptor(0), POLLIN | POLLPRI | POLLERR,
                      [&](int received_events) {
                        if (received_events & POLLHUP) {
                          LOG(INFO) << "POLLHUP enabled in fd 0. "
                                       "AttemptTermination(0).";
                          editor_state().Terminate(
                              EditorState::TerminationType::kIgnoringErrors, 0);
                        } else {
                          CHECK(screen_curses != nullptr);
                          std::vector<ExtendedChar> input;
                          input.reserve(10);
                          {
                            std::optional<ExtendedChar> c;
                            while (input.size() < 1024 &&
                                   (c = ReadChar(&mbstate)) != std::nullopt) {
                              input.push_back(c.value());
                            }
                          }
                          if (remote_server_fd.has_value())
                            for (const ExtendedChar& c : input) {
                              static Tracker tracker(L"Main::ProcessInput");
                              auto call = tracker.Call();
                              if (const wchar_t* regular_c =
                                      std::get_if<wchar_t>(&c);
                                  regular_c != nullptr)
                                CHECK(!IsError(SyncSendCommandsToServer(
                                    remote_server_fd.value(),
                                    "ProcessInput(" +
                                        std::to_string(*regular_c) + ");\n")));
                            }
                          else
                            editor_state().ProcessInput(std::move(input));
                        }
                      });
              }})
      .Run();

  int output = editor_state().exit_value().value();
  std::shared_ptr<LazyString> exit_notice = editor_state().GetExitNotice();
  LOG(INFO) << "Deleting editor.";
  global_editor_state = nullptr;

  LOG(INFO) << "Deleting screen_curses.";
  screen_curses = nullptr;

  LOG(INFO) << "Returning";
  if (exit_notice != nullptr) std::cerr << *exit_notice;
  return output;
}

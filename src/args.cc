#include "src/args.h"

#include <fstream>
#include <iostream>
#include <string>

extern "C" {
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/infrastructure/command_line.h"
#include "src/infrastructure/dirname.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/server.h"
#include "src/tests/benchmarks.h"
#include "src/vm/escape.h"

using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IgnoreErrors;
using afc::language::NewError;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;

namespace afc::editor {
namespace {
static Path GetHomeDirectory() {
  if (char* env = getenv("HOME"); env != nullptr) {
    return std::visit(overload{[&](Error error) {
                                 LOG(FATAL) << "Invalid home directory (from "
                                               "`HOME` environment variable): "
                                            << error << ": " << env;
                                 return Path::Root();
                               },
                               [](Path path) { return path; }},
                      Path::New(LazyString{FromByteString(env)}));
  }
  if (struct passwd* entry = getpwuid(getuid()); entry != nullptr) {
    return std::visit(
        overload{[&](Error error) {
                   LOG(FATAL)
                       << "Invalid home directory (from `getpwuid`): " << error;
                   return Path::Root();
                 },
                 [](Path path) { return path; }},
        Path::New(LazyString{FromByteString(entry->pw_dir)}));
  }
  return Path::Root();  // What else?
}

static std::vector<std::wstring> GetEdgeConfigPath(const Path& home) {
  std::vector<std::wstring> output;
  std::unordered_set<Path> output_set;
  auto push = [&output, &output_set](Path path) {
    if (output_set.insert(path).second)
      // TODO(trivial, 2024-08-04): Avoid the calls to ToString.
      output.push_back(path.read().ToString());
  };
  push(Path::Join(home, PathComponent::FromString(L".edge")));
  LOG(INFO) << "Pushing config path: " << output[0];
  if (char* env = getenv("EDGE_PATH"); env != nullptr) {
    std::istringstream text_stream(std::string(env) + ";");
    std::string dir;
    // TODO: stat it and don't add it if it doesn't exist.
    while (std::getline(text_stream, dir, ';')) {
      std::visit(overload{IgnoreErrors{}, push},
                 Path::New(LazyString{FromByteString(dir)}));
    }
  }
  return output;
}

}  // namespace

using afc::command_line_arguments::Handler;

CommandLineValues::CommandLineValues() : home_directory(GetHomeDirectory()) {
  config_paths = GetEdgeConfigPath(home_directory);
}

const std::vector<Handler<CommandLineValues>>& CommandLineArgs() {
  static const std::vector<Handler<CommandLineValues>> handlers = {
      Handler<CommandLineValues>({L"fork", L"f"},
                                 L"Create a buffer running a shell command")
          .SetHelp(
              L"The `--fork` command-line argument must be followed by a shell "
              L"command. Edge will create a buffer running that command.\n\n"
              L"Example:\n\n"
              L"    edge --fork \"ls -lR /tmp\" --fork \"make\"\n\n"
              L"If Edge is running nested (inside an existing Edge), it will "
              L"cause the parent instance to open those buffers.")
          .Require(L"shellcmd", L"Shell command to run")
          .PushBackTo(&CommandLineValues::commands_to_fork),

      Handler<CommandLineValues>({L"run"}, L"Run a VM command")
          .SetHelp(
              L"The `--run` command-line argument must be followed by a string "
              L"with a VM command to run.\n\n"
              L"Example:\n\n"
              L"    edge --run 'string flags = \"-R\"; editor.ForkCommand(\"ls "
              L"\" + "
              L"flags, true);'\n\n")
          .Require(L"vmcmd", L"VM command to run")
          .AppendTo(&CommandLineValues::commands_to_run),

      Handler<CommandLineValues>({L"load", L"l"},
                                 L"Load a file with VM commands")
          .Require(L"path", L"Path to file containing VM commands to run")
          .Transform([](std::wstring value) {
            // TODO(easy, 2023-12-31): Avoid ToString.
            return L"buffer.EvaluateFile(" +
                   vm::EscapedString::FromString(LazyString{value})
                       .CppRepresentation()
                       .ToString() +
                   L");";
          })
          .AppendTo(&CommandLineValues::commands_to_run),

      Handler<CommandLineValues>({L"server", L"s"},
                                 L"Run in daemon mode (at an optional path)")
          .SetHelp(
              L"The `--server` command-line argument causes Edge to run in "
              L"*background* mode: without reading any input from stdin nor "
              L"producing any output to stdout. Instead, Edge will wait for "
              L"connections to the path given.\n\n"
              L"If you pass an empty string (or no argument), Edge generates "
              L"a temporary file. Otherwise, the path given must not currently "
              L"exist.\n\n"
              L"Edge always runs with a server, even when this flag is not "
              L"used. Passing this flag merely causes Edge to daemonize itself "
              L"and not use the current terminal. Technically, it's more "
              L"correct to say that this is \"background\" or \"headless\" "
              L"mode than to say that this is \"server\" mode. However, we "
              L"decided to use \"--server\" (instead of some other flag) for "
              L"symmetry with \"--client\".\n\n"
              L"For example, you'd start the server thus:\n\n"
              L"    edge --server /tmp/edge-server-blah\n\n"
              L"You can then connect a client:\n\n"
              L"    edge --client /tmp/edge-server-blah"
              L"If your session is terminated (e.g. your SSH connection dies), "
              L"you can run the client command again.")
          .Accept(L"path", L"Path to the pipe in which to run the server")
          .Set(&CommandLineValues::server_path,
               [](std::wstring input) -> ValueOrError<std::optional<Path>> {
                 if (input.empty()) {
                   return Success(std::optional<Path>());
                 }
                 // TODO(easy, 2024-08-04): Receive input already as LazyString
                 // and avoid explicit conversion.
                 ValueOrError<Path> output = Path::New(LazyString{input});
                 if (std::holds_alternative<Error>(output)) {
                   return std::get<Error>(output);
                 }
                 return Success(OptionalFrom(output));
               })
          .Set(&CommandLineValues::server, true),

      Handler<CommandLineValues>({L"client", L"c"},
                                 L"Connect to daemon at a given path")
          .Require(L"path",
                   L"Path to the pipe in which the daemon is listening")
          .Set(&CommandLineValues::client,
               [](std::wstring input) -> ValueOrError<std::optional<Path>> {
                 // TODO(easy, 2024-08-04): Receive input already as LazyString
                 // and avoid explicit conversion.
                 ValueOrError<Path> output = Path::New(LazyString{input});
                 if (std::holds_alternative<Error>(output)) {
                   return std::get<Error>(output);
                 }
                 return Success(OptionalFrom(output));
               }),

      Handler<CommandLineValues>({L"mute"}, L"Disable audio output")
          .Set(&CommandLineValues::mute, true)
          .Accept(L"bool", L""),

      Handler<CommandLineValues>({L"ao"}, L"Prompt for a path to open")
          .Set(&CommandLineValues::prompt_for_path, true),

      Handler<CommandLineValues>({L"bg"},
                                 L"Open buffers given to -f in background")
          .Set(&CommandLineValues::background, true),

      Handler<CommandLineValues>({L"X"}, L"If nested, exit early")
          .SetHelp(
              L"When `edge` runs nested (i.e., under a parent instance), the "
              L"child instance will not create any buffers for any files that "
              L"the user may have passed as command-line arguments nor any "
              L"commands (passed with `--fork`). Instead, it will connect to "
              L"the parent and request that the parent itself creates the "
              L"corresponding buffers.\n\n"
              L"The `-X` command-line argument controls when the child "
              L"instance will exit. By default, it will wait until any buffers "
              L"that it requests are deleted by the user (with `ad`). This is "
              L"suitable for commands such as `git commit` that may run a "
              L"nested instance of Edge. However, when `-X` is given, the "
              L"child instance will exit as soon as it has successfully "
              L"communicated with the parent (without waiting for the user to "
              L"delete corresponding buffers.")
          .Set(&CommandLineValues::nested_edge_behavior,
               CommandLineValues::NestedEdgeBehavior::kExitEarly),

      Handler<CommandLineValues>({L"benchmark"}, L"Run a benchmark")
          .Require(L"benchmark", L"The benchmark to run.")
          .Set<std::wstring>(
              &CommandLineValues::benchmark,
              [](std::wstring input) -> ValueOrError<std::wstring> {
                std::vector<std::wstring> benchmarks = tests::BenchmarkNames();
                if (std::find(benchmarks.begin(), benchmarks.end(), input) !=
                    benchmarks.end()) {
                  return input;
                }
                return NewError(LazyString{L"Invalid value (valid values: "} +
                                Concatenate(benchmarks |
                                            std::views::transform(
                                                [](const std::wstring& b) {
                                                  return LazyString{b};
                                                }) |
                                            Intersperse(LazyString{L", "})) +
                                LazyString{L")"});
              }),

      Handler<CommandLineValues>({L"view"}, L"Widget mode")
          .Require(L"mode",
                   L"The default view mode. Valid values are `all` and "
                   L"`default`.")
          .Set<CommandLineValues::ViewMode>(
              &CommandLineValues::view_mode,
              [](std::wstring input)
                  -> ValueOrError<CommandLineValues::ViewMode> {
                if (input == L"all")
                  return CommandLineValues::ViewMode::kAllBuffers;
                if (input == L"default")
                  return CommandLineValues::ViewMode::kDefault;
                return Error(
                    L"Invalid value (valid values are `all` and `default`): " +
                    input);
              }),

      Handler<CommandLineValues>({L"fps"}, L"Frames per second")
          .Require(L"fps",
                   L"The maximum number of frames per second to render. If the "
                   L"state in the editor changes more frequently than this "
                   L"value, not all changes will be displaed.")
          .Set(&CommandLineValues::frames_per_second),

      Handler<CommandLineValues>({L"p"},
                                 L"Apply search paths to initial local paths.")
          .SetHelp(
              L"Apply search paths  initial local paths: local paths given on "
              L"the command line (in the invocation to Edge) will be looked up "
              L"based on search paths (rather than simply attempting to open "
              L"them as relative paths to the current working directory).")
          .Set(&CommandLineValues::initial_path_resolution_behavior,
               CommandLineValues::LocalPathResolutionBehavior::kAdvanced),

      Handler<CommandLineValues>({L"prompt_history_read_only"},
                                 L"Don't append new entries to prompt history.")
          .SetHelp(
              L"By default, Edge appends new values given to prompts (e.g., "
              L"the open file or execute command prompts) to corresponding "
              L"files in the Edge runtime path (e.g., ~/.edge or $EDGE_PATH). "
              L"If this flag is given, that functionality is disabled (but "
              L"Edge will still attempt to read prompt history files).")
          .Set(&CommandLineValues::prompt_history_behavior,
               CommandLineValues::HistoryFileBehavior::kReadOnly),

      Handler<CommandLineValues>(
          {L"positions_history_read_only"},
          L"Don't append new entries to positions history.")
          .SetHelp(
              L"By default, Edge keeps track of positions you've visited in "
              L"`$EDGE_PATH/positions`. If this flag is given, that "
              L"functionality "
              L"is disabled (but Edge may still attempt to read previous "
              L"state).")
          .Set(&CommandLineValues::positions_history_behavior,
               CommandLineValues::HistoryFileBehavior::kReadOnly)};
  return handlers;
}

LazyString CommandsToRun(CommandLineValues args) {
  using afc::vm::EscapedString;
  // TODO(trivial, 2023-12-31): Avoid LazyString.
  LazyString commands_to_run =
      LazyString{args.commands_to_run} +
      LazyString{L"VectorBuffer buffers_to_watch = VectorBuffer();\n"};
  bool start_shell = args.commands_to_run.empty();
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
    commands_to_run +=
        LazyString{L"buffers_to_watch.push_back(editor.OpenFile("} +
        EscapedString::FromString(LazyString{full_path}).CppRepresentation() +
        LazyString{L", true));\n"};
    start_shell = false;
  }
  for (auto& command_to_fork : args.commands_to_fork) {
    commands_to_run +=
        LazyString{L"ForkCommandOptions options = ForkCommandOptions();\n"} +
        LazyString{L"options.set_command("} +
        EscapedString::FromString(LazyString{command_to_fork})
            .CppRepresentation() +
        LazyString{L");\noptions.set_insertion_type(\""} +
        LazyString{args.background ? L"skip" : L"search_or_create"} +
        LazyString{L"\");\n"} +
        LazyString{L"buffers_to_watch.push_back(editor.ForkCommand(options));"};
    start_shell = false;
  }
  switch (args.view_mode) {
    case CommandLineValues::ViewMode::kAllBuffers:
      commands_to_run +=
          LazyString{L"editor.set_multiple_buffers(true);\n"} +
          LazyString{L"editor.SetHorizontalSplitsWithAllBuffers();\n"};
      break;
    case CommandLineValues::ViewMode::kDefault:
      break;
  }
  if (args.client.has_value()) {
    static const char* kEdgeParentAddress = "EDGE_PARENT_ADDRESS";
    commands_to_run +=
        LazyString{L"Screen screen = RemoteScreen("} +
        EscapedString::FromString(
            LazyString{FromByteString(getenv(kEdgeParentAddress))})
            .CppRepresentation() +
        LazyString{L");\n"};
    start_shell = false;
  } else if (args.nested_edge_behavior ==
             CommandLineValues::NestedEdgeBehavior::kWaitForClose) {
    commands_to_run += LazyString{L"editor.WaitForClose(buffers_to_watch);\n"};
  }
  if (args.prompt_for_path) {
    commands_to_run += LazyString{L"editor.PromptAndOpenFile();"};
    start_shell = false;
  }
  if (start_shell) {
    static const LazyString kDefaultCommandsToRun = LazyString{
        L"ForkCommandOptions options = ForkCommandOptions();\n"
        L"options.set_command(\"sh -l\");\n"
        L"options.set_insertion_type(\"search_or_create\");\n"
        L"options.set_name(\"💻shell\");\n"
        L"editor.ForkCommand(options);"};
    commands_to_run += kDefaultCommandsToRun;
  }
  return commands_to_run;
}
}  // namespace afc::editor

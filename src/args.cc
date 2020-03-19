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

#include "src/command_line.h"
#include "src/server.h"
#include "src/wstring.h"

namespace afc::editor {
namespace {
static wstring GetHomeDirectory() {
  char* env = getenv("HOME");
  if (env != nullptr) {
    return FromByteString(env);
  }
  struct passwd* entry = getpwuid(getuid());
  if (entry != nullptr) {
    return FromByteString(entry->pw_dir);
  }
  return L"/";  // What else?
}

static vector<std::wstring> GetEdgeConfigPath(const std::wstring& home) {
  // TODO: Don't add repeated paths?
  vector<wstring> output;
  output.push_back(home + L"/.edge");
  LOG(INFO) << "Pushing config path: " << output[0];
  char* env = getenv("EDGE_PATH");
  if (env != nullptr) {
    std::istringstream text_stream(string(env) + ";");
    std::string dir;
    // TODO: stat it and don't add it if it doesn't exist.
    while (std::getline(text_stream, dir, ';')) {
      output.push_back(FromByteString(dir));
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
      Handler<CommandLineValues>::Help(L"Open the files given."),

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
              L"    edge --run 'string flags = \"-R\"; ForkCommand(\"ls \" + "
              L"flags, true);'\n\n")
          .Require(L"vmcmd", L"VM command to run")
          .AppendTo(&CommandLineValues::commands_to_run),

      Handler<CommandLineValues>({L"load", L"l"},
                                 L"Load a file with VM commands")
          .Require(L"path", L"Path to file containing VM commands to run")
          .Transform([](wstring value) {
            return L"buffer.EvaluateFile(\"" + CppEscapeString(value) + L"\");";
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
          .Set(&CommandLineValues::server_path)
          .Set(&CommandLineValues::server, true),

      Handler<CommandLineValues>({L"client", L"c"},
                                 L"Connect to daemon at a given path")
          .Require(L"path",
                   L"Path to the pipe in which the daemon is listening")
          .Set(&CommandLineValues::client),

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

      Handler<CommandLineValues>({L"tests"}, L"Unit tests behavior")
          .Require(
              L"behavior",
              L"The behavior for tests. Valid values are `run` and `list`.")
          .Set<CommandLineValues::TestsBehavior>(
              &CommandLineValues::tests_behavior,
              [](std::wstring input, std::wstring* error)
                  -> std::optional<CommandLineValues::TestsBehavior> {
                if (input == L"run")
                  return CommandLineValues::TestsBehavior::kRunAndExit;
                if (input == L"list")
                  return CommandLineValues::TestsBehavior::kListAndExit;
                *error =
                    L"Invalid value (valid values are `run` and `list`): " +
                    input;
                return std::nullopt;
              }),

      Handler<CommandLineValues>({L"view"}, L"Widget mode")
          .Require(L"mode",
                   L"The default view mode. Valid values are `all` and "
                   L"`default`.")
          .Set<CommandLineValues::ViewMode>(
              &CommandLineValues::view_mode,
              [](std::wstring input, std::wstring* error)
                  -> std::optional<CommandLineValues::ViewMode> {
                if (input == L"all")
                  return CommandLineValues::ViewMode::kAllBuffers;
                if (input == L"default")
                  return CommandLineValues::ViewMode::kDefault;
                *error =
                    L"Invalid value (valid values are `all` and `default`): " +
                    input;
                return std::nullopt;
              })};
  return handlers;
}

}  // namespace afc::editor

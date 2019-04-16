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

#include "src/server.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace command_line_arguments {
using std::wstring;

struct ParsingData {
  std::list<wstring> input;
  Values output;
};

Handler::Handler(std::vector<std::wstring> aliases, std::wstring short_help)
    : aliases_(aliases), short_help_(short_help) {}

Handler& Handler::Transform(
    std::function<std::wstring(std::wstring)> transform) {
  transform_ = std::move(transform);
  return *this;
}

Handler& Handler::PushBackTo(std::vector<std::wstring> Values::*field) {
  return PushDelegate([field](std::wstring* value, Values* args) {
    if (value != nullptr) {
      (args->*field).push_back(*value);
    }
  });
}

Handler& Handler::AppendTo(std::wstring(Values::*field)) {
  return PushDelegate([field](std::wstring* value, Values* args) {
    if (value != nullptr) {
      (args->*field) += *value;
    }
  });
}

Handler& Handler::Set(std::wstring Values::*field) {
  return PushDelegate([field](std::wstring* value, Values* args) {
    if (value != nullptr) {
      (args->*field) = *value;
    }
  });
}

Handler& Handler::Run(std::function<void()> callback) {
  return PushDelegate([callback](std::wstring*, Values*) { callback(); });
}

Handler& Handler::Run(std::function<void(Values*)> callback) {
  return PushDelegate(
      [callback](std::wstring*, Values* data) { callback(data); });
}

void Handler::Execute(ParsingData* data) const {
  auto cmd = data->input.front();
  data->input.pop_front();

  if (type_ == VariableType::kNone || data->input.empty()) {
    if (type_ == VariableType::kRequired) {
      std::cerr << data->output.binary_name << ": " << cmd
                << ": Expected argument: " << name_ << ": "
                << argument_description_ << std::endl;
      exit(1);
    } else {
      delegate_(nullptr, &data->output);
    }
  } else {
    wstring input = transform_(data->input.front());
    delegate_(&input, &data->output);
    data->input.pop_front();
  }
}

Handler& Handler::PushDelegate(
    std::function<void(std::wstring*, Values*)> delegate) {
  auto old_delegate = std::move(delegate_);
  delegate_ = [old_delegate, delegate](std::wstring* value, Values* args) {
    old_delegate(value, args);
    delegate(value, args);
  };
  return *this;
}

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

static vector<wstring> GetEdgeConfigPath(const wstring& home) {
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

void Help();

const std::vector<Handler>& Handlers() {
  static const std::vector<Handler> handlers = {
      Handler({L"help", L"h"}, L"Display help and exit")
          .SetHelp(
              L"The --help command-line argument displays a brief overview of "
              L"the available command line arguments and exits.")
          .Run(Help),

      Handler({L"fork", L"f"}, L"Create a buffer running a shell command")
          .SetHelp(
              L"The --fork command-line argument must be followed by a shell "
              L"command. Edge will create a buffer running that command.\n\n"
              L"Example:\n\n"
              L"    edge --fork \"ls -lR /tmp\" --fork \"make\"\n\n"
              L"If Edge is running nested (inside an existing Edge), it will "
              L"cause the parent instance to open those buffers.")
          .Require(L"shellcmd", L"Shell command to run")
          .PushBackTo(&Values::commands_to_fork),

      Handler({L"run"}, L"Run a VM command")
          .SetHelp(
              L"The --run command-line argument must be followed by a string "
              L"with a VM command to run.\n\n"
              L"Example:\n\n"
              L"    edge --run 'string flags = \"-R\"; ForkCommand(\"ls \" + "
              L"flags, true);'\n\n")
          .Require(L"vmcmd", L"VM command to run")
          .AppendTo(&Values::commands_to_run),

      Handler({L"load", L"l"}, L"Load a file with VM commands")
          .Require(L"path", L"Path to file containing VM commands to run")
          .Transform([](wstring value) {
            return L"buffer.EvaluateFile(\"" + CppEscapeString(value) + L"\");";
          })
          .AppendTo(&Values::commands_to_run),

      Handler({L"server", L"s"}, L"Run in daemon mode (at an optional path)")
          .SetHelp(
              L"The --server command-line argument causes Edge to run in "
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
          .Set(&Values::server_path)
          .Set(&Values::server, true),

      Handler({L"client", L"c"}, L"Connect to daemon at a given path")
          .Require(L"path",
                   L"Path to the pipe in which the daemon is listening")
          .Set(&Values::client),

      Handler({L"mute"}, L"Disable audio output").Set(&Values::mute, true),

      Handler({L"bg"}, L"Open buffers given to -f in background")
          .Set(&Values::background, true),

      Handler({L"X"}, L"If nested, exit early")
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
          .Set(&Values::nested_edge_behavior,
               Values::NestedEdgeBehavior::kExitEarly)};
  return handlers;
}  // namespace command_line_arguments

void Help() {
  std::cout << L"Usage: edge [OPTION]... [FILE]...\n"
               L"Open the files given.\n\n"
               L"Edge supports the following options:\n";

  std::vector<wstring> initial_table;
  for (auto& handler : Handlers()) {
    std::wstringbuf buffer;
    std::wostream os(&buffer);
    wstring prefix = L"  ";
    for (auto alias : handler.aliases()) {
      os << prefix << L"-" << alias;
      prefix = L", ";
    }
    switch (handler.argument_type()) {
      case Handler::VariableType::kRequired:
        os << L" <" << handler.argument() << L">";
        break;

      case Handler::VariableType::kOptional:
        os << L" [" << handler.argument() << L"]";
        break;

      case Handler::VariableType::kNone:
        break;
    }
    initial_table.push_back(buffer.str());
  }

  size_t max_length = 0;
  for (auto& entry : initial_table) {
    max_length = max(max_length, entry.size());
  }

  size_t padding = max_length + 2;

  for (size_t i = 0; i < Handlers().size(); i++) {
    auto& handler = Handlers()[i];
    std::cout << initial_table[i]
              << wstring(padding > initial_table[i].size()
                             ? padding - initial_table[i].size()
                             : 1,
                         L' ')
              << handler.short_help() << "\n";
  }
  exit(0);
}

Values Parse(int argc, const char** argv) {
  using std::cerr;
  using std::cout;

  ParsingData args_data;

  args_data.output.home_directory = GetHomeDirectory();
  args_data.output.config_paths =
      GetEdgeConfigPath(args_data.output.home_directory);

  for (auto config_path : args_data.output.config_paths) {
    auto flags_path = config_path + L"/flags.txt";
    LOG(INFO) << "Attempting to load additional flags from: " << flags_path;
    std::wifstream flags_stream(ToByteString(flags_path));
    flags_stream.imbue(std::locale(""));
    if (flags_stream.fail()) {
      LOG(INFO) << "Unable to open file, skipping";
      continue;
    }
    std::wstring line;
    while (std::getline(flags_stream, line)) {
      args_data.input.push_back(line);
    }
  }

  CHECK_GT(argc, 0);
  args_data.output.binary_name = FromByteString(argv[0]);
  for (int i = 1; i < argc; i++) {
    args_data.input.push_back(FromByteString(argv[i]));
  }

  std::map<wstring, int> handlers_map;
  for (size_t i = 0; i < Handlers().size(); i++) {
    for (auto& alias : Handlers()[i].aliases()) {
      handlers_map[L"-" + alias] = i;
      handlers_map[L"--" + alias] = i;
    }
  }

  while (!args_data.input.empty()) {
    wstring cmd = args_data.input.front();
    if (cmd.empty()) {
      args_data.input.pop_front();
      continue;
    }

    if (cmd[0] != '-') {
      args_data.output.files_to_open.push_back(cmd);
      args_data.input.pop_front();
      continue;
    }

    auto it = handlers_map.find(cmd);
    if (it == handlers_map.end()) {
      cerr << args_data.output.binary_name << ": Invalid flag: " << cmd
           << std::endl;
      exit(1);
    }
    Handlers()[it->second].Execute(&args_data);
  }

  return args_data.output;
}

}  // namespace command_line_arguments
}  // namespace editor
}  // namespace afc

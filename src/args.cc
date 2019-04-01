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

using std::wstring;

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

void Help(string) {
  string kHelpString =
      "Usage: edge [OPTION]... [FILE]...\n"
      "Open the files given.\n\n"
      "Edge supports the following options:\n"
      "  -f, --fork <shellcmd>  Creates a buffer running a shell command\n"
      "  -h, --help             Displays this message\n"
      "  --run <vmcmd>          Runs a VM command\n"
      "  --load <path>          Loads a file with VM commands\n"
      "  -s, --server <path>    Runs in daemon mode at path given\n"
      "  -c, --client <path>    Connects to daemon at path given\n"
      "  --mute                 Disables audio output\n"
      "  --bg                   -f opens buffers in background\n"
      "  -X                     If nested, exit early.\n";
  std::cout << kHelpString;
  exit(0);
}

Args ParseArgs(int argc, const char** argv) {
  using std::cerr;
  using std::cout;

  Args output;

  output.home_directory = GetHomeDirectory();
  output.config_paths = GetEdgeConfigPath(output.home_directory);

  std::list<string> inputs;
  for (auto config_path : output.config_paths) {
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
      inputs.push_back(ToByteString(line));
    }
  }

  CHECK_GT(argc, 0);
  output.binary_name = argv[0];
  for (int i = 1; i < argc; i++) {
    inputs.push_back(argv[i]);
  }

  auto WithArgument = [&](string name, std::function<void(string)> delegate) {
    return [name, delegate, &inputs, &output](string cmd) {
      if (inputs.empty()) {
        std::cerr << output.binary_name << ": " << cmd
                  << ": Expected argument: " << name << std::endl;
        exit(1);
      }
      delegate(inputs.front());
      inputs.pop_front();
    };
  };
  auto WithOptionalArgument = [&](std::function<void(string*)> delegate) {
    return [delegate, &inputs](string) {
      if (inputs.empty()) {
        delegate(nullptr);
      } else {
        delegate(&inputs.front());
        inputs.pop_front();
      }
    };
  };
  auto PushInto = [&](string name, std::vector<string>* v) {
    return WithArgument(name, [v](string x) { v->push_back(x); });
  };
  auto AppendString = [&](string name, string* s) {
    return WithArgument(name, [s](string x) { *s += x; });
  };

  struct Handler {
    std::vector<string> aliases;
    std::function<void(string)> callback;
  };

  std::vector<Handler> handlers = {
      {{"h", "help"}, Help},
      {{"f", "fork"}, PushInto("Command to fork", &output.commands_to_fork)},
      {{"run"}, AppendString("Command to run", &output.commands_to_run)},
      {{"l", "load"},
       WithArgument("Path to VM commands to run",
                    [&](string value) {
                      output.commands_to_run +=
                          "buffer.EvaluateFile(\"" +
                          ToByteString(CppEscapeString(FromByteString(value))) +
                          "\");";
                    })},
      {{"s", "server"}, WithOptionalArgument([&](string* value) {
         output.server = true;
         if (value != nullptr) {
           output.server_path = *value;
         }
       })},
      {{"c", "client"},
       WithArgument("Server path (given to -s)",
                    [&](string v) { output.client = v; })},
      {{"mute"}, [&](string) { output.mute = true; }},
      {{"bg"}, [&](string) { output.background = true; }},
      {{"X"}, [&](string) {
         output.nested_edge_behavior = Args::NestedEdgeBehavior::kExitEarly;
       }}};

  std::map<string, int> handlers_map;
  for (size_t i = 0; i < handlers.size(); i++) {
    for (auto& alias : handlers[i].aliases) {
      handlers_map["-" + alias] = i;
      handlers_map["--" + alias] = i;
    }
  }

  while (!inputs.empty()) {
    string cmd = inputs.front();
    inputs.pop_front();
    if (cmd.empty()) {
      continue;
    }

    if (cmd[0] != '-') {
      output.files_to_open.push_back(cmd);
      continue;
    }

    auto it = handlers_map.find(cmd);
    if (it == handlers_map.end()) {
      cerr << output.binary_name << ": Invalid flag: " << cmd << std::endl;
      exit(1);
    }
    handlers[it->second].callback(cmd);
  }

  return output;
}

}  // namespace editor
}  // namespace afc

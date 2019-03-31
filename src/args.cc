#include "src/args.h"

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

Args ParseArgs(int argc, const char** argv) {
  using std::cerr;
  using std::cout;

  string kHelpString =
      "Usage: edge [OPTION]... [FILE]...\n"
      "Open the files given.\n\nEdge supports the following options:\n"
      "  -f, --fork <shellcmd>  Creates a buffer running a shell command\n"
      "  -h, --help             Displays this message\n"
      "  --run <vmcmd>          Runs a VM command\n"
      "  --load <path>          Loads a file with VM commands\n"
      "  -s, --server <path>    Runs in daemon mode at path given\n"
      "  -c, --client <path>    Connects to daemon at path given\n"
      "  --mute                 Disables audio output\n"
      "  --bg                   -f opens buffers in background\n";

  Args output;

  output.home_directory = GetHomeDirectory();
  output.config_paths = GetEdgeConfigPath(output.home_directory);

  auto pop_argument = [&argc, &argv, &output]() {
    if (argc == 0) {
      cerr << output.binary_name << ": Parameters missing." << std::endl;
      exit(1);
    }
    argv++;
    argc--;
    return argv[-1];
  };

  output.binary_name = pop_argument();

  while (argc > 0) {
    string cmd = pop_argument();
    if (cmd.empty()) {
      continue;
    }
    if (cmd[0] != '-') {
      output.files_to_open.push_back(cmd);
    } else if (cmd == "--help" || cmd == "-h") {
      cout << kHelpString;
      exit(0);
    } else if (cmd == "--fork" || cmd == "-f") {
      CHECK_GT(argc, 0) << output.binary_name << ": " << cmd
                        << ": Expected command to fork.\n";
      output.commands_to_fork.push_back(pop_argument());
    } else if (cmd == "--run") {
      CHECK_GT(argc, 0) << output.binary_name << ": " << cmd
                        << ": Expected command to run.\n";
      output.commands_to_run += pop_argument();
    } else if (cmd == "--load" || cmd == "-l") {
      CHECK_GT(argc, 0) << output.binary_name << ": " << cmd
                        << ": Expected path to VM commands to run.\n";
      output.commands_to_run +=
          "buffer.EvaluateFile(\"" +
          ToByteString(CppEscapeString(FromByteString(pop_argument()))) +
          "\");";
    } else if (cmd == "--server" || cmd == "-s") {
      output.server = true;
      if (argc > 0) {
        output.server_path = pop_argument();
      }
    } else if (cmd == "--client" || cmd == "-c") {
      output.client = pop_argument();
      if (output.client.empty()) {
        cerr << output.binary_name << ": --client: Missing server path."
             << std::endl;
        exit(1);
      }
    } else if (cmd == "--mute") {
      output.mute = true;
    } else if (cmd == "--bg") {
      output.background = true;
    } else {
      cerr << output.binary_name << ": Invalid flag: " << cmd << std::endl;
      exit(1);
    }
  }

  return output;
}

}  // namespace editor
}  // namespace afc

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
struct ParsingData {
  std::list<wstring> input;
  Args output;
};

struct Handler {
  using Callback = std::function<void(ParsingData*)>;
  enum class VariableType { kRequired, kOptional, kNone };

  Handler(std::vector<wstring> aliases, wstring short_help)
      : aliases_(aliases), short_help_(short_help) {}

  Handler& Transform(std::function<wstring(wstring)> transform) {
    transform_ = std::move(transform);
    return *this;
  }

  Handler& PushBackTo(std::vector<wstring> Args::*field) {
    return PushDelegate([field](wstring* value, Args* args) {
      if (value != nullptr) {
        (args->*field).push_back(*value);
      }
    });
  }

  Handler& AppendTo(wstring(Args::*field)) {
    return PushDelegate([field](wstring* value, Args* args) {
      if (value != nullptr) {
        (args->*field) += *value;
      }
    });
  }

  template <typename Type>
  Handler& Set(Type Args::*field, Type value) {
    return PushDelegate(
        [field, value](wstring*, Args* args) { (args->*field) = value; });
  }

  Handler& Set(wstring Args::*field) {
    return PushDelegate([field](wstring* value, Args* args) {
      if (value != nullptr) {
        (args->*field) = *value;
      }
    });
  }

  Handler& Run(std::function<void()> callback) {
    return PushDelegate([callback](wstring*, Args*) { callback(); });
  }

  Handler& Run(std::function<void(Args*)> callback) {
    return PushDelegate([callback](wstring*, Args* data) { callback(data); });
  }

  void Execute(ParsingData* data) const {
    auto cmd = data->input.front();
    data->input.pop_front();

    if (type_ == VariableType::kNone || data->input.empty()) {
      if (type_ == VariableType::kRequired) {
        std::cerr << data->output.binary_name << ": " << cmd
                  << ": Expected argument: " << name_ << ": " << description_
                  << std::endl;
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

  Handler& Require(wstring name, wstring description) {
    type_ = VariableType::kRequired;
    name_ = name;
    description_ = description;
    return *this;
  }

  Handler& Accept(wstring name, wstring description) {
    type_ = VariableType::kOptional;
    name_ = name;
    description_ = description;
    return *this;
  }

  const std::vector<wstring>& aliases() const { return aliases_; }
  const wstring& short_help() const { return short_help_; }
  wstring argument() const { return name_; }
  VariableType argument_type() const { return type_; }

 private:
  Handler& PushDelegate(std::function<void(wstring*, Args*)> delegate) {
    auto old_delegate = std::move(delegate_);
    delegate_ = [old_delegate, delegate](wstring* value, Args* args) {
      old_delegate(value, args);
      delegate(value, args);
    };
    return *this;
  }

  std::vector<wstring> aliases_;
  wstring short_help_;

  VariableType type_ = VariableType::kNone;
  wstring name_;
  wstring description_;
  std::function<wstring(wstring)> transform_ = [](wstring x) { return x; };
  std::function<void(wstring*, Args*)> delegate_ = [](wstring*, Args*) {};
};

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

const std::vector<Handler> handlers(
    {Handler({L"h", L"help"}, L"Display help and exit").Run(Help),
     Handler({L"f", L"fork"}, L"Create a buffer running a shell command")
         .Require(L"shellcmd", L"Shell command to run")
         .PushBackTo(&Args::commands_to_fork),
     Handler({L"run"}, L"Run a VM command")
         .Require(L"vmcmd", L"VM command to run")
         .AppendTo(&Args::commands_to_run),
     Handler({L"l", L"load"}, L"Load a file with VM commands")
         .Require(L"path", L"Path to file containing VM commands to run")
         .Transform([](wstring value) {
           return L"buffer.EvaluateFile(\"" + CppEscapeString(value) + L"\");";
         })
         .AppendTo(&Args::commands_to_run),
     Handler({L"s", L"server"}, L"Run in daemon mode (at an optional path)")
         .Accept(L"path", L"Path to the pipe in which to run the server")
         .Set(&Args::server_path)
         .Set(&Args::server, true),
     Handler({L"c", L"client"}, L"Connect to daemon at a given path")
         .Require(L"path", L"Path to the pipe in which the daemon is listening")
         .Set(&Args::client),
     Handler({L"mute"}, L"Disable audio output").Set(&Args::mute, true),
     Handler({L"bg"}, L"Open buffers given to -f in background")
         .Set(&Args::background, true),
     Handler({L"X"}, L"If nested, exit early")
         .Set(&Args::nested_edge_behavior,
              Args::NestedEdgeBehavior::kExitEarly)});

void Help() {
  std::cout << L"Usage: edge [OPTION]... [FILE]...\n"
               L"Open the files given.\n\n"
               L"Edge supports the following options:\n";

  std::vector<wstring> initial_table;
  for (auto& handler : handlers) {
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
  for (size_t i = 0; i < handlers.size(); i++) {
    std::cout << initial_table[i]
              << wstring(padding > initial_table[i].size()
                             ? padding - initial_table[i].size()
                             : 1,
                         L' ')
              << handlers[i].short_help() << std::endl;
  }
  exit(0);
}

Args ParseArgs(int argc, const char** argv) {
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
  for (size_t i = 0; i < handlers.size(); i++) {
    for (auto& alias : handlers[i].aliases()) {
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
    handlers[it->second].Execute(&args_data);
  }

  return args_data.output;
}

}  // namespace editor
}  // namespace afc

// Generic command-line parsing logic library.
//
// This module allows specific applications to define their own flags and call
// the methods defined here in order to parse them into an application-specific
// structure (of type ParsedValues).
#ifndef __AFC_COMMAND_LINE_ARGUMENTS__H_
#define __AFC_COMMAND_LINE_ARGUMENTS__H_

#include <glog/logging.h>

#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <string>

// TODO: Don't depend on these "internal" (to Edge) symbols:
#include "src/wstring.h"

namespace afc::command_line_arguments {
template <typename ParsedValues>
class Handler;

template <typename ParsedValues>
struct ParsingData {
  const std::vector<Handler<ParsedValues>>* handlers;
  std::list<std::wstring> input;
  ParsedValues output;
};

template <typename ParsedValues>
class Handler {
 public:
  using Callback = std::function<void(ParsingData<ParsedValues>*)>;
  enum class VariableType { kRequired, kOptional, kNone };

  static Handler<ParsedValues> Help(std::wstring description) {
    return Handler<ParsedValues>({L"help", L"h"}, L"Display help and exit")
        .SetHelp(
            L"The `--help` command-line argument displays a brief overview "
            L"of the available command line arguments and exits.")
        .Run([description](ParsingData<ParsedValues>* data) {
          DisplayHelp(description, data);
        });
  }

  Handler(std::vector<std::wstring> aliases, std::wstring short_help)
      : aliases_(aliases), short_help_(short_help) {}

  Handler<ParsedValues>& Transform(
      std::function<std::wstring(std::wstring)> transform) {
    transform_ = std::move(transform);
    return *this;
  }

  Handler<ParsedValues>& PushBackTo(
      std::vector<std::wstring> ParsedValues::*field) {
    return PushDelegate(
        [field](std::wstring* value, ParsingData<ParsedValues>* data) {
          if (value != nullptr) {
            (data->output.*field).push_back(*value);
          }
        });
  }

  Handler<ParsedValues>& AppendTo(std::wstring(ParsedValues::*field)) {
    return PushDelegate(
        [field](std::wstring* value, ParsingData<ParsedValues>* data) {
          if (value != nullptr) {
            (data->output.*field) += *value;
          }
        });
  }

  template <typename Type>
  Handler<ParsedValues>& Set(Type ParsedValues::*field, Type value) {
    return PushDelegate(
        [field, value](std::wstring*, ParsingData<ParsedValues>* data) {
          (data->output.*field) = value;
        });
  }

  Handler<ParsedValues>& Set(std::wstring ParsedValues::*field) {
    return PushDelegate(
        [field](std::wstring* value, ParsingData<ParsedValues>* data) {
          if (value != nullptr) {
            (data->output.*field) = *value;
          }
        });
  }

  Handler<ParsedValues>& Run(
      std::function<void(ParsingData<ParsedValues>*)> callback) {
    return PushDelegate(
        [callback](std::wstring*, ParsingData<ParsedValues>* data) {
          callback(data);
        });
  }

  void Execute(ParsingData<ParsedValues>* data) const {
    auto cmd = data->input.front();
    data->input.pop_front();

    if (type_ == VariableType::kNone || data->input.empty()) {
      if (type_ == VariableType::kRequired) {
        std::cerr << data->output.binary_name << ": " << cmd
                  << ": Expected argument: " << name_ << ": "
                  << argument_description_ << std::endl;
        exit(1);
      } else {
        delegate_(nullptr, data);
      }
    } else {
      std::wstring input = transform_(data->input.front());
      delegate_(&input, data);
      data->input.pop_front();
    }
  }

  Handler<ParsedValues>& Require(std::wstring name, std::wstring description) {
    type_ = VariableType::kRequired;
    name_ = name;
    argument_description_ = description;
    return *this;
  }

  Handler<ParsedValues>& Accept(std::wstring name, std::wstring description) {
    type_ = VariableType::kOptional;
    name_ = name;
    argument_description_ = description;
    return *this;
  }

  const std::vector<std::wstring>& aliases() const { return aliases_; }
  const std::wstring& short_help() const { return short_help_; }
  Handler<ParsedValues>& SetHelp(std::wstring help) {
    help_ = std::move(help);
    return *this;
  }
  std::wstring help() const { return help_.empty() ? short_help_ : help_; }
  std::wstring argument() const { return name_; }
  std::wstring argument_description() const { return argument_description_; }
  VariableType argument_type() const { return type_; }

 private:
  Handler<ParsedValues>& PushDelegate(
      std::function<void(std::wstring*, ParsingData<ParsedValues>*)> delegate) {
    auto old_delegate = std::move(delegate_);
    delegate_ = [old_delegate, delegate](std::wstring* value,
                                         ParsingData<ParsedValues>* data) {
      old_delegate(value, data);
      delegate(value, data);
    };
    return *this;
  }

  static void DisplayHelp(std::wstring description,
                          ParsingData<ParsedValues>* data) {
    using afc::editor::ToByteString;
    std::cout << "Usage: " << ToByteString(data->output.binary_name)
              << " [OPTION]... [FILE]...\n"
              << ToByteString(description)
              << "\n\nSupports the following options:\n";

    std::vector<std::wstring> initial_table;
    for (const auto& handler : *data->handlers) {
      std::wstring line;
      std::wstring prefix = L"  ";
      for (auto alias : handler.aliases()) {
        line += prefix + L"-" + alias;
        prefix = L", ";
      }
      switch (handler.argument_type()) {
        case VariableType::kRequired:
          line += L" <" + handler.argument() + L">";
          break;

        case VariableType::kOptional:
          line += L" [" + handler.argument() + L"]";
          break;

        case VariableType::kNone:
          break;
      }
      initial_table.push_back(line);
    }

    size_t max_length = 0;
    for (auto& entry : initial_table) {
      max_length = std::max(max_length, entry.size());
    }

    size_t padding = max_length + 2;

    for (size_t i = 0; i < data->handlers->size(); i++) {
      const Handler<ParsedValues>& handler = (*data->handlers)[i];
      // TODO: Figure out how to get rid of the calls to `ToByteString`.
      std::cout << ToByteString(initial_table[i])
                << std::string(padding > initial_table[i].size()
                                   ? padding - initial_table[i].size()
                                   : 1,
                               ' ')
                << ToByteString(handler.short_help()) << "\n";
    }
    exit(0);
  }

  std::vector<std::wstring> aliases_;
  std::wstring short_help_;
  std::wstring help_;

  VariableType type_ = VariableType::kNone;
  std::wstring name_;
  std::wstring argument_description_;
  std::function<std::wstring(std::wstring)> transform_ = [](std::wstring x) {
    return x;
  };
  std::function<void(std::wstring*, ParsingData<ParsedValues>*)> delegate_ =
      [](std::wstring*, ParsingData<ParsedValues>*) {};
};

template <typename ParsedValues>
ParsedValues Parse(std::vector<Handler<ParsedValues>> handlers, int argc,
                   const char** argv) {
  using afc::editor::FromByteString;
  using afc::editor::ToByteString;
  using std::cerr;
  using std::cout;

  ParsingData<ParsedValues> args_data;
  args_data.handlers = &handlers;

  for (auto config_path : args_data.output.config_paths) {
    auto flags_path = config_path + L"/flags.txt";
    LOG(INFO) << "Attempting to load additional flags from: "
              << ToByteString(flags_path);
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

  std::map<std::wstring, int> handlers_map;
  for (size_t i = 0; i < handlers.size(); i++) {
    for (auto& alias : handlers[i].aliases()) {
      handlers_map[L"-" + alias] = i;
      handlers_map[L"--" + alias] = i;
    }
  }

  while (!args_data.input.empty()) {
    std::wstring cmd = args_data.input.front();
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

}  // namespace afc::command_line_arguments

#endif  // __AFC_COMMAND_LINE_ARGUMENTS__H_

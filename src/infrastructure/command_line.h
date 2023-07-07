// Generic command-line parsing logic library.
//
// This module allows specific applications to define their own flags and call
// the methods defined here in order to parse them into an application-specific
// structure (of type ParsedValues).
//
// To use this, create a structure that extends `StandardArguments` containing
// all the outputs of the parsing of flags.
//
//     struct MyArgs : StandardArguments {
//       std::string foo;
//     };
//
// Then call `Parse`, passing a handler for each flag. Use the `Handler` class
// below to provide semantics about the flags.
//
// Example handler:
//
//     Handler<CommandLineValues>({L"input", L"i"},
//                                 L"Set the input file")
//          .Require(L"path", L"CSV file to read")
//          .Set(&MyArgs::foo),

#ifndef __AFC_COMMAND_LINE_ARGUMENTS__H_
#define __AFC_COMMAND_LINE_ARGUMENTS__H_

#include <glog/logging.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <optional>
#include <string>

extern "C" {
#include <sysexits.h>
}

#include "src/language/wstring.h"

namespace afc::command_line_arguments {
template <typename ParsedValues>
class Handler;

enum class TestsBehavior { kRunAndExit, kListAndExit, kIgnore };

// ParsedValues should inherit from `StandardArguments`. This contains standard
// fields that the command-line parsing logic uses.
struct StandardArguments {
  // Input parameter.
  std::vector<std::wstring> config_paths;

  TestsBehavior tests_behavior = TestsBehavior::kIgnore;

  // Output parameter with the name of the binary.
  std::wstring binary_name;
  std::vector<std::wstring> naked_arguments;
};

template <typename ParsedValues>
struct ParsingData {
  const std::vector<Handler<ParsedValues>>* handlers;
  std::list<std::wstring> input;
  ParsedValues output;
  std::wstring current_flag;
  std::optional<std::wstring> current_value;
};

template <typename ParsedValues>
class Handler {
 public:
  using Callback = std::function<void(ParsingData<ParsedValues>*)>;
  enum class VariableType { kRequired, kOptional, kNone };

  static std::vector<Handler<ParsedValues>> StandardHandlers() {
    return {
        Handler<ParsedValues>({L"help", L"h"}, L"Display help and exit")
            .SetHelp(
                L"The `--help` command-line argument displays a brief overview "
                L"of the available command line arguments and exits.")
            .Run([](ParsingData<ParsedValues>* data) { DisplayHelp(data); }),
        Handler<ParsedValues>({L"tests"}, L"Unit tests behavior")
            .Require(
                L"behavior",
                L"The behavior for tests. Valid values are `run` and `list`.")
            .Set(&ParsedValues::tests_behavior,
                 [](std::wstring input,
                    std::wstring* error) -> std::optional<TestsBehavior> {
                   if (input == L"run") return TestsBehavior::kRunAndExit;
                   if (input == L"list") return TestsBehavior::kListAndExit;
                   *error =
                       L"Invalid value (valid values are `run` and `list`): " +
                       input;
                   return std::nullopt;
                 })};
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
    return PushDelegate([field](ParsingData<ParsedValues>* data) {
      if (data->current_value.has_value()) {
        (data->output.*field).push_back(data->current_value.value());
      }
    });
  }

  Handler<ParsedValues>& AppendTo(std::wstring(ParsedValues::*field)) {
    return PushDelegate([field](ParsingData<ParsedValues>* data) {
      if (data->current_value.has_value()) {
        (data->output.*field) += data->current_value.value();
      }
    });
  }

  Handler<ParsedValues>& Set(bool ParsedValues::*field, bool default_value) {
    return PushDelegate([field,
                         default_value](ParsingData<ParsedValues>* data) {
      if (data->current_value.has_value() && data->current_value != L"true" &&
          data->current_value != L"false") {
        std::cerr << data->output.binary_name << ": " << data->current_flag
                  << ": Invalid bool value (expected \"true\" or \"false\"): "
                  << data->current_value.value() << std::endl;
        exit(EX_USAGE);
      }
      (data->output.*field) = data->current_value.has_value()
                                  ? data->current_value.value() == L"true"
                                  : default_value;
    });
  }

  template <typename Type>
  Handler<ParsedValues>& Set(Type ParsedValues::*field, Type value) {
    return PushDelegate([field, value](ParsingData<ParsedValues>* data) {
      if (data->current_value.has_value() && data->current_value != L"true" &&
          data->current_value != L"false") {
        std::cerr << data->output.binary_name << ": " << data->current_flag
                  << ": Invalid value: " << data->current_value.value()
                  << std::endl;
        exit(EX_USAGE);
      }
      (data->output.*field) = value;
    });
  }

  // Class can be ParsedValues or a super-class of it. This is useful for fields
  // in StandardArguments.
  // Callable should receive two arguments: std::wstring input, and
  // std::wstring* error. It should return an std::optional<Type>.
  template <typename Type, typename Class, typename Callable>
  Handler<ParsedValues>& Set(Type Class::*field, Callable callback) {
    return PushDelegate([field, callback](ParsingData<ParsedValues>* data) {
      if (data->current_value.has_value()) {
        // TODO(easy, 2023-07-04): Switch to ValueOrError.
        std::wstring error;
        auto value = callback(data->current_value.value(), &error);
        if (!value.has_value()) {
          std::cerr << data->output.binary_name << ": " << data->current_flag
                    << ": " << error << std::endl;
          exit(EX_USAGE);
        }
        (data->output.*field) = value.value();
      }
    });
  }

  Handler<ParsedValues>& Set(std::wstring ParsedValues::*field) {
    return PushDelegate([field](ParsingData<ParsedValues>* data) {
      if (data->current_value.has_value()) {
        (data->output.*field) = data->current_value.value();
      }
    });
  }

  Handler<ParsedValues>& Set(double ParsedValues::*field) {
    return PushDelegate([field](ParsingData<ParsedValues>* data) {
      try {
        data->output.*field = std::stod(data->current_value.value());
      } catch (const std::invalid_argument& ia) {
        std::cerr << data->output.binary_name << ": " << data->current_flag
                  << ": " << ia.what() << std::endl;
      } catch (const std::out_of_range& oor) {
        std::cerr << data->output.binary_name << ": " << data->current_flag
                  << ": " << oor.what() << std::endl;
      }
    });
  }

  Handler<ParsedValues>& Run(
      std::function<void(ParsingData<ParsedValues>*)> callback) {
    return PushDelegate(callback);
  }

  void Execute(ParsingData<ParsedValues>* data) const {
    switch (type_) {
      case VariableType::kNone:
        if (data->current_value.has_value()) {
          std::cerr << data->output.binary_name << ": " << data->current_flag
                    << ": Flag does not accept arguments: " << name_ << ": "
                    << argument_description_ << std::endl;
          exit(EX_USAGE);
        }
        return delegate_(data);

      case VariableType::kRequired:
        if (data->current_value.has_value()) {
          // Okay.
        } else if (data->input.empty()) {
          std::cerr << data->output.binary_name << ": " << data->current_flag
                    << ": Expected argument: " << name_ << ": "
                    << argument_description_ << std::endl;
          exit(EX_USAGE);
        } else {
          data->current_value = data->input.front();
          data->input.pop_front();
        }
        // Fallthrough.

      case VariableType::kOptional:
        if (data->current_value.has_value()) {
          data->current_value = transform_(data->current_value.value());
        }
        return delegate_(data);
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
      std::function<void(ParsingData<ParsedValues>*)> delegate) {
    auto old_delegate = std::move(delegate_);
    delegate_ = [old_delegate, delegate](ParsingData<ParsedValues>* data) {
      old_delegate(data);
      delegate(data);
    };
    return *this;
  }

  static void DisplayHelp(ParsingData<ParsedValues>* data) {
    using language::ToByteString;
    std::cout << "Usage: " << ToByteString(data->output.binary_name)
              << " [OPTION]... [FILE]...\n\n"
              << "Supports the following options:\n";

    std::vector<std::wstring> initial_table;
    std::vector<const Handler<ParsedValues>*> sorted_handlers;
    for (const Handler<ParsedValues>& handler : *data->handlers)
      sorted_handlers.push_back(&handler);
    std::sort(sorted_handlers.begin(), sorted_handlers.end(),
              [](const auto& a, const auto& b) {
                return a->aliases().front() < b->aliases().front();
              });

    for (const Handler<ParsedValues>* handler : sorted_handlers) {
      std::wstring line;
      std::wstring prefix = L"  ";
      for (const std::wstring& alias : handler->aliases()) {
        line += prefix + L"-" + alias;
        prefix = L", ";
      }
      switch (handler->argument_type()) {
        case VariableType::kRequired:
          line += L" <" + handler->argument() + L">";
          break;

        case VariableType::kOptional:
          line += L"[=" + handler->argument() + L"]";
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

    for (size_t i = 0; i < sorted_handlers.size(); i++) {
      const Handler<ParsedValues>* handler = sorted_handlers[i];
      // TODO: Figure out how to get rid of the calls to `ToByteString`.
      std::cout << ToByteString(initial_table[i])
                << std::string(padding > initial_table[i].size()
                                   ? padding - initial_table[i].size()
                                   : 1,
                               ' ')
                << ToByteString(handler->short_help()) << "\n";
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
  std::function<void(ParsingData<ParsedValues>*)> delegate_ =
      [](ParsingData<ParsedValues>*) {};
};

void HonorStandardArguments(const StandardArguments& arguments);

template <typename ParsedValues>
ParsedValues Parse(std::vector<Handler<ParsedValues>> handlers, int argc,
                   const char** argv) {
  using language::FromByteString;
  using language::ToByteString;
  using std::cerr;
  using std::cout;

  for (const auto& h : Handler<ParsedValues>::StandardHandlers())
    handlers.push_back(h);

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
      args_data.output.naked_arguments.push_back(cmd);
      args_data.input.pop_front();
      continue;
    }

    if (auto equals = cmd.find('='); equals != std::wstring::npos) {
      args_data.current_flag = cmd.substr(0, equals);
      args_data.current_value = cmd.substr(equals + 1, cmd.size());
    } else {
      args_data.current_flag = cmd;
      args_data.current_value = std::nullopt;
    }
    args_data.input.pop_front();

    if (auto it = handlers_map.find(args_data.current_flag);
        it != handlers_map.end()) {
      handlers[it->second].Execute(&args_data);
    } else {
      cerr << args_data.output.binary_name << ": Invalid flag: " << cmd
           << std::endl;
      exit(EX_USAGE);
    }
  }

  HonorStandardArguments(args_data.output);
  return args_data.output;
}

}  // namespace afc::command_line_arguments

#endif  // __AFC_COMMAND_LINE_ARGUMENTS__H_

#ifndef __AFC_EDITOR_SRC_ARGS_H__
#define __AFC_EDITOR_SRC_ARGS_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace afc {
namespace editor {
namespace command_line_arguments {

using std::wstring;

class ParsingData;

struct Values {
  wstring binary_name;
  wstring home_directory;
  std::vector<std::wstring> config_paths;

  std::vector<std::wstring> files_to_open;
  std::vector<std::wstring> commands_to_fork;

  // Contains C++ (VM) code to execute.
  wstring commands_to_run;

  bool server = false;
  wstring server_path = L"";

  // If non-empty, path of the server to connect to.
  wstring client = L"";

  bool mute = false;
  bool background = false;

  enum class NestedEdgeBehavior {
    // Wait until the buffers we open have been closed in the parent.
    kWaitForClose,
    // Exit as soon as we know that we've successfully communicated with the
    // parent.
    kExitEarly,
  };

  NestedEdgeBehavior nested_edge_behavior = NestedEdgeBehavior::kWaitForClose;
};

Values Parse(int argc, const char** argv);

class Handler {
 public:
  using Callback = std::function<void(ParsingData*)>;
  enum class VariableType { kRequired, kOptional, kNone };

  Handler(std::vector<std::wstring> aliases, std::wstring short_help);

  Handler& Transform(std::function<std::wstring(std::wstring)> transform);
  Handler& PushBackTo(std::vector<std::wstring> Values::*field);
  Handler& AppendTo(std::wstring(Values::*field));
  template <typename Type>
  Handler& Set(Type Values::*field, Type value) {
    return PushDelegate([field, value](std::wstring*, Values* args) {
      (args->*field) = value;
    });
  }

  Handler& Set(std::wstring Values::*field);

  Handler& Run(std::function<void()> callback);
  Handler& Run(std::function<void(Values*)> callback);

  void Execute(ParsingData* data) const;

  Handler& Require(std::wstring name, wstring description) {
    type_ = VariableType::kRequired;
    name_ = name;
    argument_description_ = description;
    return *this;
  }

  Handler& Accept(std::wstring name, wstring description) {
    type_ = VariableType::kOptional;
    name_ = name;
    argument_description_ = description;
    return *this;
  }

  const std::vector<std::wstring>& aliases() const { return aliases_; }
  const wstring& short_help() const { return short_help_; }
  Handler& SetHelp(std::wstring help) {
    help_ = std::move(help);
    return *this;
  }
  wstring help() const { return help_.empty() ? short_help_ : help_; }
  wstring argument() const { return name_; }
  wstring argument_description() const { return argument_description_; }
  VariableType argument_type() const { return type_; }

 private:
  Handler& PushDelegate(std::function<void(std::wstring*, Values*)> delegate);

  std::vector<std::wstring> aliases_;
  wstring short_help_;
  wstring help_;

  VariableType type_ = VariableType::kNone;
  std::wstring name_;
  std::wstring argument_description_;
  std::function<std::wstring(std::wstring)> transform_ = [](std::wstring x) {
    return x;
  };
  std::function<void(std::wstring*, Values*)> delegate_ = [](std::wstring*,
                                                             Values*) {};
};

const std::vector<Handler>& Handlers();

}  // namespace command_line_arguments
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SRC_ARGS_H__

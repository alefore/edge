#include "cpp_command.h"

#include <memory>

#include <glog/logging.h>

#include "command.h"
#include "editor.h"
#include "vm/public/vm.h"
#include "wstring.h"

namespace afc {
namespace editor {

using std::wstring;

namespace {

wstring GetDescriptionString(wstring code) {
  size_t end = code.find(L'\n');
  if (end == code.npos) {
    return code;
  }
  auto first_line = code.substr(0, end);
  VLOG(5) << "First line: " << first_line;
  wstring prefix = L"// ";
  if (first_line.size() < prefix.size() || first_line.substr(0, 3) != prefix) {
    return first_line;
  }
  return first_line.substr(prefix.size());
}

class CppCommand : public Command {
 public:
  CppCommand(std::unique_ptr<afc::vm::Expression> expression, wstring code)
      : expression_(std::move(expression)),
        code_(std::move(code)),
        description_(GetDescriptionString(code_)) {
    CHECK(expression_ != nullptr);
  }

  const std::wstring Description() override { return description_; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    Evaluate(expression_.get(), editor_state->environment());
  }

 private:
  const std::unique_ptr<afc::vm::Expression> expression_;
  const wstring code_;
  const wstring description_;
};

}  // namespace

std::unique_ptr<Command> NewCppCommand(afc::vm::Environment* environment,
                                       wstring code) {
  wstring error_description;
  auto expr = afc::vm::CompileString(code, environment, &error_description);
  if (expr == nullptr) {
    LOG(ERROR) << "Failed compilation of command: " << code << ": "
               << error_description;
    return nullptr;
  }

  return std::unique_ptr<Command>(
      new CppCommand(std::move(expr), std::move(code)));
}

}  // namespace editor
}  // namespace afc


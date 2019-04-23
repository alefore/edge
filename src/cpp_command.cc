#include "src/cpp_command.h"

#include <glog/logging.h>

#include <memory>

#include "src/command.h"
#include "src/editor.h"
#include "src/vm/public/vm.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

using std::wstring;

namespace {

wstring GetFirstLine(wstring code) {
  size_t end = code.find(L'\n');
  if (end == code.npos) {
    return code;
  }
  auto first_line = code.substr(0, end);
  DVLOG(5) << "First line: " << first_line;
  wstring prefix = L"// ";
  if (first_line.size() < prefix.size() || first_line.substr(0, 3) != prefix) {
    return first_line;
  }
  return first_line.substr(prefix.size());
}

wstring GetDescriptionString(wstring code) {
  auto first_line = GetFirstLine(code);
  auto colon = first_line.find(L':');
  return colon == wstring::npos ? first_line : first_line.substr(colon + 1);
}

wstring GetCategoryString(wstring code) {
  auto first_line = GetFirstLine(code);
  auto colon = first_line.find(L':');
  return colon == wstring::npos ? L"Unknown" : first_line.substr(0, colon);
}

class CppCommand : public Command {
 public:
  CppCommand(std::unique_ptr<afc::vm::Expression> expression, wstring code)
      : expression_(std::move(expression)),
        code_(std::move(code)),
        description_(GetDescriptionString(code_)),
        category_(GetCategoryString(code_)) {
    CHECK(expression_ != nullptr);
  }

  std::wstring Description() const override { return description_; }
  std::wstring Category() const override { return category_; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    DVLOG(4) << "CppCommand starting (" << description_ << ")";
    auto expression = expression_;
    std::shared_ptr<OpenBuffer> buffer = editor_state->current_buffer();
    CHECK(buffer != nullptr);

    Evaluate(
        expression_.get(), editor_state->environment(),
        [expression](std::unique_ptr<Value>) {
          DVLOG(5) << "CppCommand finished.";
        },
        [buffer](std::function<void()> callback) {
          buffer->SchedulePendingWork(std::move(callback));
        });
  }

 private:
  const std::shared_ptr<afc::vm::Expression> expression_;
  const wstring code_;
  const wstring description_;
  const wstring category_;
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

  return std::make_unique<CppCommand>(std::move(expr), std::move(code));
}

}  // namespace editor
}  // namespace afc

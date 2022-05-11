#include "src/cpp_command.h"

#include <glog/logging.h>

#include <memory>

#include "src/command.h"
#include "src/editor.h"
#include "src/language/wstring.h"
#include "src/vm/public/vm.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;
using language::ValueOrError;
namespace gc = language::gc;

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
  CppCommand(EditorState& editor_state,
             NonNull<std::unique_ptr<afc::vm::Expression>> expression,
             wstring code)
      : editor_state_(editor_state),
        expression_(std::move(expression)),
        code_(std::move(code)),
        description_(GetDescriptionString(code_)),
        category_(GetCategoryString(code_)) {}

  std::wstring Description() const override { return description_; }
  std::wstring Category() const override { return category_; }

  void ProcessInput(wint_t) override {
    DVLOG(4) << "CppCommand starting (" << description_ << ")";
    Evaluate(*expression_, editor_state_.gc_pool(), editor_state_.environment(),
             [work_queue =
                  editor_state_.work_queue()](std::function<void()> callback) {
               work_queue->Schedule(std::move(callback));
             });
  }

 private:
  EditorState& editor_state_;
  const NonNull<std::unique_ptr<vm::Expression>> expression_;
  const wstring code_;
  const wstring description_;
  const wstring category_;
};

}  // namespace

ValueOrError<NonNull<std::unique_ptr<Command>>> NewCppCommand(
    EditorState& editor_state, gc::Root<afc::vm::Environment> environment,
    wstring code) {
  auto result = vm::CompileString(code, std::move(environment));
  if (result.IsError()) {
    LOG(ERROR) << "Failed compilation of command: " << code << ": "
               << result.error();
    return result.error();
  }
  return MakeNonNullUnique<CppCommand>(editor_state, std::move(result.value()),
                                       std::move(code));
}

}  // namespace afc::editor

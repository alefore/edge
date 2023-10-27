#include "src/cpp_command.h"

#include <glog/logging.h>

#include <memory>

#include "src/command.h"
#include "src/concurrent/work_queue.h"
#include "src/editor.h"
#include "src/language/wstring.h"
#include "src/vm/vm.h"

namespace afc::editor {
using concurrent::WorkQueue;
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
             wstring code, gc::Ptr<vm::Environment> environment)
      : editor_state_(editor_state),
        expression_(std::move(expression)),
        code_(std::move(code)),
        description_(GetDescriptionString(code_)),
        category_(GetCategoryString(code_)),
        environment_(std::move(environment)) {}

  std::wstring Description() const override { return description_; }
  std::wstring Category() const override { return category_; }

  void ProcessInput(wint_t) override {
    DVLOG(4) << "CppCommand starting (" << description_ << ")";
    Evaluate(expression_, editor_state_.gc_pool(), environment_.ToRoot(),
             [work_queue =
                  editor_state_.work_queue()](std::function<void()> callback) {
               work_queue->Schedule(
                   WorkQueue::Callback{.callback = std::move(callback)});
             });
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {environment_.object_metadata()};
  }

 private:
  EditorState& editor_state_;
  const NonNull<std::shared_ptr<vm::Expression>> expression_;
  const wstring code_;
  const wstring description_;
  const wstring category_;
  const gc::Ptr<vm::Environment> environment_;
};

}  // namespace

ValueOrError<gc::Root<Command>> NewCppCommand(
    EditorState& editor_state, gc::Root<afc::vm::Environment> environment,
    wstring code) {
  ASSIGN_OR_RETURN(
      NonNull<std::unique_ptr<vm::Expression>> result,
      vm::CompileString(code, editor_state.gc_pool(), environment));
  return editor_state.gc_pool().NewRoot(MakeNonNullUnique<CppCommand>(
      editor_state, std::move(result), std::move(code), environment.ptr()));
}

}  // namespace afc::editor

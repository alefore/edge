#include "src/cpp_command.h"

#include <glog/logging.h>

#include <memory>

#include "src/command.h"
#include "src/concurrent/work_queue.h"
#include "src/editor.h"
#include "src/infrastructure/extended_char.h"
#include "src/language/wstring.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;
using afc::concurrent::WorkQueue;
using afc::infrastructure::ExtendedChar;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;

namespace afc::editor {
namespace {
LazyString GetFirstLine(LazyString code) {
  std::optional<ColumnNumber> end = FindFirstOf(code, {L'\n'});
  if (end == std::nullopt) return code;
  LazyString first_line = code.Substring(ColumnNumber{}, end->ToDelta());
  DVLOG(5) << "First line: " << first_line;
  if (LazyString prefix = LazyString{L"// "}; StartsWith(first_line, prefix))
    return first_line.Substring(ColumnNumber{} + prefix.size());
  return first_line;
}

LazyString GetDescriptionString(LazyString code) {
  LazyString first_line = GetFirstLine(code);
  return VisitOptional(
      [&first_line](ColumnNumber colon) {
        return first_line.Substring(colon + ColumnNumberDelta{1});
      },
      [&] { return first_line; }, FindFirstOf(first_line, {L':'}));
}

LazyString GetCategoryString(LazyString code) {
  LazyString first_line = GetFirstLine(code);
  return VisitOptional(
      [&first_line](ColumnNumber colon) {
        return first_line.Substring(ColumnNumber{}, colon.ToDelta());
      },
      [&first_line] { return first_line; }, FindFirstOf(first_line, {L':'}));
}

class CppCommand : public Command {
  EditorState& editor_state_;
  const NonNull<std::shared_ptr<vm::Expression>> expression_;
  const LazyString code_;
  const LazyString description_;
  const LazyString category_;
  const gc::Ptr<vm::Environment> environment_;

 public:
  CppCommand(EditorState& editor_state,
             NonNull<std::unique_ptr<afc::vm::Expression>> expression,
             LazyString code, gc::Ptr<vm::Environment> environment)
      : editor_state_(editor_state),
        expression_(std::move(expression)),
        code_(std::move(code)),
        description_(GetDescriptionString(code_)),
        category_(GetCategoryString(code_)),
        environment_(std::move(environment)) {}

  LazyString Description() const override { return description_; }
  // TODO(trivial, 2024-08-13): Get rid of ToString.
  std::wstring Category() const override { return category_.ToString(); }

  void ProcessInput(ExtendedChar) override {
    DVLOG(4) << "CppCommand starting (" << description_ << ")";
    Evaluate(expression_, editor_state_.gc_pool(), environment_.ToRoot(),
             [work_queue = editor_state_.work_queue()](
                 OnceOnlyFunction<void()> callback) {
               work_queue->Schedule(
                   WorkQueue::Callback{.callback = std::move(callback)});
             });
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {environment_.object_metadata()};
  }
};

}  // namespace

ValueOrError<gc::Root<Command>> NewCppCommand(
    EditorState& editor_state, gc::Root<afc::vm::Environment> environment,
    std::wstring code) {
  ASSIGN_OR_RETURN(
      NonNull<std::unique_ptr<vm::Expression>> result,
      vm::CompileString(code, editor_state.gc_pool(), environment));
  // TODO(2024-01-24): Get rid of LazyString{}.
  return editor_state.gc_pool().NewRoot(MakeNonNullUnique<CppCommand>(
      editor_state, std::move(result), LazyString{std::move(code)},
      environment.ptr()));
}

}  // namespace afc::editor

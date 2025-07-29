#include "src/cpp_command.h"

#include <glog/logging.h>

#include <memory>

#include "src/command.h"
#include "src/concurrent/work_queue.h"
#include "src/editor.h"
#include "src/infrastructure/extended_char.h"
#include "src/language/text/line_sequence.h"
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
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::LineSequence;

namespace afc::editor {
namespace {
SingleLine GetFirstLine(LazyString code) {
  SingleLine first_line =
      LineSequence::BreakLines(std::move(code)).front().contents();
  DVLOG(5) << "First line: " << first_line;
  if (LazyString prefix = LazyString{L"// "}; StartsWith(first_line, prefix))
    return first_line.Substring(ColumnNumber{} + prefix.size());
  return first_line;
}

SingleLine GetDescriptionString(LazyString code) {
  SingleLine first_line = GetFirstLine(code);
  return VisitOptional(
      [&first_line](ColumnNumber colon) {
        return first_line.Substring(colon + ColumnNumberDelta{1});
      },
      [&] { return first_line; }, FindFirstOf(first_line, {L':'}));
}

ValueOrError<CommandCategory> GetCategoryString(LazyString code) {
  DECLARE_OR_RETURN(NonEmptySingleLine first_line,
                    NonEmptySingleLine::New(GetFirstLine(code)));
  return VisitOptional(
      [&first_line](ColumnNumber colon) {
        return CommandCategory::New(NonEmptySingleLine::New(
            first_line.Substring(ColumnNumber{}, colon.ToDelta())));
      },
      [&first_line] { return Success(CommandCategory{first_line}); },
      FindFirstOf(first_line, {L':'}));
}

class CppCommand : public Command {
  const gc::Ptr<ExecutionContext::CompilationResult> compilation_result_;
  const LazyString description_;
  const CommandCategory category_;

 public:
  CppCommand(gc::Ptr<ExecutionContext::CompilationResult> compilation_result,
             LazyString code, CommandCategory category)
      : compilation_result_(std::move(compilation_result)),
        description_(ToLazyString(GetDescriptionString(code))),
        category_(category) {}

  LazyString Description() const override { return description_; }
  CommandCategory Category() const override { return category_; }

  void ProcessInput(ExtendedChar) override {
    DVLOG(4) << "CppCommand starting (" << description_ << ")";
    compilation_result_->evaluate();
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {compilation_result_.object_metadata()};
  }
};

}  // namespace

ValueOrError<gc::Root<Command>> NewCppCommand(
    ExecutionContext& execution_context, const LazyString& code) {
  ASSIGN_OR_RETURN(CommandCategory category, GetCategoryString(code));
  ASSIGN_OR_RETURN(gc::Root<ExecutionContext::CompilationResult> result,
                   execution_context.CompileString(code));
  return execution_context.environment().pool().NewRoot(
      MakeNonNullUnique<CppCommand>(std::move(result).ptr(), code, category));
}

}  // namespace afc::editor

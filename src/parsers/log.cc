#include "src/parsers/log.h"

#include "src/concurrent/protected.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/infrastructure/tracker.h"
#include "src/language/error/value_or_error.h"
#include "src/language/error/view.h"
#include "src/language/gc.h"
#include "src/log_model.h"
#include "src/log_model_vm.h"
#include "src/vm/types.h"
#include "src/vm/value.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;
using afc::concurrent::Protected;
using afc::infrastructure::screen::LineModifierSet;
using afc::infrastructure::screen::ModifierFromString;
using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::ValueOrError;
using afc::language::VisitValue;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineRange;
using afc::vm::Expression;
using afc::vm::Identifier;

namespace afc::editor::parsers {
class LogTreeParser : public TreeParser {
  const LogType log_type_;
  const LogView log_view_;

 public:
  LogTreeParser(LogType log_type, LogView log_view)
      : log_type_(std::move(log_type)), log_view_(std::move(log_view)) {}

  StateBoundary state_boundary() const override { return StateBoundary::Line; }

  ParseTree FindChildren(const language::text::LineSequence& contents,
                         language::text::Range range) {
    TRACK_OPERATION(LogTreeParser_FindChildren);
    // TODO(2026-04-30, P2): Consider moving evaluator_ to be a class value?
    LogEvaluator evaluator(log_type_);
    ParseTree output = ParseTree(range);
    output.set_properties(ParseTree::PropertyMap{
        {ParseTreePropertyName::Link(), LazyString{}},
        {ParseTreePropertyName::LinkTarget(),
         LazyString{L"vm:buffer."} + ToLazyString(kOpenLogLineIdentifier()) +
             LazyString{L"()"}}});
    CompiledLogView compiled_log_view(evaluator, log_view_);
    range.ForEachLine([&](LineNumber i) {
      VisitValue(
          log_type_.Parse(contents.at(i).contents().read()), [&](LogLine line) {
            ParseTree line_output(
                LineRange(LineColumn{i},
                          std::numeric_limits<ColumnNumberDelta>::max())
                    .read());
            std::expected<std::unordered_map<LogEntryName, LineModifierSet>,
                          Error>
                modifiers_map = compiled_log_view.Evaluate(
                    line.values | std::views::transform(&LogEntryValue::name) |
                        std::ranges::to<std::unordered_set>(),
                    line);
            if (!modifiers_map) {
              LOG(INFO) << "Error: " << modifiers_map.error();
              return;
            }

            TRACK_OPERATION(LogTreeParser_ProduceTrees);
            std::ranges::for_each(line.values, [&](const LogEntryValue& data) {
              ParseTree child(
                  LineRange(LineColumn{i, data.position}, data.size).read());
              child.set_modifiers(modifiers_map->find(data.name)->second);
              line_output.PushChild(std::move(child));
            });
            output.PushChild(std::move(line_output));
          });
    });
    return output;
  }
};

language::NonNull<std::unique_ptr<TreeParser>> NewLogTreeParser(
    LogType log_type, LogView log_view) {
  LOG(INFO) << "Building LogTreeParser." << log_type;
  return MakeNonNullUnique<LogTreeParser>(std::move(log_type),
                                          std::move(log_view));
}
}  // namespace afc::editor::parsers

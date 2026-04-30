#include "src/parsers/log.h"

#include "src/infrastructure/screen/line_modifier.h"
#include "src/log_model.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::MakeNonNullUnique;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineRange;

namespace afc::editor::parsers {
class LogTreeParser : public TreeParser {
  const LogModel log_model_;
  const LogType log_type_;

 public:
  LogTreeParser(LogModel log_model, LogType log_type)
      : log_model_(std::move(log_model)), log_type_(std::move(log_type)) {}

  ParseTree FindChildren(const language::text::LineSequence& contents,
                         language::text::Range range) {
    ParseTree output = ParseTree(range);
    // TODO(P1, 2026-04-30, trivial): Don't hard-code the view here. Receive it
    // at construction.
    auto view = log_model_.views.find(
        LogViewName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"main")});
    if (view == log_model_.views.end()) {
      LOG(INFO) << "Unable to find view: " << view;
      return output;
    }
    range.ForEachLine([&](LineNumber i) {
      VisitValue(
          log_type_.Parse(contents.at(i).contents().read()), [&](LogLine line) {
            std::ranges::for_each(
                line.values, [&](std::pair<LogEntryName, LogEntryValue> entry) {
                  if (auto it = view->second.expressions.find(entry.first);
                      it != view->second.expressions.end()) {
                    ParseTree child(
                        LineRange(LineColumn{i, entry.second.position},
                                  entry.second.size)
                            .read());
                    // TODO(P1, 2026-04-30): Evaluate the expressions instead of
                    // hard-coding bold.
                    child.set_modifiers(LineModifierSet{LineModifier::kBold});
                    output.PushChild(std::move(child));
                  }
                });
          });
    });
    return output;
  }
};

language::NonNull<std::unique_ptr<TreeParser>> NewLogTreeParser(
    LogModel log_model, LogType log_type) {
  LOG(INFO) << "Building LogTreeParser." << log_type;
  return MakeNonNullUnique<LogTreeParser>(std::move(log_model),
                                          std::move(log_type));
}
}  // namespace afc::editor::parsers

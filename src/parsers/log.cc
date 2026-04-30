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
  const LogType log_type_;

 public:
  LogTreeParser(LogType log_type) : log_type_(std::move(log_type)) {}

  ParseTree FindChildren(const language::text::LineSequence& contents,
                         language::text::Range range) {
    ParseTree output = ParseTree(range);
    range.ForEachLine([&](LineNumber i) {
      VisitValue(
          log_type_.Parse(contents.at(i).contents().read()), [&](LogLine line) {
            std::ranges::for_each(
                line.values, [&](std::pair<LogEntryName, LogEntryValue> entry) {
                  // TODO(P1, log, 2026-04-28): Remove the "bold" handling here
                  // and just add a level of indirection based on the view
                  // configurations. This is just to test the waters.
                  if (entry.first == LogEntryName{L"bold"}) {
                    ParseTree child(
                        LineRange(LineColumn{i, entry.second.position},
                                  entry.second.size)
                            .read());
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
    LogType log_type) {
  LOG(INFO) << "Building LogTreeParser." << log_type;
  return MakeNonNullUnique<LogTreeParser>(std::move(log_type));
}
}  // namespace afc::editor::parsers

#include "src/infrastructure/regular_file_adapter.h"

#include "src/language/lazy_string/substring.h"
#include "src/language/text/line.h"
#include "src/language/text/line_builder.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::Observers;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineColumnDelta;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;

namespace afc::infrastructure {
RegularFileAdapter::RegularFileAdapter(Options options)
    : options_(std::move(options)) {}

void RegularFileAdapter::UpdateSize() {}

std::optional<language::text::LineColumn> RegularFileAdapter::position() const {
  return std::nullopt;
}

void RegularFileAdapter::SetPositionToZero() {}

std::vector<NonNull<std::shared_ptr<const Line>>> CreateLineInstances(
    LazyString contents, const LineModifierSet& modifiers) {
  TRACK_OPERATION(FileDescriptorReader_CreateLineInstances);

  std::vector<NonNull<std::shared_ptr<const Line>>> lines_to_insert;
  lines_to_insert.reserve(4096);
  ColumnNumber line_start;
  for (ColumnNumber i; i.ToDelta() < ColumnNumberDelta(contents.size()); ++i) {
    if (contents.get(i) == '\n') {
      VLOG(8) << "Adding line from " << line_start << " to " << i;

      LineBuilder line_options;
      line_options.set_contents(
          Substring(contents, line_start, ColumnNumber(i) - line_start));
      line_options.set_modifiers(ColumnNumber(0), modifiers);
      lines_to_insert.emplace_back(std::move(line_options).Build());

      line_start = ColumnNumber(i) + ColumnNumberDelta(1);
    }
  }

  VLOG(8) << "Adding last line from " << line_start << " to "
          << contents.size();
  LineBuilder line_options;
  line_options.set_contents(Substring(contents, line_start));
  line_options.set_modifiers(ColumnNumber(0), modifiers);
  lines_to_insert.emplace_back(std::move(line_options).Build());
  return lines_to_insert;
}

futures::Value<EmptyValue> RegularFileAdapter::ReceiveInput(
    language::lazy_string::LazyString str, const LineModifierSet& modifiers) {
  return options_.thread_pool
      .Run(std::bind_front(CreateLineInstances, std::move(str), modifiers))
      .Transform([options = options_](
                     std::vector<NonNull<std::shared_ptr<const Line>>> lines) {
        TRACK_OPERATION(RegularFileAdapter_ReceiveInput);
        CHECK_GT(lines.size(), 0ul);
        options.insert_lines(std::move(lines));
        return EmptyValue();
      });
}

bool RegularFileAdapter::WriteSignal(UnixSignal) { return false; }
}  // namespace afc::infrastructure

#include "src/frame_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/text/line_builder.h"

using afc::infrastructure::screen::Color;using afc::infrastructure::screen::StandardColor;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;

namespace afc::editor {

Line FrameLine(FrameOutputProducerOptions options) {
  Style line_modifiers =
      options.active_state == FrameOutputProducerOptions::ActiveState::Inactive
          ? Style{.attributes = StyleAttribute::Dim}
          : Style{.foreground_color = StandardColor::Cyan,
                  .attributes = StyleAttribute::Bold};
  Style title_modifiers =
      options.active_state == FrameOutputProducerOptions::ActiveState::Active
          ? Style{.foreground_color = StandardColor::Cyan,
                  .attributes = StyleAttribute::Bold | StyleAttribute::Reverse}
          : Style();
  LineBuilder output;
  output.AppendString(options.prefix, line_modifiers);
  output.AppendString(SingleLine::Padding<L'─'>(ColumnNumberDelta{2}),
                      line_modifiers);
  if (!options.title.empty())
    output.AppendString(
        SingleLine::Char<L' '>() + options.title + SingleLine::Char<L' '>(),
        title_modifiers);
  if (options.position_in_parent.has_value()) {
    output.AppendString(SingleLine::Char<L'─'>() + SingleLine::Char<L'('>(),
                        line_modifiers);
    // Add 1 because that matches what the repetitions do. Humans
    // typically start counting from 1.
    output.AppendString(SingleLine{LazyString{std::to_wstring(
                            1 + options.position_in_parent.value())}},
                        Style{.foreground_color = StandardColor::Cyan,
                              .attributes = StyleAttribute::Bold});
    output.AppendString(SingleLine::Char<L')'>(), line_modifiers);
  }

  if (!options.extra_information.size().IsZero()) {
    output.AppendString(SingleLine::Char<L'─'>() + SingleLine::Char<L'<'>(),
                        line_modifiers);
    output.AppendString(options.extra_information, line_modifiers);
    output.AppendString(SingleLine::Char<L'>'>(), line_modifiers);
  }

  output.AppendString(
      SingleLine::Padding<L'─'>(options.width -
                                ColumnNumberDelta(output.modifiers_size())),
      line_modifiers);

  return std::move(output).Build();
}

}  // namespace afc::editor

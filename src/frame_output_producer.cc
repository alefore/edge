#include "src/frame_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/language/lazy_string/padding.h"

namespace afc::editor {
using language::lazy_string::ColumnNumberDelta;
using language::text::Line;
using language::text::LineBuilder;

Line FrameLine(FrameOutputProducerOptions options) {
  LineModifierSet line_modifiers =
      options.active_state == FrameOutputProducerOptions::ActiveState::kInactive
          ? LineModifierSet({LineModifier::kDim})
          : LineModifierSet({LineModifier::kBold, LineModifier::kCyan});
  LineModifierSet title_modifiers =
      options.active_state == FrameOutputProducerOptions::ActiveState::kActive
          ? LineModifierSet({LineModifier::kBold, LineModifier::kCyan,
                             LineModifier::kReverse})
          : LineModifierSet();
  LineBuilder output;
  output.AppendString(options.prefix, line_modifiers);
  output.AppendString(L"──", line_modifiers);
  if (!options.title.empty()) {
    output.AppendString(L" " + options.title + L" ", title_modifiers);
  }
  if (options.position_in_parent.has_value()) {
    output.AppendString(L"─(", line_modifiers);
    // Add 1 because that matches what the repetitions do. Humans
    // typically start counting from 1.
    output.AppendString(
        std::to_wstring(1 + options.position_in_parent.value()),
        LineModifierSet{LineModifier::kBold, LineModifier::kCyan});
    output.AppendString(L")", line_modifiers);
  }

  if (!options.extra_information.empty()) {
    output.AppendString(L"─<", line_modifiers);
    output.AppendString(options.extra_information, line_modifiers);
    output.AppendString(L">", line_modifiers);
  }

  output.AppendString(
      Padding(options.width - ColumnNumberDelta(output.modifiers_size()), L'─'),
      line_modifiers);

  return std::move(output).Build();
}

}  // namespace afc::editor

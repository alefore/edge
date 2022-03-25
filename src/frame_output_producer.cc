#include "src/frame_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace afc {
namespace editor {
FrameOutputProducer::FrameOutputProducer(Options options)
    : line_(
          [options,
           line_modifiers =
               options.active_state == Options::ActiveState::kInactive
                   ? LineModifierSet({LineModifier::DIM})
                   : LineModifierSet({LineModifier::BOLD, LineModifier::CYAN}),
           title_modifiers =
               options.active_state == Options::ActiveState::kActive
                   ? LineModifierSet({LineModifier::BOLD, LineModifier::CYAN,
                                      LineModifier::REVERSE})
                   : LineModifierSet()] {
            Line::Options output;
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
                  LineModifierSet{LineModifier::BOLD, LineModifier::CYAN});
              output.AppendString(L")", line_modifiers);
            }

            if (!options.extra_information.empty()) {
              output.AppendString(L"─<", line_modifiers);
              output.AppendString(options.extra_information, line_modifiers);
              output.AppendString(L">", line_modifiers);
            }

            output.AppendString(
                ColumnNumberDelta::PaddingString(
                    options.width - ColumnNumberDelta(output.modifiers.size()),
                    L'─'),
                line_modifiers);
            return std::make_shared<Line>(std::move(output));
          }()) {}

OutputProducer::Output FrameOutputProducer::Produce(LineNumberDelta lines) {
  return Output{
      .lines = std::vector(
          lines.line_delta,
          Generator{.inputs_hash = {},
                    .generate =
                        [line = OutputProducer::LineWithCursor{.line = line_}] {
                          return line;
                        }}),
      .width = line_->contents()->size()};
}

}  // namespace editor
}  // namespace afc

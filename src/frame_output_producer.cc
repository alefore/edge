#include "src/frame_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace afc {
namespace editor {
FrameOutputProducer::FrameOutputProducer(FrameOptions options)
    : options_(std::move(options)),
      line_modifiers_(options_.active_state ==
                              FrameOptions::ActiveState::kInactive
                          ? LineModifierSet({LineModifier::DIM})
                          : LineModifierSet({LineModifier::BOLD})),
      title_modifiers_(options_.active_state ==
                               FrameOptions::ActiveState::kActive
                           ? LineModifierSet({LineModifier::BOLD})
                           : LineModifierSet()) {}

OutputProducer::Generator FrameOutputProducer::Next() {
  return Generator{
      std::nullopt, [this]() {
        Line::Options output;
        output.AppendString(options_.prefix, line_modifiers_);
        output.AppendString(L"──", line_modifiers_);
        if (!options_.title.empty()) {
          output.AppendString(L" " + options_.title + L" ", title_modifiers_);
        }
        if (options_.position_in_parent.has_value()) {
          output.AppendString(L"─(", line_modifiers_);
          // Add 1 because that matches what the repetitions do. Humans
          // typically start counting from 1.
          output.AppendString(
              std::to_wstring(1 + options_.position_in_parent.value()),
              LineModifierSet{LineModifier::BOLD, LineModifier::CYAN});
          output.AppendString(L")", line_modifiers_);
        }

        if (!options_.extra_information.empty()) {
          output.AppendString(L"─<", line_modifiers_);
          output.AppendString(options_.extra_information, line_modifiers_);
          output.AppendString(L">", line_modifiers_);
        }

        output.AppendString(
            ColumnNumberDelta::PaddingString(
                options_.width - ColumnNumberDelta(output.modifiers.size()),
                L'─'),
            line_modifiers_);
        return OutputProducer::LineWithCursor{
            std::make_shared<Line>(std::move(output)), std::nullopt};
      }};
}

}  // namespace editor
}  // namespace afc

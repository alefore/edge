#include "src/frame_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/output_receiver.h"

namespace afc {
namespace editor {
void FrameOutputProducer::WriteLine(OutputProducer::Options options) {
  LineModifier line_modifier = LineModifier::RESET;
  LineModifier title_modifier = LineModifier::RESET;
  switch (options_.active_state) {
    case FrameOptions::ActiveState::kActive:
      line_modifier = LineModifier::RESET;
      title_modifier = LineModifier::BOLD;
      break;
    case FrameOptions::ActiveState::kInactive:
      line_modifier = LineModifier::DIM;
      title_modifier = LineModifier::RESET;
      break;
  }

  options.receiver->AddModifier(line_modifier);
  options.receiver->AddString(L"──");
  if (!options_.title.empty()) {
    options.receiver->AddModifier(LineModifier::RESET);
    options.receiver->AddModifier(title_modifier);
    options.receiver->AddString(L" " + options_.title + L" ");
    options.receiver->AddModifier(LineModifier::RESET);
    options.receiver->AddModifier(line_modifier);
  }
  if (options_.position_in_parent.has_value()) {
    options.receiver->AddString(L"─(");
    options.receiver->AddModifier(LineModifier::BOLD);
    options.receiver->AddModifier(LineModifier::CYAN);
    // Add 1 because that matches what the repetitions do. Humans typically
    // start counting from 1.
    options.receiver->AddString(
        std::to_wstring(1 + options_.position_in_parent.value()));
    options.receiver->AddModifier(LineModifier::RESET);
    options.receiver->AddModifier(line_modifier);
    options.receiver->AddString(L")");
  }

  if (!options_.extra_information.empty()) {
    options.receiver->AddString(L"─<");
    options.receiver->AddString(options_.extra_information);
    options.receiver->AddString(L">");
  }

  options.receiver->AddString(std::wstring(
      options.receiver->width() - options.receiver->column(), L'─'));
  options.receiver->AddModifier(LineModifier::RESET);
}

}  // namespace editor
}  // namespace afc

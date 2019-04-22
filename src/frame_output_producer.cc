#include "src/frame_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/output_receiver.h"

namespace afc {
namespace editor {
void FrameOutputProducer::WriteLine(OutputProducer::Options options) {
  LineModifier default_modifier = LineModifier::RESET;
  switch (options_.active_state) {
    case FrameOptions::ActiveState::kActive:
      default_modifier = LineModifier::BOLD;
      break;
    case FrameOptions::ActiveState::kInactive:
      break;
  }

  options.receiver->AddModifier(default_modifier);
  options.receiver->AddString(
      L"──" + (options_.title.empty() ? L"" : L" " + options_.title + L" ") +
      L"─");
  if (options_.position_in_parent.has_value()) {
    options.receiver->AddString(L"(");
    options.receiver->AddModifier(LineModifier::CYAN);
    // Add 1 because that matches what the repetitions do. Humans typically
    // start counting from 1.
    options.receiver->AddString(
        std::to_wstring(1 + options_.position_in_parent.value()));
    options.receiver->AddModifier(LineModifier::RESET);
    options.receiver->AddModifier(default_modifier);
    options.receiver->AddString(L")");
  }

  options.receiver->AddString(std::wstring(
      options.receiver->width() - options.receiver->column(), L'─'));
  options.receiver->AddModifier(LineModifier::RESET);
}

}  // namespace editor
}  // namespace afc

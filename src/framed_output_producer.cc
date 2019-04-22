#include "src/framed_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/output_receiver.h"

namespace afc {
namespace editor {
void FramedOutputProducer::WriteLine(Options options) {
  if (lines_written_++ == 0) {
    AddFirstLine(std::move(options));
    return;
  }

  const auto original_active_cursor = options.active_cursor;
  std::optional<size_t> active_cursor;
  options.active_cursor = &active_cursor;
  delegate_->WriteLine(std::move(options));

  if (active_cursor.has_value() && original_active_cursor != nullptr) {
    *original_active_cursor = active_cursor.value();
  }
}

void FramedOutputProducer::AddFirstLine(
    const OutputProducer::Options& options) {
  LineModifier default_modifier = options.active_cursor == nullptr
                                      ? LineModifier::RESET
                                      : LineModifier::BOLD;

  options.receiver->AddModifier(default_modifier);
  options.receiver->AddString(
      L"──" + (title_.empty() ? L"" : L" " + title_ + L" ") + L"─");
  if (position_in_parent_.has_value()) {
    options.receiver->AddString(L"(");
    options.receiver->AddModifier(LineModifier::CYAN);
    // Add 1 because that matches what the repetitions do. Humans typically
    // start counting from 1.
    options.receiver->AddString(
        std::to_wstring(1 + position_in_parent_.value()));
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

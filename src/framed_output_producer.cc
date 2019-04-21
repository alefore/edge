#include "src/framed_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/output_receiver.h"

namespace afc {
namespace editor {
size_t FramedOutputProducer::MinimumLines() {
  return 1 + delegate_->MinimumLines();
}

void AddFirstLine(const wstring& title,
                  const OutputProducer::Options& options) {
  LineModifier default_modifier = options.active_cursor == nullptr
                                      ? LineModifier::RESET
                                      : LineModifier::BOLD;

  OutputReceiver& receiver = *options.lines[0];

  receiver.AddModifier(default_modifier);
  receiver.AddString(L"── " + title + L" ─");
  if (options.position_in_parent.has_value()) {
    receiver.AddString(L"(");
    receiver.AddModifier(LineModifier::CYAN);
    // Add 1 because that matches what the repetitions do. Humans typically
    // start counting from 1.
    receiver.AddString(std::to_wstring(1 + options.position_in_parent.value()));
    receiver.AddModifier(LineModifier::RESET);
    receiver.AddModifier(default_modifier);
    receiver.AddString(L")");
  }

  receiver.AddString(std::wstring(
      options.lines[0]->width() - options.lines[0]->column(), L'─'));
  receiver.AddModifier(LineModifier::RESET);
}

void FramedOutputProducer::Produce(Options options) {
  AddFirstLine(title_, options);
  options.lines.erase(options.lines.begin());

  const auto original_active_cursor = options.active_cursor;
  std::optional<LineColumn> active_cursor;
  options.active_cursor = &active_cursor;
  delegate_->Produce(std::move(options));
  if (active_cursor.has_value() && original_active_cursor != nullptr) {
    *original_active_cursor = LineColumn(active_cursor.value().line + 1,
                                         active_cursor.value().column);
  }
}

}  // namespace editor
}  // namespace afc

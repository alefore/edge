#include "src/framed_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/output_receiver.h"

namespace afc {
namespace editor {
void FramedOutputProducer::Produce(Options options) {
  options.lines[0]->AddString(L"───" + title_);
  if (options.lines[0]->column() + 2 < options.lines[0]->width()) {
    options.lines[0]->AddString(L"  ");
    options.lines[0]->AddString(std::wstring(
        options.lines[0]->width() - 2 - options.lines[0]->column(), L'─'));
  }
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

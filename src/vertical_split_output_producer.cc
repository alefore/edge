#include "src/vertical_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/delegating_output_receiver.h"
#include "src/output_receiver.h"

namespace afc {
namespace editor {
class SplitOutputReceiver : public DelegatingOutputReceiver {
 public:
  SplitOutputReceiver(OutputReceiver* delegate, ColumnNumber start,
                      std::optional<ColumnNumberDelta> width)
      : DelegatingOutputReceiver(delegate),
        skips_(start.ToDelta()),
        width_(
            width.has_value()
                ? min(width.value(), DelegatingOutputReceiver::width() - skips_)
                : DelegatingOutputReceiver::width() - skips_) {
    SetTabsStart(ColumnNumber(0));
  }

  void AddCharacter(wchar_t character) override {
    if (column() < ColumnNumber(0) + width()) {
      DelegatingOutputReceiver::AddCharacter(character);
    }
  }

  void AddString(const wstring& str) override {
    for (auto& character : str) {
      if (column() < ColumnNumber(0) + width()) {
        DelegatingOutputReceiver::AddCharacter(character);
      } else {
        return;
      }
    }
  }

  void SetTabsStart(ColumnNumber columns) override {
    DelegatingOutputReceiver::SetTabsStart(columns + skips_);
  }

  ColumnNumber column() override {
    return DelegatingOutputReceiver::column() - skips_;
  }
  ColumnNumberDelta width() override { return width_; }

 private:
  const ColumnNumberDelta skips_;
  const ColumnNumberDelta width_;
};

void VerticalSplitOutputProducer::WriteLine(Options options) {
  ColumnNumber initial_column;
  for (size_t i = 0; i < columns_.size(); i++) {
    if (options.receiver->column() < initial_column) {
      // TODO: Consider adding an 'advance N spaces' function?
      options.receiver->AddString(ColumnNumberDelta::PaddingString(
          initial_column - options.receiver->column(), L' '));
    }
    options.receiver->AddModifier(LineModifier::RESET);

    Options child_options;
    child_options.receiver = std::make_unique<SplitOutputReceiver>(
        options.receiver.get(), initial_column, columns_[i].width);

    std::optional<ColumnNumber> active_cursor;
    if (options.active_cursor != nullptr && i == index_active_) {
      child_options.active_cursor = &active_cursor;
    }
    columns_[i].producer->WriteLine(std::move(child_options));
    if (active_cursor.has_value()) {
      *options.active_cursor = active_cursor.value() + initial_column.ToDelta();
    }
    if (!columns_[i].width.has_value()) {
      return;
    }
    initial_column += columns_[i].width.value();
  }
}

}  // namespace editor
}  // namespace afc

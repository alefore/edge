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
  SplitOutputReceiver(OutputReceiver* delegate, size_t start,
                      std::optional<size_t> width)
      : DelegatingOutputReceiver(delegate),
        start_(start),
        width_(
            width.has_value()
                ? min(width.value(), DelegatingOutputReceiver::width() - start_)
                : DelegatingOutputReceiver::width() - start_) {
    SetTabsStart(0);
  }

  ~SplitOutputReceiver() { AddModifier(LineModifier::RESET); }

  void AddCharacter(wchar_t character) override {
    if (column() < width()) {
      DelegatingOutputReceiver::AddCharacter(character);
    }
  }

  void AddString(const wstring& str) override {
    for (auto& character : str) {
      if (column() < width()) {
        DelegatingOutputReceiver::AddCharacter(character);
      } else {
        return;
      }
    }
  }

  void SetTabsStart(size_t columns) override {
    DelegatingOutputReceiver::SetTabsStart(start_ + columns);
  }

  size_t column() override {
    return DelegatingOutputReceiver::column() - start_;
  }
  size_t width() override { return width_; }

 private:
  const size_t start_;
  const size_t width_;
};

void VerticalSplitOutputProducer::WriteLine(Options options) {
  size_t initial_column = 0;
  for (size_t i = 0; i < columns_.size(); i++) {
    if (options.receiver->column() < initial_column) {
      // TODO: Consider adding an 'advance N spaces' function?
      options.receiver->AddString(
          wstring(initial_column - options.receiver->column(), L' '));
    }

    Options child_options;
    child_options.receiver = std::make_unique<SplitOutputReceiver>(
        options.receiver.get(), initial_column, columns_[i].width);

    std::optional<size_t> active_cursor;
    if (options.active_cursor != nullptr && i == index_active_) {
      child_options.active_cursor = &active_cursor;
    }
    columns_[i].producer->WriteLine(std::move(child_options));
    if (active_cursor.has_value()) {
      *options.active_cursor = active_cursor.value() + initial_column;
    }
    if (!columns_[i].width.has_value()) {
      return;
    }
    initial_column += columns_[i].width.value();
  }
}

}  // namespace editor
}  // namespace afc

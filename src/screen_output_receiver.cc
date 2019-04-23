#include "src/screen_output_receiver.h"

#include <glog/logging.h>

#include <cmath>

#include "src/output_receiver.h"

namespace afc {
namespace editor {
namespace {
class Receiver : public OutputReceiver {
 public:
  Receiver(Screen* screen) : screen_(screen) {}
  ~Receiver() {
    AddModifier(LineModifier::RESET);
    if (column() < width()) {
      VLOG(6) << "Adding newline characters.";
      AddString(L"\n");
    }
  }

  void AddCharacter(wchar_t c) override {
    if (column_write_ >= screen_->columns()) return;
    if (c == L'\t') {
      // Nothing.
    } else if (iswprint(c) || c == L'\t' || c == L'\r' || c == L'\n') {
      screen_->WriteString(wstring(1, c));
    } else if (wcwidth(c) <= 0) {
      // Nothing.
    } else {
      screen_->WriteString(wstring(wcwidth(c), L' '));
    }
    RegisterChar(c);
  }
  void AddString(const wstring& str) override {
    for (auto& c : str) {
      AddCharacter(c);
    }
  }
  void AddModifier(LineModifier modifier) override {
    screen_->SetModifier(modifier);
  }

  void SetTabsStart(size_t columns) override { tabs_start_ = columns % 8; }

  size_t column() override { return column_write_; }

  size_t width() override { return screen_->columns(); }

 private:
  void RegisterChar(wchar_t c) {
    switch (c) {
      case L'\n':
        column_write_ = screen_->columns();
        break;
      case L'\t': {
        CHECK_GE(column_write_, tabs_start_);
        size_t new_value =
            tabs_start_ +
            8 * static_cast<size_t>(
                    1 + floor(static_cast<double>(column_write_ - tabs_start_) /
                              8.0));
        new_value = std::min(new_value, screen_->columns());
        CHECK_GT(new_value, column_write_);
        CHECK_LE(new_value - column_write_, 8u);
        screen_->WriteString(wstring(new_value - column_write_, ' '));
        column_write_ = new_value;
      } break;
      case L'​':
        break;
      default:
        column_write_ += wcwidth(c);
    }
    column_write_ = std::min(column_write_, screen_->columns());
  }

  Screen* const screen_;
  size_t column_write_ = 0;
  size_t tabs_start_ = 0;
};
}  // namespace

std::unique_ptr<OutputReceiver> NewScreenOutputReceiver(Screen* screen) {
  return std::make_unique<Receiver>(screen);
}

}  // namespace editor
}  // namespace afc

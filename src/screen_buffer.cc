#include <memory>

#include <glog/logging.h>

#include "screen.h"
#include "screen_buffer.h"
#include "server.h"
#include "vm/public/environment.h"
#include "vm/public/value.h"
#include "wstring.h"

namespace afc {
namespace editor {

using vm::Environment;
using vm::ObjectType;
using vm::Value;
using vm::VMType;

namespace {
class ScreenBuffer : public Screen {
 public:
  ScreenBuffer(std::shared_ptr<Screen> delegate)
      : delegate_(std::move(delegate)) {}

  ~ScreenBuffer() { Flush(); }

  void Flush() override {
    for (auto& call : calls_) {
      call(delegate_.get());
    }
    calls_.clear();
    delegate_->Flush();
  }

  void HardRefresh() override {
    calls_.push_back([](Screen* s) { s->HardRefresh(); });
  }
  void Refresh() override {
    calls_.push_back([](Screen* s) { s->Refresh(); });
  }
  void Clear() override {
    calls_.push_back([](Screen* s) { s->Clear(); });
  }
  void SetCursorVisibility(CursorVisibility cursor_visibility) override {
    calls_.push_back(
        [cursor_visibility](Screen* s) {
          s->SetCursorVisibility(cursor_visibility);
        });
  }

  void Move(size_t y, size_t x) override {
    calls_.push_back([y, x](Screen* s) { s->Move(y, x); });
  }

  void WriteString(const wstring& str) override {
    calls_.push_back([str](Screen* s) { s->WriteString(str); });
  }

  void SetModifier(Line::Modifier modifier) override {
    calls_.push_back([modifier](Screen* s) { s->SetModifier(modifier); });
  }

  size_t columns() const { return delegate_->columns(); }
  size_t lines() const { return delegate_->lines(); }

 private:
  const std::shared_ptr<Screen> delegate_;
  std::vector<std::function<void(Screen*)>> calls_;
};
}  // namespace

std::unique_ptr<Screen> NewScreenBuffer(std::shared_ptr<Screen> delegate) {
  CHECK(delegate != nullptr);
  return std::unique_ptr<Screen>(new ScreenBuffer(std::move(delegate)));
}

}  // namespace editor
}  // namespace afc
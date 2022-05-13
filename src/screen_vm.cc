#include "src/screen_vm.h"

#include <glog/logging.h>

#include <memory>

#include "src/char_buffer.h"
#include "src/language/wstring.h"
#include "src/line_column_vm.h"
#include "src/screen.h"
#include "src/server.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"

namespace afc {
namespace gc = language::gc;

namespace vm {
template <>
struct VMTypeMapper<editor::Screen*> {
  static editor::Screen* get(Value& value) {
    CHECK_EQ(value.type, vmtype);
    return static_cast<editor::Screen*>(value.user_value.get());
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::Screen*>::vmtype =
    VMType::ObjectType(L"Screen");
}  // namespace vm
namespace editor {

using infrastructure::Path;
using language::MakeNonNullUnique;
using language::NonNull;
using language::ToByteString;
using vm::Environment;
using vm::ObjectType;
using vm::Value;
using vm::VMType;

namespace {
class ScreenVm : public Screen {
 public:
  ScreenVm(int fd) : fd_(fd) {}

  ~ScreenVm() override {
    LOG(INFO) << "Sending terminate command to remote screen: fd: " << fd_;
    buffer_ += "set_terminate(0);";
    Write();
  }

  void Flush() override {
    buffer_ += "screen.Flush();";
    Write();
  }

  void HardRefresh() override { buffer_ += "screen.HardRefresh();"; }

  void Refresh() override { buffer_ += "screen.Refresh();"; }

  void Clear() override { buffer_ += "screen.Clear();"; }

  void SetCursorVisibility(CursorVisibility cursor_visibility) override {
    buffer_ += "screen.SetCursorVisibility(\"" +
               CursorVisibilityToString(cursor_visibility) + "\");";
  }

  void Move(LineColumn position) override {
    buffer_ += "screen.Move(LineColumn(" + std::to_string(position.line.line) +
               ", " + std::to_string(position.column.column) + "));";
  }

  void WriteString(const NonNull<std::shared_ptr<LazyString>>& str) override {
    // TODO(easy, 2022-04-27): Avoid call of ToString.
    buffer_ += "screen.WriteString(\"" +
               ToByteString(vm::CppEscapeString(str->ToString())) + "\");";
  }

  void SetModifier(LineModifier modifier) override {
    buffer_ += "screen.SetModifier(\"" + ModifierToString(modifier) + "\");";
  }

  LineColumnDelta size() const override { return size_; }
  void set_size(LineColumnDelta size) {
    DVLOG(5) << "Received new size: " << size;
    size_ = size;
  }

 private:
  void Write() {
    buffer_ += "\n";
    LOG(INFO) << "Sending command: " << buffer_;
    int result = write(fd_, buffer_.c_str(), buffer_.size());
    if (result != static_cast<int>(buffer_.size())) {
      LOG(INFO) << "Remote screen update failed!";
    }
    buffer_.clear();
  }

  string buffer_;
  const int fd_;
  LineColumnDelta size_ =
      LineColumnDelta(LineNumberDelta(25), ColumnNumberDelta(80));
};
}  // namespace

void RegisterScreenType(gc::Pool& pool, Environment& environment) {
  using vm::EvaluationOutput;
  using vm::Trampoline;

  auto screen_type = MakeNonNullUnique<ObjectType>(L"Screen");

  // Constructors.
  environment.Define(
      L"RemoteScreen",
      Value::NewFunction(
          pool, {screen_type->type(), VMType::String()},
          [&pool](std::vector<gc::Root<Value>> args,
                  Trampoline&) -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 1u);
            CHECK(args[0].value()->IsString());
            auto path = Path::FromString(args[0].value()->str);
            if (path.IsError()) {
              LOG(ERROR) << "RemoteScreen: " << path.error();
              return futures::Past(path.error());
            }
            auto output = MaybeConnectToServer(path.value());
            if (output.IsError()) {
              LOG(ERROR) << "RemoteScreen: MaybeConnectToServer: "
                         << output.error();
              return futures::Past(output.error());
            }
            return futures::Past(EvaluationOutput::Return(Value::NewObject(
                pool, L"Screen", std::make_shared<ScreenVm>(output.value()))));
          }));

  // Methods for Screen.
  screen_type->AddField(L"Flush", vm::NewCallback(pool, [](Screen* screen) {
                          CHECK(screen != nullptr);
                          screen->Flush();
                        }));

  screen_type->AddField(L"HardRefresh",
                        vm::NewCallback(pool, [](Screen* screen) {
                          CHECK(screen != nullptr);
                          screen->HardRefresh();
                        }));

  screen_type->AddField(L"Refresh", vm::NewCallback(pool, [](Screen* screen) {
                          CHECK(screen != nullptr);
                          screen->Refresh();
                        }));

  screen_type->AddField(L"Clear", vm::NewCallback(pool, [](Screen* screen) {
                          CHECK(screen != nullptr);
                          screen->Clear();
                        }));

  screen_type->AddField(
      L"SetCursorVisibility",
      vm::NewCallback(pool, [](Screen* screen, wstring cursor_visibility) {
        CHECK(screen != nullptr);
        screen->SetCursorVisibility(Screen::CursorVisibilityFromString(
            ToByteString(cursor_visibility)));
      }));

  screen_type->AddField(
      L"Move", vm::NewCallback(pool, [](Screen* screen, LineColumn position) {
        CHECK(screen != nullptr);
        screen->Move(position);
      }));

  screen_type->AddField(L"WriteString",
                        vm::NewCallback(pool, [](Screen* screen, wstring str) {
                          using ::operator<<;
                          CHECK(screen != nullptr);
                          DVLOG(5) << "Writing string: " << str;
                          screen->WriteString(NewLazyString(std::move(str)));
                        }));

  screen_type->AddField(
      L"SetModifier", vm::NewCallback(pool, [](Screen* screen, wstring str) {
        CHECK(screen != nullptr);
        screen->SetModifier(ModifierFromString(ToByteString(str)));
      }));

  screen_type->AddField(
      L"set_size",
      vm::NewCallback(pool, [](Screen* screen, LineColumnDelta size) {
        ScreenVm* screen_vm = dynamic_cast<ScreenVm*>(screen);
        CHECK(screen != nullptr);
        screen_vm->set_size(size);
      }));

  screen_type->AddField(L"size", vm::NewCallback(pool, [](Screen* screen) {
                          CHECK(screen != nullptr);
                          return screen->size();
                        }));

  environment.DefineType(L"Screen", std::move(screen_type));
}

std::unique_ptr<Screen> NewScreenVm(int fd) {
  return std::make_unique<ScreenVm>(fd);
}

const VMType& GetScreenVmType() {
  static const VMType* const output = new VMType(VMType::ObjectType(L"Screen"));
  return *output;
}

}  // namespace editor
}  // namespace afc

#include "screen_vm.h"

#include <memory>

#include <glog/logging.h>

#include "screen.h"
#include "server.h"
#include "vm/public/callbacks.h"
#include "vm/public/environment.h"
#include "vm/public/value.h"
#include "wstring.h"

namespace afc {
namespace vm {
template <>
struct VMTypeMapper<editor::Screen*> {
  static editor::Screen* get(Value* value) {
    return static_cast<editor::Screen*>(value->user_value.get());
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::Screen*>::vmtype =
    VMType::ObjectType(L"Screen");
}  // namespace vm
namespace editor {

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

  void Move(size_t y, size_t x) override {
    buffer_ +=
        "screen.Move(" + std::to_string(y) + ", " + std::to_string(x) + ");";
  }

  void WriteString(const wstring& str) override {
    buffer_ +=
        "screen.WriteString(\"" + ToByteString(CppEscapeString(str)) + "\");";
  }

  void SetModifier(LineModifier modifier) override {
    buffer_ += "screen.SetModifier(\"" + ModifierToString(modifier) + "\");";
  }

  size_t columns() const { return columns_; }
  size_t lines() const { return lines_; }
  void set_size(size_t columns, size_t lines) {
    DVLOG(5) << "Received new size: " << columns << " x " << lines;
    columns_ = columns;
    lines_ = lines;
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
  size_t columns_ = 80;
  size_t lines_ = 25;
};
}  // namespace

void RegisterScreenType(Environment* environment) {
  auto screen_type = std::make_unique<ObjectType>(L"Screen");

  // Constructors.
  environment->Define(
      L"RemoteScreen",
      Value::NewFunction(
          {VMType::ObjectType(screen_type.get()), VMType::String()},
          [](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), 1u);
            CHECK_EQ(args[0]->type, VMType::VM_STRING);
            wstring error;
            int fd = MaybeConnectToServer(ToByteString(args[0]->str), &error);
            return Value::NewObject(L"Screen", std::make_shared<ScreenVm>(fd));
          }));

  // Methods for Screen.
  screen_type->AddField(
      L"Flush",
      vm::NewCallback(std::function<void(Screen*)>([](Screen* screen) {
        CHECK(screen != nullptr);
        screen->Flush();
      })));

  screen_type->AddField(
      L"HardRefresh",
      vm::NewCallback(std::function<void(Screen*)>([](Screen* screen) {
        CHECK(screen != nullptr);
        screen->HardRefresh();
      })));

  screen_type->AddField(
      L"Refresh",
      vm::NewCallback(std::function<void(Screen*)>([](Screen* screen) {
        CHECK(screen != nullptr);
        screen->Refresh();
      })));

  screen_type->AddField(
      L"Clear",
      vm::NewCallback(std::function<void(Screen*)>([](Screen* screen) {
        CHECK(screen != nullptr);
        screen->Clear();
      })));

  screen_type->AddField(
      L"SetCursorVisibility",
      vm::NewCallback(std::function<void(Screen*, wstring)>(
          [](Screen* screen, wstring cursor_visibility) {
            CHECK(screen != nullptr);
            screen->SetCursorVisibility(Screen::CursorVisibilityFromString(
                ToByteString(cursor_visibility)));
          })));

  screen_type->AddField(L"Move",
                        vm::NewCallback(std::function<void(Screen*, int, int)>(
                            [](Screen* screen, int y, int x) {
                              CHECK(screen != nullptr);
                              screen->Move(y, x);
                            })));

  screen_type->AddField(L"WriteString",
                        vm::NewCallback(std::function<void(Screen*, wstring)>(
                            [](Screen* screen, wstring str) {
                              CHECK(screen != nullptr);
                              DVLOG(5) << "Writing string: " << str;
                              screen->WriteString(str);
                            })));

  screen_type->AddField(
      L"SetModifier",
      vm::NewCallback(std::function<void(Screen*, wstring)>(
          [](Screen* screen, wstring str) {
            CHECK(screen != nullptr);
            screen->SetModifier(ModifierFromString(ToByteString(str)));
          })));

  screen_type->AddField(
      L"set_size", vm::NewCallback(std::function<void(Screen*, int, int)>(
                       [](Screen* screen, int columns, int lines) {
                         ScreenVm* screen_vm = dynamic_cast<ScreenVm*>(screen);
                         CHECK(screen != nullptr);
                         screen_vm->set_size(columns, lines);
                       })));

  screen_type->AddField(
      L"columns",
      vm::NewCallback(std::function<int(Screen*)>([](Screen* screen) {
        CHECK(screen != nullptr);
        return screen->columns();
      })));

  screen_type->AddField(
      L"lines", vm::NewCallback(std::function<int(Screen*)>([](Screen* screen) {
        CHECK(screen != nullptr);
        return screen->lines();
      })));

  environment->DefineType(L"Screen", std::move(screen_type));
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

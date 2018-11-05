#include "screen_vm.h"

#include <memory>

#include <glog/logging.h>

#include "screen.h"
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
class ScreenVm : public Screen {
 public:
  ScreenVm(int fd) : fd_(fd) {}

  ~ScreenVm() override {
    LOG(INFO) << "Sending terminate command to remote screen: fd: " << fd_;
    buffer_ += "set_terminate(true);";
    Write();
  }

  void Flush() override {
    buffer_ += "screen.Flush();";
    Write();
  }

  void HardRefresh() override {
    buffer_ += "screen.HardRefresh();";
  }

  void Refresh() override {
    buffer_ += "screen.Refresh();";
  }

  void Clear() override {
    buffer_ += "screen.Clear();";
  }

  void SetCursorVisibility(CursorVisibility cursor_visibility) override {
    buffer_ += "screen.SetCursorVisibility(\""
        + CursorVisibilityToString(cursor_visibility) + "\");";
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
  unique_ptr<ObjectType> screen_type(new ObjectType(L"Screen"));

  // Constructors.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));

    // Address.
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1u);
          CHECK_EQ(args[0]->type, VMType::VM_STRING);
          wstring error;
          int fd = MaybeConnectToServer(ToByteString(args[0]->str), &error);
          return Value::NewObject(L"Screen", std::make_shared<ScreenVm>(fd));
        };
    environment->Define(L"RemoteScreen", std::move(callback));
  }

  // Methods for Screen.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto screen = static_cast<Screen*>(args[0]->user_value.get());
          CHECK(screen != nullptr);

          screen->Flush();
          return Value::NewVoid();
        };
    screen_type->AddField(L"Flush", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto screen = static_cast<Screen*>(args[0]->user_value.get());
          CHECK(screen != nullptr);

          screen->HardRefresh();
          return Value::NewVoid();
        };
    screen_type->AddField(L"HardRefresh", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto screen = static_cast<Screen*>(args[0]->user_value.get());
          CHECK(screen != nullptr);

          screen->Refresh();
          return Value::NewVoid();
        };
    screen_type->AddField(L"Refresh", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto screen = static_cast<Screen*>(args[0]->user_value.get());
          CHECK(screen != nullptr);

          screen->Clear();
          return Value::NewVoid();
        };
    screen_type->AddField(L"Clear", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));
    callback->type.type_arguments.push_back(VMType::VM_STRING);

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 2u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto screen = static_cast<Screen*>(args[0]->user_value.get());
          CHECK(screen != nullptr);

          CHECK_EQ(args[1]->type, VMType::VM_STRING);

          screen->SetCursorVisibility(
              Screen::CursorVisibilityFromString(ToByteString(args[1]->str)));
          return Value::NewVoid();
        };
    screen_type->AddField(L"SetCursorVisibility", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 3u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto screen = static_cast<Screen*>(args[0]->user_value.get());
          CHECK(screen != nullptr);

          CHECK_EQ(args[1]->type, VMType::VM_INTEGER);
          CHECK_EQ(args[2]->type, VMType::VM_INTEGER);

          screen->Move(args[1]->integer, args[2]->integer);
          return Value::NewVoid();
        };
    screen_type->AddField(L"Move", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 2u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto screen = static_cast<Screen*>(args[0]->user_value.get());
          CHECK(screen != nullptr);

          CHECK_EQ(args[1]->type, VMType::VM_STRING);

          DVLOG(5) << "Writing string: " << args[1]->str;
          screen->WriteString(args[1]->str);
          return Value::NewVoid();
        };
    screen_type->AddField(L"WriteString", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));
    callback->type.type_arguments.push_back(VMType::VM_STRING);

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 2u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto screen = static_cast<Screen*>(args[0]->user_value.get());
          CHECK(screen != nullptr);

          CHECK_EQ(args[1]->type, VMType::VM_STRING);

          screen->SetModifier(ModifierFromString(ToByteString(args[1]->str)));
          return Value::NewVoid();
        };
    screen_type->AddField(L"SetModifier", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 3u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          CHECK_EQ(args[1]->type, VMType::VM_INTEGER);
          CHECK_EQ(args[2]->type, VMType::VM_INTEGER);
          auto screen = static_cast<ScreenVm*>(args[0]->user_value.get());
          CHECK(screen != nullptr);

          screen->set_size(args[1]->integer, args[2]->integer);
          return Value::NewVoid();
        };
    screen_type->AddField(L"set_size", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto screen = static_cast<Screen*>(args[0]->user_value.get());
          CHECK(screen != nullptr);
          return Value::NewInteger(screen->columns());
        };
    screen_type->AddField(L"columns", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(
        VMType::ObjectType(screen_type.get()));

    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1u);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto screen = static_cast<Screen*>(args[0]->user_value.get());
          CHECK(screen != nullptr);
          return Value::NewInteger(screen->lines());
        };
    screen_type->AddField(L"lines", std::move(callback));
  }
  environment->DefineType(L"Screen", std::move(screen_type));
}

std::unique_ptr<Screen> NewScreenVm(int fd) {
  return std::unique_ptr<Screen>(new ScreenVm(fd));
}

}  // namespace editor
}  // namespace afc

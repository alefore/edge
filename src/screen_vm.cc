#include "src/screen_vm.h"

#include <glog/logging.h>

#include <memory>

#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/line_column_vm.h"
#include "src/screen.h"
#include "src/server.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"

namespace afc {
using infrastructure::FileDescriptor;
using language::Error;
using language::NonNull;
using language::Success;
using language::VisitPointer;

namespace gc = language::gc;

namespace vm {
template <>
const VMType VMTypeMapper<NonNull<std::shared_ptr<editor::Screen>>>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"Screen"));
}  // namespace vm
namespace editor {
using infrastructure::FileDescriptor;
using infrastructure::Path;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::ToByteString;
using vm::Environment;
using vm::ObjectType;
using vm::Value;
using vm::VMType;

namespace {
class ScreenVm : public Screen {
 public:
  ScreenVm(FileDescriptor fd) : fd_(fd) {}

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
    int result = write(fd_.read(), buffer_.c_str(), buffer_.size());
    if (result != static_cast<int>(buffer_.size())) {
      LOG(INFO) << "Remote screen update failed!";
    }
    buffer_.clear();
  }

  std::string buffer_;
  const FileDescriptor fd_;
  LineColumnDelta size_ =
      LineColumnDelta(LineNumberDelta(25), ColumnNumberDelta(80));
};
}  // namespace

void RegisterScreenType(EditorState& editor, Environment& environment) {
  using vm::EvaluationOutput;
  using vm::PurityType;
  using vm::Trampoline;
  using vm::VMTypeMapper;

  gc::Pool& pool = editor.gc_pool();

  auto screen_type = MakeNonNullUnique<ObjectType>(
      VMTypeMapper<NonNull<std::shared_ptr<Screen>>>::vmtype);

  // Constructors.
  environment.Define(
      L"RemoteScreen",
      Value::NewFunction(
          pool, PurityType::kUnknown, {screen_type->type(), VMType::String()},
          [&pool, &editor](std::vector<gc::Root<Value>> args, Trampoline&)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 1u);
            FUTURES_ASSIGN_OR_RETURN(
                Path path, Path::FromString(args[0].ptr()->get_string()));
            return editor.thread_pool()
                .Run([path] { return SyncConnectToServer(path); })
                .Transform([&pool](FileDescriptor fd) {
                  return futures::Past(
                      Success(EvaluationOutput::Return(Value::NewObject(
                          pool,
                          VMTypeMapper<
                              NonNull<std::shared_ptr<editor::Screen>>>::vmtype
                              .object_type,
                          MakeNonNullShared<ScreenVm>(fd)))));
                });
          }));

  // Methods for Screen.
  screen_type->AddField(
      L"Flush", vm::NewCallback(pool, PurityType::kUnknown,
                                [](NonNull<std::shared_ptr<Screen>> screen) {
                                  screen->Flush();
                                }));

  screen_type->AddField(
      L"HardRefresh",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](NonNull<std::shared_ptr<Screen>> screen) {
                        screen->HardRefresh();
                      }));

  screen_type->AddField(
      L"Refresh", vm::NewCallback(pool, PurityType::kUnknown,
                                  [](NonNull<std::shared_ptr<Screen>> screen) {
                                    screen->Refresh();
                                  }));

  screen_type->AddField(
      L"Clear", vm::NewCallback(pool, PurityType::kUnknown,
                                [](NonNull<std::shared_ptr<Screen>> screen) {
                                  screen->Clear();
                                }));

  screen_type->AddField(
      L"SetCursorVisibility",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](NonNull<std::shared_ptr<Screen>> screen,
                         std::wstring cursor_visibility) {
                        screen->SetCursorVisibility(
                            Screen::CursorVisibilityFromString(
                                ToByteString(cursor_visibility)));
                      }));

  screen_type->AddField(
      L"Move",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](NonNull<std::shared_ptr<Screen>> screen,
                         LineColumn position) { screen->Move(position); }));

  screen_type->AddField(
      L"WriteString",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](NonNull<std::shared_ptr<Screen>> screen, std::wstring str) {
            using ::operator<<;
            DVLOG(5) << "Writing string: " << str;
            screen->WriteString(NewLazyString(std::move(str)));
          }));

  screen_type->AddField(
      L"SetModifier",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](NonNull<std::shared_ptr<Screen>> screen, std::wstring str) {
            screen->SetModifier(ModifierFromString(ToByteString(str)));
          }));

  screen_type->AddField(
      L"set_size",
      Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Void(), screen_type->type(),
           VMTypeMapper<LineColumnDelta>::vmtype},
          [&pool](std::vector<gc::Root<Value>> args, Trampoline& trampoline) {
            CHECK_EQ(args.size(), 2ul);
            return futures::Past(VisitPointer(
                NonNull<std::shared_ptr<ScreenVm>>::DynamicCast(
                    VMTypeMapper<NonNull<std::shared_ptr<Screen>>>::get(
                        args[0].ptr().value())),
                [&args,
                 &trampoline](NonNull<std::shared_ptr<ScreenVm>> screen) {
                  screen->set_size(VMTypeMapper<LineColumnDelta>::get(
                      args[1].ptr().value()));
                  return Success(EvaluationOutput::Return(
                      Value::NewVoid(trampoline.pool())));
                },
                [] {
                  return Error(
                      L"Screen type does not support set_size method.");
                }));
          }));

  screen_type->AddField(
      L"size", vm::NewCallback(pool, PurityType::kReader,
                               [](NonNull<std::shared_ptr<Screen>> screen) {
                                 return screen->size();
                               }));

  environment.DefineType(std::move(screen_type));
}

std::unique_ptr<Screen> NewScreenVm(FileDescriptor fd) {
  return std::make_unique<ScreenVm>(fd);
}

const VMType& GetScreenVmType() {
  return vm::VMTypeMapper<NonNull<std::shared_ptr<editor::Screen>>>::vmtype;
}

}  // namespace editor
}  // namespace afc

#include "src/screen_vm.h"

#include <glog/logging.h>

#include <memory>

#include "src/editor.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column_vm.h"
#include "src/language/wstring.h"
#include "src/screen.h"
#include "src/server.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"

namespace afc {
using infrastructure::FileDescriptor;
using language::Error;
using language::NonNull;
using language::PossibleError;
using language::Success;
using language::ValueOrError;
using language::VisitPointer;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;
using language::text::LineColumn;
using language::text::LineColumnDelta;
using language::text::LineNumberDelta;

namespace gc = language::gc;

namespace vm {
template <>
const types::ObjectName
    VMTypeMapper<NonNull<std::shared_ptr<editor::Screen>>>::object_type_name =
        types::ObjectName(L"Screen");
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
    buffer_ += "screen.Move(LineColumn(" +
               ToByteString(to_wstring(position.line)) + ", " +
               ToByteString(to_wstring(position.column)) + "));";
  }

  void WriteString(const NonNull<std::shared_ptr<LazyString>>& str) override {
    buffer_ +=
        "screen.WriteString(" +
        ToByteString(vm::EscapedString::FromString(str).CppRepresentation()) +
        ");";
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

  // TODO(easy, 2022-06-06): Turn this into a LazyString? Serialize it at once?
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

  gc::Root<ObjectType> screen_type = ObjectType::New(
      pool, VMTypeMapper<NonNull<std::shared_ptr<Screen>>>::object_type_name);

  // Constructors.
  environment.Define(
      L"RemoteScreen",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [&editor](std::wstring str_path)
              -> futures::ValueOrError<NonNull<std::shared_ptr<Screen>>> {
            FUTURES_ASSIGN_OR_RETURN(Path path, Path::FromString(str_path));
            return editor.thread_pool()
                .Run([path] { return SyncConnectToServer(path); })
                .Transform([](FileDescriptor fd)
                               -> futures::ValueOrError<
                                   NonNull<std::shared_ptr<Screen>>> {
                  return futures::Past(MakeNonNullShared<ScreenVm>(fd));
                });
          }));

  // Methods for Screen.
  screen_type.ptr()->AddField(
      L"Flush", vm::NewCallback(pool, PurityType::kUnknown,
                                [](NonNull<std::shared_ptr<Screen>> screen) {
                                  screen->Flush();
                                })
                    .ptr());

  screen_type.ptr()->AddField(
      L"HardRefresh",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](NonNull<std::shared_ptr<Screen>> screen) {
                        screen->HardRefresh();
                      })
          .ptr());

  screen_type.ptr()->AddField(
      L"Refresh", vm::NewCallback(pool, PurityType::kUnknown,
                                  [](NonNull<std::shared_ptr<Screen>> screen) {
                                    screen->Refresh();
                                  })
                      .ptr());

  screen_type.ptr()->AddField(
      L"Clear", vm::NewCallback(pool, PurityType::kUnknown,
                                [](NonNull<std::shared_ptr<Screen>> screen) {
                                  screen->Clear();
                                })
                    .ptr());

  screen_type.ptr()->AddField(
      L"SetCursorVisibility",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](NonNull<std::shared_ptr<Screen>> screen,
                         std::wstring cursor_visibility) {
                        screen->SetCursorVisibility(
                            Screen::CursorVisibilityFromString(
                                ToByteString(cursor_visibility)));
                      })
          .ptr());

  screen_type.ptr()->AddField(
      L"Move",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](NonNull<std::shared_ptr<Screen>> screen,
                         LineColumn position) { screen->Move(position); })
          .ptr());

  screen_type.ptr()->AddField(
      L"WriteString",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](NonNull<std::shared_ptr<Screen>> screen, std::wstring str) {
            using ::operator<<;
            DVLOG(5) << "Writing string: " << str;
            screen->WriteString(NewLazyString(std::move(str)));
          })
          .ptr());

  screen_type.ptr()->AddField(
      L"SetModifier",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](NonNull<std::shared_ptr<Screen>> screen, std::wstring str) {
            screen->SetModifier(ModifierFromString(ToByteString(str)));
          })
          .ptr());

  screen_type.ptr()->AddField(
      L"set_size",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [&pool](NonNull<std::shared_ptr<Screen>> screen,
                  LineColumnDelta line_column_delta) {
            return futures::Past(VisitPointer(
                NonNull<std::shared_ptr<ScreenVm>>::DynamicCast(screen),
                [&pool, line_column_delta](
                    NonNull<std::shared_ptr<ScreenVm>> vm_screen)
                    -> PossibleError {
                  vm_screen->set_size(line_column_delta);
                  return language::EmptyValue();
                },
                []() -> PossibleError {
                  return Error(
                      L"Screen type does not support set_size method.");
                }));
          })
          .ptr());

  screen_type.ptr()->AddField(
      L"size", vm::NewCallback(pool, PurityType::kReader,
                               [](NonNull<std::shared_ptr<Screen>> screen) {
                                 return screen->size();
                               })
                   .ptr());

  environment.DefineType(screen_type.ptr());
}

std::unique_ptr<Screen> NewScreenVm(FileDescriptor fd) {
  return std::make_unique<ScreenVm>(fd);
}

const vm::types::ObjectName& GetScreenVmType() {
  return vm::VMTypeMapper<
      NonNull<std::shared_ptr<editor::Screen>>>::object_type_name;
}

}  // namespace editor
}  // namespace afc

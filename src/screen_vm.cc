#include "src/screen_vm.h"

#include <glog/logging.h>

#include <memory>

#include "src/editor.h"
#include "src/infrastructure/dirname_vm.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/screen/screen.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column_vm.h"
#include "src/language/wstring.h"
#include "src/server.h"
#include "src/vm/callbacks.h"
#include "src/vm/environment.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

using afc::infrastructure::FileDescriptor;
using afc::infrastructure::Path;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::ModifierFromString;
using afc::infrastructure::screen::Screen;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ToByteString;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineColumnDelta;
using afc::language::text::LineNumberDelta;
using afc::vm::Environment;
using afc::vm::Identifier;
using afc::vm::kPurityTypeReader;
using afc::vm::kPurityTypeUnknown;
using afc::vm::ObjectType;
using afc::vm::Value;
using afc::vm::VMType;

namespace afc {
namespace vm {
template <>
const types::ObjectName
    VMTypeMapper<NonNull<std::shared_ptr<Screen>>>::object_type_name =
        types::ObjectName(L"Screen");
}  // namespace vm
namespace editor {

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

  void WriteString(const LazyString& str) override {
    buffer_ +=
        "screen.WriteString(" +
        ToByteString(
            vm::EscapedString::FromString(str).CppRepresentation().ToString()) +
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
  using vm::PurityType;
  using vm::Trampoline;
  using vm::VMTypeMapper;

  gc::Pool& pool = editor.gc_pool();

  gc::Root<ObjectType> screen_type = ObjectType::New(
      pool, VMTypeMapper<NonNull<std::shared_ptr<Screen>>>::object_type_name);

  // Constructors.
  environment.Define(
      Identifier(L"RemoteScreen"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [&editor](Path path)
              -> futures::ValueOrError<NonNull<std::shared_ptr<Screen>>> {
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
      Identifier(L"Flush"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<Screen>> screen) { screen->Flush(); })
          .ptr());

  screen_type.ptr()->AddField(
      Identifier(L"HardRefresh"),
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](NonNull<std::shared_ptr<Screen>> screen) {
                        screen->HardRefresh();
                      })
          .ptr());

  screen_type.ptr()->AddField(
      Identifier(L"Refresh"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<Screen>> screen) { screen->Refresh(); })
          .ptr());

  screen_type.ptr()->AddField(
      Identifier(L"Clear"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<Screen>> screen) { screen->Clear(); })
          .ptr());

  screen_type.ptr()->AddField(
      Identifier(L"SetCursorVisibility"),
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](NonNull<std::shared_ptr<Screen>> screen,
                         std::wstring cursor_visibility) {
                        screen->SetCursorVisibility(
                            Screen::CursorVisibilityFromString(
                                ToByteString(cursor_visibility)));
                      })
          .ptr());

  screen_type.ptr()->AddField(
      Identifier(L"Move"),
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](NonNull<std::shared_ptr<Screen>> screen,
                         LineColumn position) { screen->Move(position); })
          .ptr());

  screen_type.ptr()->AddField(
      Identifier(L"WriteString"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<Screen>> screen, std::wstring str) {
            using ::operator<<;
            DVLOG(5) << "Writing string: " << str;
            screen->WriteString(LazyString{std::move(str)});
          })
          .ptr());

  screen_type.ptr()->AddField(
      Identifier(L"SetModifier"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<Screen>> screen, std::wstring str) {
            screen->SetModifier(ModifierFromString(ToByteString(str)));
          })
          .ptr());

  screen_type.ptr()->AddField(
      Identifier(L"set_size"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<Screen>> screen,
             LineColumnDelta line_column_delta) {
            return futures::Past(VisitPointer(
                NonNull<std::shared_ptr<ScreenVm>>::DynamicCast(screen),
                [line_column_delta](
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
      Identifier(L"size"),
      vm::NewCallback(pool, kPurityTypeReader,
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
  return vm::VMTypeMapper<NonNull<std::shared_ptr<Screen>>>::object_type_name;
}

}  // namespace editor
}  // namespace afc

#include "src/buffer.h"
#include "src/editor.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/status.h"
#include "src/tests/tests.h"

using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;

namespace afc::editor {
namespace {
namespace gc = language::gc;
namespace audio = infrastructure::audio;

const bool prompt_tests_registration = tests::Register(
    L"StatusPrompt",
    {{.name = L"InsertError",
      .callback =
          [] {
            NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
            Status status(editor->audio_player());
            gc::Root<OpenBuffer> prompt = NewBufferForTests(editor.value());
            status.set_prompt(NewLazyString(L">"), prompt);
            status.InsertError(Error(L"Foobar"));
            CHECK(status.text().ToString() == L">");
            CHECK(&status.prompt_buffer().value().ptr().value() ==
                  &prompt.ptr().value());
          }},
     {.name = L"SetExpiringInformationText", .callback = [] {
        NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
        Status status(editor->audio_player());
        gc::Root<OpenBuffer> prompt = NewBufferForTests(editor.value());
        status.set_prompt(NewLazyString(L">"), prompt);
        auto value = status.SetExpiringInformationText(Line(L"Foobar"));
        CHECK(status.text().ToString() == L">");
        value = nullptr;
        CHECK(status.text().ToString() == L">");
        CHECK(&status.prompt_buffer().value().ptr().value() ==
              &prompt.ptr().value());
      }}});
}  // namespace
}  // namespace afc::editor

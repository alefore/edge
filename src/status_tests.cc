#include "src/buffer.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/status.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
namespace gc = language::gc;
namespace audio = infrastructure::audio;

using language::Error;
using language::NonNull;
using language::lazy_string::NewLazyString;

const bool prompt_tests_registration = tests::Register(
    L"StatusPrompt",
    {{.name = L"InsertError",
      .callback =
          [] {
            NonNull<std::unique_ptr<audio::Player>> audio_player =
                audio::NewNullPlayer();
            Status status(audio_player.value());
            gc::Root<OpenBuffer> prompt = NewBufferForTests();
            status.set_prompt(NewLazyString(L">"), prompt);
            status.InsertError(Error(L"Foobar"));
            CHECK(status.text() == L">");
            CHECK(&status.prompt_buffer().value().ptr().value() ==
                  &prompt.ptr().value());
          }},
     {.name = L"SetExpiringInformationText", .callback = [] {
        NonNull<std::unique_ptr<audio::Player>> audio_player =
            audio::NewNullPlayer();
        Status status(audio_player.value());
        gc::Root<OpenBuffer> prompt = NewBufferForTests();
        status.set_prompt(NewLazyString(L">"), prompt);
        status.SetExpiringInformationText(NewLazyString(L"Foobar"));
        CHECK(status.text() == L">");
        CHECK(&status.prompt_buffer().value().ptr().value() ==
              &prompt.ptr().value());
      }}});
}  // namespace
}  // namespace afc::editor

#include "src/buffer.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/status.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
using language::NonNull;
namespace gc = language::gc;
namespace audio = infrastructure::audio;

const bool prompt_tests_registration = tests::Register(
    L"StatusPrompt",
    {{.name = L"SetWarningText",
      .callback =
          [] {
            NonNull<std::unique_ptr<audio::Player>> audio_player =
                audio::NewNullPlayer();
            Status status(audio_player.value());
            gc::Root<OpenBuffer> prompt = NewBufferForTests();
            status.set_prompt(L">", prompt);
            status.SetWarningText(L"Foobar");
            CHECK(status.text() == L">");
            CHECK(&status.prompt_buffer().value().ptr().value() ==
                  &prompt.ptr().value());
          }},
     {.name = L"SetExpiringInformationText", .callback = [] {
        NonNull<std::unique_ptr<audio::Player>> audio_player =
            audio::NewNullPlayer();
        Status status(audio_player.value());
        gc::Root<OpenBuffer> prompt = NewBufferForTests();
        status.set_prompt(L">", prompt);
        status.SetExpiringInformationText(L"Foobar");
        CHECK(status.text() == L">");
        CHECK(&status.prompt_buffer().value().ptr().value() ==
              &prompt.ptr().value());
      }}});
}  // namespace
}  // namespace afc::editor

#include <memory>

#include "src/buffer.h"
#include "src/buffer_name.h"
#include "src/buffer_registry.h"
#include "src/editor.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

using namespace afc::language;

namespace afc::editor {
namespace {
const bool maybe_add_tests_registration = tests::Register(
    L"BufferRegistry_MaybeAdd",
    {{.name = L"Reentrant", .callback = [] {
        NonNull<std::unique_ptr<EditorState>> editor =
            EditorForTests(std::nullopt);
        BufferRegistry registry(
            [](const OpenBuffer&, const OpenBuffer&) -> std::weak_ordering {
              LOG(FATAL) << "Unexpected";
              std::unreachable();
            },
            [](const OpenBuffer&) { return false; }, [](OpenBuffer&) {});
        registry.MaybeAdd(PasteBuffer{}, [&] {
          // This is the key part: no deadlock should happen:
          auto buffers_output = registry.buffers();
          // To avoid optimizations, we validate something with buffers_output:
          CHECK_EQ(buffers_output.size(), 1ul);
          return OpenBuffer::New(OpenBuffer::Options{.editor = editor.value(),
                                                     .name = PasteBuffer{}});
        });
      }}});
}  // namespace
}  // namespace afc::editor

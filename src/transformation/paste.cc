#include "src/transformation/paste.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/editor.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/gc.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::VisitOptional;
using afc::language::lazy_string::LazyString;

namespace afc::editor::transformation {

std::wstring Paste::Serialize() const {
  return LazyString{L"Paste()"}.ToString();
}

futures::Value<CompositeTransformation::Output> Paste::Apply(
    CompositeTransformation::Input input) const {
  return VisitOptional(
      [&](gc::Root<OpenBuffer> paste_buffer) {
        DVLOG(6) << "Inserting: "
                 << paste_buffer.ptr()->contents().snapshot().ToLazyString();
        return futures::Past(
            CompositeTransformation::Output{transformation::Insert{
                .contents_to_insert = paste_buffer.ptr()->contents().snapshot(),
                .modifiers = {.insertion = input.modifiers.insertion,
                              .repetitions = input.modifiers.repetitions},
                .modifiers_set =
                    std::invoke([&input] -> std::optional<LineModifierSet> {
                      switch (input.mode) {
                        case transformation::Input::Mode::kFinal:
                          return std::nullopt;
                        case transformation::Input::Mode::kPreview:
                          return LineModifierSet{LineModifier::kCyan};
                      }
                      LOG(FATAL) << "Invalid input mode.";
                      return std::nullopt;
                    })}});
      },
      [] {
        VLOG(5) << "Paste buffer not found.";
        return futures::Past(CompositeTransformation::Output{});
      },
      input.editor.buffer_registry().Find(PasteBuffer{}));
}

}  // namespace afc::editor::transformation

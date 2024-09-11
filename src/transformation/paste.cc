#include "src/transformation/paste.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/buffer_transformation_adapter.h"
#include "src/editor.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/gc.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::VisitOptional;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineSequence;

namespace afc::editor::transformation {

std::wstring Paste::Serialize() const {
  return LazyString{L"Paste()"}.ToString();
}

futures::Value<CompositeTransformation::Output> Paste::Apply(
    CompositeTransformation::Input input) const {
  return TransformationInputAdapterImpl::FindFragment(input.editor)
      .Transform([input](LineSequence paste_data) {
        DVLOG(6) << "Inserting: " << paste_data.ToLazyString();
        return futures::Past(
            CompositeTransformation::Output{transformation::Insert{
                .contents_to_insert = paste_data,
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
      });
}

}  // namespace afc::editor::transformation

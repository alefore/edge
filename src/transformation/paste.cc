#include "src/transformation/paste.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/buffer_transformation_adapter.h"
#include "src/editor.h"
#include "src/fragments.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/error/view.h"
#include "src/language/gc.h"
#include "src/tests/tests.h"
#include "src/vm/escape.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;
using afc::language::view::SkipErrors;
using afc::vm::EscapedString;

namespace afc::editor::transformation {
Paste::Paste(FindFragmentQuery query) : query_(std::move(query)) {}

std::wstring Paste::Serialize() const {
  return LazyString{L"Paste()"}.ToString();
}

futures::Value<CompositeTransformation::Output> Paste::Apply(
    CompositeTransformation::Input input) const {
  INLINE_TRACKER(Paste_Apply);
  return FindFragment(input.editor, query_)
      .Transform(
          [input](std::vector<FilterSortBufferOutput::Match> paste_data) {
            INLINE_TRACKER(Paste_Apply_Insert);
            if (paste_data.empty()) {
              DVLOG(5) << "Empty paste buffer.";
              return CompositeTransformation::Output{};
            }
            DVLOG(6) << "Inserting: " << paste_data.back();
            return CompositeTransformation::Output{transformation::Insert{
                .contents_to_insert = paste_data.back().data,
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
                    })}};
          });
}

}  // namespace afc::editor::transformation

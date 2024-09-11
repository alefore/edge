#include "src/buffer_transformation_adapter.h"

#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/fragments.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"
#include "src/vm/escape.h"

namespace gc = afc::language::gc;

using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::overload;
using afc::language::VisitOptional;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;
using afc::language::text::LineSequence;
using afc::vm::EscapedMap;

namespace afc::editor {
const LineSequence TransformationInputAdapterImpl::contents() const {
  return buffer_.contents().snapshot();
}

void TransformationInputAdapterImpl::SetActiveCursors(
    std::vector<language::text::LineColumn> positions) {
  buffer_.set_active_cursors(std::move(positions));
}

language::text::LineColumn TransformationInputAdapterImpl::InsertInPosition(
    const language::text::LineSequence& contents_to_insert,
    const language::text::LineColumn& input_position,
    const std::optional<infrastructure::screen::LineModifierSet>& modifiers) {
  return buffer_.InsertInPosition(contents_to_insert, input_position,
                                  modifiers);
}

void TransformationInputAdapterImpl::AddError(Error error) {
  buffer_.status().SetInformationText(Line(error.read()));
}

void TransformationInputAdapterImpl::AddFragment(LineSequence fragment) {
  editor::AddFragment(buffer_.editor(), std::move(fragment));
}

}  // namespace afc::editor

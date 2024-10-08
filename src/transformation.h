#ifndef __AFC_EDITOR_TRANSFORMATION_H__
#define __AFC_EDITOR_TRANSFORMATION_H__

#include <glog/logging.h>

#include <memory>

#include "src/direction.h"
#include "src/futures/futures.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/modifiers.h"
#include "src/structure.h"
#include "src/transformation/type.h"

namespace afc::editor {
class OpenBuffer;

// Goes to a given position and applies a transformation.
transformation::Variant TransformationAtPosition(
    const language::text::LineColumn& position,
    transformation::Variant transformation);

// Returns a transformation that deletes superfluous characters (based on
// OpenBuffer::variable_line_suffix_superfluous_characters) from the current
// line.
transformation::Variant NewDeleteSuffixSuperfluousCharacters();
}  // namespace afc::editor

#endif

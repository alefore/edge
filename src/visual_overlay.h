#ifndef __AFC_EDITOR_VISUAL_OVERLAY_H__
#define __AFC_EDITOR_VISUAL_OVERLAY_H__

#include <map>
#include <memory>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/text/line_column.h"

namespace afc::editor {
struct VisualOverlay {
  std::shared_ptr<language::lazy_string::LazyString> content;  // May be null.
  LineModifierSet modifiers;

  bool operator==(const VisualOverlay& other) const {
    return content == other.content && modifiers == other.modifiers;
  }
};

using VisualOverlayMapInternal =
    std::multimap<language::text::LineColumn, VisualOverlay>;
GHOST_TYPE_CONTAINER(VisualOverlayMap, VisualOverlayMapInternal);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_VISUAL_OVERLAY_H__

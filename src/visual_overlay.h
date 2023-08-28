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

GHOST_TYPE(VisualOverlayKey, std::wstring);

// Larger numbers take precedence.
GHOST_TYPE(VisualOverlayPriority, int);
}  // namespace afc::editor

GHOST_TYPE_TOP_LEVEL(afc::editor::VisualOverlayKey);
GHOST_TYPE_TOP_LEVEL(afc::editor::VisualOverlayPriority);

namespace afc::editor {
using VisualOverlayMapInternal = std::map<
    VisualOverlayPriority,
    std::map<VisualOverlayKey,
             std::multimap<language::text::LineColumn, VisualOverlay>>>;

GHOST_TYPE_CONTAINER(VisualOverlayMap, VisualOverlayMapInternal);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_VISUAL_OVERLAY_H__

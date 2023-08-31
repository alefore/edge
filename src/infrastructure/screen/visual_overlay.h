#ifndef __AFC_INFRASTRUCTURE_SCREEN_VISUAL_OVERLAY_H__
#define __AFC_INFRASTRUCTURE_SCREEN_VISUAL_OVERLAY_H__

#include <map>
#include <memory>
#include <variant>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/text/line_column.h"

namespace afc::infrastructure::screen {
struct VisualOverlay {
  std::variant<
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>,
      language::lazy_string::ColumnNumberDelta>
      content = language::lazy_string::ColumnNumberDelta(1);

  LineModifierSet modifiers;

  enum class Behavior { kReplace, kToggle, kOn };
  Behavior behavior = Behavior::kReplace;

  bool operator==(const VisualOverlay& other) const {
    return content == other.content && modifiers == other.modifiers;
  }
};

GHOST_TYPE(VisualOverlayKey, std::wstring);

// Larger numbers take precedence.
GHOST_TYPE(VisualOverlayPriority, int);
}  // namespace afc::infrastructure::screen

GHOST_TYPE_TOP_LEVEL(afc::infrastructure::screen::VisualOverlayKey);
GHOST_TYPE_TOP_LEVEL(afc::infrastructure::screen::VisualOverlayPriority);

namespace afc::infrastructure::screen {
using VisualOverlayMapInternal = std::map<
    VisualOverlayPriority,
    std::map<VisualOverlayKey,
             std::multimap<language::text::LineColumn, VisualOverlay>>>;

GHOST_TYPE_CONTAINER(VisualOverlayMap, VisualOverlayMapInternal);

// Returns a copy of visual_overlay_map that only contains overlays that
// intersect screen_line_range.
VisualOverlayMap FilterOverlays(const VisualOverlayMap& visual_overlay_map,
                                const language::text::Range& screen_line_range);

}  // namespace afc::infrastructure::screen

#endif  // __AFC_INFRASTRUCTURE_SCREEN_VISUAL_OVERLAY_H__

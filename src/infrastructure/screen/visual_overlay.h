#ifndef __AFC_INFRASTRUCTURE_SCREEN_VISUAL_OVERLAY_H__
#define __AFC_INFRASTRUCTURE_SCREEN_VISUAL_OVERLAY_H__

#include <map>
#include <memory>
#include <variant>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/language/text/range.h"

namespace afc::infrastructure::screen {
struct VisualOverlay {
  std::variant<language::lazy_string::SingleLine,
               language::lazy_string::ColumnNumberDelta>
      content = language::lazy_string::ColumnNumberDelta(1);

  LineModifierSet modifiers;

  enum class Behavior { kReplace, kToggle, kOn };
  Behavior behavior = Behavior::kReplace;

  bool operator==(const VisualOverlay& other) const {
    return content == other.content && modifiers == other.modifiers;
  }
};

class VisualOverlayKey
    : public language::GhostType<VisualOverlayKey, std::wstring> {
  using GhostType::GhostType;
};

// Larger numbers take precedence.
class VisualOverlayPriority
    : public language::GhostType<VisualOverlayPriority, int> {
  using GhostType::GhostType;
};

class VisualOverlayMap
    : public language::GhostType<
          VisualOverlayMap,
          std::map<VisualOverlayPriority,
                   std::map<VisualOverlayKey,
                            std::multimap<language::text::LineColumn,
                                          VisualOverlay>>>> {
  using GhostType::GhostType;
};

// Returns a copy of visual_overlay_map that only contains overlays that
// intersect screen_line_range.
VisualOverlayMap FilterOverlays(
    const VisualOverlayMap& visual_overlay_map,
    const language::text::LineRange& screen_line_range);

// Returns a copy of Line after applying all overlays. The LineColumn.line
// values in the keys will be ignored: all overlays in the map will be applied,
// regardless of the line they declare.
language::text::Line ApplyVisualOverlayMap(const VisualOverlayMap& overlays,
                                           language::text::Line& line);

}  // namespace afc::infrastructure::screen
#endif  // __AFC_INFRASTRUCTURE_SCREEN_VISUAL_OVERLAY_H__

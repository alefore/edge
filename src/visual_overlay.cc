#include "src/visual_overlay.h"

namespace afc::editor {
using language::text::LineColumn;
using language::text::Range;

VisualOverlayMap FilterOverlays(const VisualOverlayMap& visual_overlay_map,
                                const Range& screen_line_range) {
  VisualOverlayMap output;
  for (const std::pair<const VisualOverlayPriority,
                       std::map<VisualOverlayKey,
                                std::multimap<LineColumn, VisualOverlay>>>&
           priority_entry : visual_overlay_map) {
    DVLOG(5) << "Visiting overlay priority: " << priority_entry.first;
    for (const std::pair<const VisualOverlayKey,
                         std::multimap<LineColumn, VisualOverlay>>& key_entry :
         priority_entry.second) {
      DVLOG(5) << "Visiting overlay key: " << key_entry.first;
      if (auto overlay_it =
              key_entry.second.lower_bound(screen_line_range.begin);
          overlay_it != key_entry.second.end() &&
          overlay_it->first < screen_line_range.end) {
        while (overlay_it != key_entry.second.end() &&
               overlay_it->first < screen_line_range.end) {
          CHECK_EQ(overlay_it->first.line, screen_line_range.end.line);
          CHECK_GE(overlay_it->first.column, screen_line_range.begin.column);
          output[priority_entry.first][key_entry.first].insert(std::make_pair(
              overlay_it->first - screen_line_range.begin.column.ToDelta(),
              overlay_it->second));
          ++overlay_it;
        }
      }
    }
  }
  DVLOG(4) << "Output overlay priorities: " << output.size();
  return output;
}
}  // namespace afc::editor

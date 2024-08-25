#include "src/infrastructure/screen/visual_overlay.h"

#include "src/language/text/line_builder.h"

using ::operator<<;
namespace afc::infrastructure::screen {

using language::MakeNonNullShared;
using language::NonNull;
using language::overload;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineRange;

VisualOverlayMap FilterOverlays(const VisualOverlayMap& visual_overlay_map,
                                const LineRange& screen_line_range) {
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
              key_entry.second.lower_bound(screen_line_range.read().begin());
          overlay_it != key_entry.second.end() &&
          overlay_it->first < screen_line_range.read().end()) {
        while (overlay_it != key_entry.second.end() &&
               overlay_it->first < screen_line_range.read().end()) {
          CHECK_EQ(overlay_it->first.line, screen_line_range.line());
          CHECK_GE(overlay_it->first.column, screen_line_range.begin_column());
          output[priority_entry.first][key_entry.first].insert(std::make_pair(
              overlay_it->first - screen_line_range.begin_column().ToDelta(),
              overlay_it->second));
          ++overlay_it;
        }
      }
    }
  }
  DVLOG(4) << "Output overlay priorities: " << output.size();
  return output;
}

namespace {
void ApplyVisualOverlay(ColumnNumber column, const VisualOverlay& overlay,
                        LineBuilder& output_line) {
  ColumnNumberDelta length =
      std::visit(overload{[&](LazyString input) { return input.size(); },
                          [&](ColumnNumberDelta l) { return l; }},
                 overlay.content);

  if (column.ToDelta() > output_line.contents().size()) return;
  if ((column + length).ToDelta() > output_line.contents().size())
    length = output_line.contents().size() - column.ToDelta();

  std::map<language::lazy_string::ColumnNumber, LineModifierSet> modifiers =
      output_line.modifiers();

  switch (overlay.behavior) {
    case VisualOverlay::Behavior::kReplace:
      modifiers.erase(modifiers.lower_bound(column),
                      modifiers.lower_bound(column + length));
      modifiers.insert({column, overlay.modifiers});
      modifiers.insert({column + length, {}});
      break;

    case VisualOverlay::Behavior::kToggle:
    case VisualOverlay::Behavior::kOn:
      LineModifierSet last_modifiers;
      if (modifiers.find(column) == modifiers.end()) {
        auto bound = modifiers.lower_bound(column);
        if (bound == modifiers.begin())
          modifiers.insert({column, {}});
        else if (bound != modifiers.end())
          modifiers.insert({column, std::prev(bound)->second});
        else if (modifiers.empty())
          modifiers.insert({column, {}});
        else
          modifiers.insert({column, modifiers.rbegin()->second});
      }
      for (auto it = modifiers.find(column);
           it != modifiers.end() && it->first < column + length; ++it) {
        last_modifiers = it->second;
        for (auto& m : overlay.modifiers) switch (overlay.behavior) {
            case VisualOverlay::Behavior::kReplace:
              LOG(FATAL) << "Invalid behavior; internal error.";
              break;

            case VisualOverlay::Behavior::kOn:
              it->second.insert(m);
              break;

            case VisualOverlay::Behavior::kToggle:
              ToggleModifier(m, it->second);
          }
      }
      if (column.ToDelta() + length == output_line.contents().size())
        output_line.insert_end_of_line_modifiers(last_modifiers);
      else
        modifiers.insert({column + length, last_modifiers});
  }

  std::visit(overload{[&](LazyString input) {
                        for (ColumnNumberDelta i; i < input.size(); ++i)
                          output_line.SetCharacter(
                              column + i, input.get(ColumnNumber() + i), {});
                      },
                      [&](ColumnNumberDelta) {}},
             overlay.content);
  output_line.set_modifiers(modifiers);
}
}  // namespace

Line ApplyVisualOverlayMap(const VisualOverlayMap& overlays, Line& line) {
  LineBuilder line_builder(line);
  for (const std::pair<const VisualOverlayPriority,
                       std::map<VisualOverlayKey,
                                std::multimap<LineColumn, VisualOverlay>>>&
           priority_entry : overlays)
    for (const std::pair<const VisualOverlayKey,
                         std::multimap<LineColumn, VisualOverlay>>& key_entry :
         priority_entry.second)
      for (const std::pair<const LineColumn, VisualOverlay>& overlay :
           key_entry.second) {
        ApplyVisualOverlay(overlay.first.column, overlay.second, line_builder);
      }

  return std::move(line_builder).Build();
}
}  // namespace afc::infrastructure::screen

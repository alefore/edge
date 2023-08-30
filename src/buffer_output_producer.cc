#include "src/buffer_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/parse_tree.h"
#include "src/terminal.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
using infrastructure::Tracker;
using language::compute_hash;
using language::hash_combine;
using language::MakeNonNullShared;
using language::MakeWithHash;
using language::NonNull;
using language::overload;
using language::VisitPointer;
using language::WithHash;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineColumnDelta;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::Range;

void ApplyVisualOverlay(ColumnNumber column, const VisualOverlay& overlay,
                        LineBuilder& output_line) {
  ColumnNumberDelta length =
      std::visit(overload{[&](NonNull<std::shared_ptr<LazyString>> input) {
                            return input->size();
                          },
                          [&](ColumnNumberDelta l) { return l; }},
                 overlay.content);

  if (column.ToDelta() > output_line.contents()->size()) return;
  if ((column + length).ToDelta() > output_line.contents()->size())
    length = output_line.contents()->size() - column.ToDelta();

  std::map<language::lazy_string::ColumnNumber, afc::editor::LineModifierSet>
      modifiers = output_line.modifiers();

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
      if (column.ToDelta() + length == output_line.contents()->size())
        output_line.insert_end_of_line_modifiers(last_modifiers);
      else
        modifiers.insert({column + length, last_modifiers});
  }

  std::visit(overload{[&](NonNull<std::shared_ptr<LazyString>> input) {
                        for (ColumnNumberDelta i; i < input->size(); ++i)
                          output_line.SetCharacter(
                              column + i, input->get(ColumnNumber() + i), {});
                      },
                      [&](ColumnNumberDelta) {}},
             overlay.content);
  output_line.set_modifiers(modifiers);
}

LineWithCursor::Generator ApplyVisualOverlay(
    VisualOverlayMap overlays, LineWithCursor::Generator generator) {
  return LineWithCursor::Generator{
      std::nullopt, [overlays = std::move(overlays), generator]() {
        auto output = generator.generate();
        LineBuilder line_options(output.line.value());
        for (const std::pair<
                 const VisualOverlayPriority,
                 std::map<VisualOverlayKey,
                          std::multimap<LineColumn, VisualOverlay>>>&
                 priority_entry : overlays)
          for (const std::pair<const VisualOverlayKey,
                               std::multimap<LineColumn, VisualOverlay>>&
                   key_entry : priority_entry.second)
            for (const std::pair<const LineColumn, VisualOverlay>& overlay :
                 key_entry.second) {
              ApplyVisualOverlay(overlay.first.column, overlay.second,
                                 line_options);
            }

        output.line = MakeNonNullShared<Line>(std::move(line_options).Build());
        return output;
      }};
}

// Use to highlight entire lines (for variable `atomic_lines`).
LineWithCursor::Generator LineHighlighter(LineWithCursor::Generator generator) {
  return LineWithCursor::Generator{
      std::nullopt, [generator]() {
        auto output = generator.generate();
        LineBuilder line_options(output.line.value());
        std::map<language::lazy_string::ColumnNumber, LineModifierSet>
            new_modifiers;
        new_modifiers.insert({ColumnNumber(0), {}});
        for (auto& m : line_options.modifiers()) {
          auto it = m.second.insert(LineModifier::kReverse);
          if (!it.second) {
            m.second.erase(it.first);
          }
          new_modifiers[m.first] = m.second;
        }
        line_options.set_modifiers(std::move(new_modifiers));
        output.line = MakeNonNullShared<Line>(std::move(line_options).Build());
        return output;
      }};
}

LineWithCursor::Generator ParseTreeHighlighter(
    ColumnNumber begin, ColumnNumber end, LineWithCursor::Generator generator) {
  return LineWithCursor::Generator{
      std::nullopt, [=]() {
        LineWithCursor output = generator.generate();
        LineBuilder line_options(output.line.value());
        std::map<language::lazy_string::ColumnNumber, LineModifierSet>
            modifiers = line_options.modifiers();
        modifiers.erase(modifiers.lower_bound(begin),
                        modifiers.lower_bound(end));
        modifiers[begin] = {LineModifier::kBlue};
        line_options.set_modifiers(std::move(modifiers));
        output.line = MakeNonNullShared<Line>(std::move(line_options).Build());
        return output;
      }};
}

// Adds to `output` all modifiers for the tree relevant to a given range.
//
// If range.begin.column is non-zero, the columns in the output will have
// already subtracted it. In other words, the columns in the output are
// relative to range.begin.column, rather than absolute.
//
// Only modifiers in the line range.begin.line will ever be outputed. Most of
// the time, range.end is either in the same line or at the beginning of the
// next, and this restriction won't apply.
void GetSyntaxModifiersForLine(
    Range range, const ParseTree& tree, LineModifierSet syntax_modifiers,
    std::map<ColumnNumber, LineModifierSet>& output) {
  VLOG(5) << "Getting syntax for " << range << " from " << tree.range();
  if (range.Intersection(tree.range()).IsEmpty()) return;
  auto PushCurrentModifiers = [&](LineColumn tree_position) {
    if (tree_position.line != range.begin.line) return;
    auto column = tree_position.column.MinusHandlingOverflow(
        range.begin.column.ToDelta());
    output[column] = syntax_modifiers;
  };

  PushCurrentModifiers(tree.range().end);
  syntax_modifiers.insert(tree.modifiers().begin(), tree.modifiers().end());
  PushCurrentModifiers(std::max(range.begin, tree.range().begin));

  const auto& children = tree.children();
  auto it = std::upper_bound(
      children.begin(), children.end(), range.begin,
      [](const LineColumn& position, const ParseTree& candidate) {
        return position < candidate.range().end;
      });

  while (it != children.end() && (*it).range().begin <= range.end) {
    GetSyntaxModifiersForLine(range, *it, syntax_modifiers, output);
    ++it;
  }
}

LineWithCursor::Generator ParseTreeHighlighterTokens(
    NonNull<std::shared_ptr<const ParseTree>> root, Range range,
    LineWithCursor::Generator generator) {
  generator.inputs_hash =
      hash_combine(hash_combine(generator.inputs_hash.value(), root->hash()),
                   std::hash<Range>{}(range));
  generator.generate = [root, range, generator = std::move(generator)]() {
    LineWithCursor input = generator.generate();
    LineBuilder options(input.line.value());

    std::map<ColumnNumber, LineModifierSet> syntax_modifiers;
    GetSyntaxModifiersForLine(range, root.value(), {}, syntax_modifiers);
    VLOG(8) << "Syntax tokens for " << range << ": " << syntax_modifiers.size();

    // Merge them.
    std::map<ColumnNumber, LineModifierSet> merged_modifiers;
    auto options_modifiers = options.modifiers();
    auto parent_it = options_modifiers.begin();
    auto syntax_it = syntax_modifiers.begin();
    LineModifierSet current_parent_modifiers;
    LineModifierSet current_syntax_modifiers;
    while ((syntax_it != syntax_modifiers.end() &&
            syntax_it->first <= options.EndColumn()) ||
           parent_it != options_modifiers.end()) {
      if (syntax_it == syntax_modifiers.end()) {
        merged_modifiers.insert(*parent_it);
        ++parent_it;
        if (parent_it == options_modifiers.end()) {
          current_parent_modifiers = options.copy_end_of_line_modifiers();
        }
        continue;
      }
      if (parent_it == options_modifiers.end() ||
          parent_it->first > syntax_it->first) {
        current_syntax_modifiers = syntax_it->second;
        if (current_parent_modifiers.empty()) {
          merged_modifiers[syntax_it->first] = current_syntax_modifiers;
        }
        ++syntax_it;
        continue;
      }
      CHECK(parent_it != options_modifiers.end());
      CHECK(syntax_it != syntax_modifiers.end());
      CHECK_LE(parent_it->first, syntax_it->first);
      current_parent_modifiers = parent_it->second;
      merged_modifiers[parent_it->first] = current_parent_modifiers.empty()
                                               ? current_syntax_modifiers
                                               : current_parent_modifiers;
      ++parent_it;
    }
    options.set_modifiers(std::move(merged_modifiers));

    input.line = MakeNonNullShared<Line>(std::move(options).Build());
    return input;
  };
  return generator;
}

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
}  // namespace

LineWithCursor::Generator::Vector ProduceBufferView(
    const OpenBuffer& buffer,
    const std::vector<BufferContentsViewLayout::Line>& lines,
    const Widget::OutputProducerOptions& output_producer_options) {
  static Tracker tracker(L"ProduceBufferView");
  auto call = tracker.Call();

  CHECK_GE(output_producer_options.size.line, LineNumberDelta());

  const NonNull<std::shared_ptr<const ParseTree>> root = buffer.parse_tree();
  const ParseTree& current_tree = buffer.current_tree(root.value());

  LineWithCursor::Generator::Vector output{
      .lines = {}, .width = output_producer_options.size.column};

  for (BufferContentsViewLayout::Line screen_line : lines) {
    auto line = screen_line.range.begin.line;

    if (line > buffer.EndLine()) {
      output.lines.push_back(LineWithCursor::Generator::Empty());
      continue;
    }

    std::shared_ptr<const Line> line_contents = buffer.LineAt(line);
    std::shared_ptr<EditorMode> editor_keyboard_redirect =
        buffer.editor().keyboard_redirect();
    LineWithCursor::Generator generator =
        LineWithCursor::Generator::New(language::CaptureAndHash(
            [](ColumnNumberDelta size_columns,
               Widget::OutputProducerOptions::MainCursorDisplay
                   main_cursor_display,
               WithHash<std::shared_ptr<const Line>> line_contents_with_hash,
               BufferContentsViewLayout::Line screen_line, bool atomic_lines,
               bool multiple_cursors, LineColumn position,
               EditorMode::CursorMode cursor_mode) {
              LineWithCursor::ViewOptions options{
                  .line = *line_contents_with_hash.value,
                  .initial_column = screen_line.range.begin.column,
                  .width = size_columns,
                  .input_width =
                      screen_line.range.begin.line == screen_line.range.end.line
                          ? screen_line.range.end.column -
                                screen_line.range.begin.column
                          : std::numeric_limits<ColumnNumberDelta>::max()};
              if (!atomic_lines) {
                options.inactive_cursor_columns = screen_line.current_cursors;
                if (position.line == screen_line.range.begin.line &&
                    options.inactive_cursor_columns.erase(position.column)) {
                  options.active_cursor_column = position.column;
                }
                switch (main_cursor_display) {
                  case Widget::OutputProducerOptions::MainCursorDisplay::
                      kActive:
                    switch (cursor_mode) {
                      case EditorMode::CursorMode::kDefault:
                        options.modifiers_main_cursor = {
                            multiple_cursors ? LineModifier::kGreen
                                             : LineModifier::kWhite};
                        break;
                      case EditorMode::CursorMode::kInserting:
                        options.modifiers_main_cursor = {LineModifier::kYellow};
                        break;
                      case EditorMode::CursorMode::kOverwriting:
                        options.modifiers_main_cursor = {
                            LineModifier::kRed, LineModifier::kUnderline};
                        break;
                    }
                    options.modifiers_inactive_cursors =
                        multiple_cursors
                            ? options.modifiers_main_cursor
                            : LineModifierSet({LineModifier::kBlue});
                    // The inactive cursors need the REVERSE modifier to ensure
                    // they get highlighted. The active one doesn't need it,
                    // since the terminal handler actually places the real
                    // cursor in the corresponding position.
                    options.modifiers_inactive_cursors.insert(
                        LineModifier::kReverse);
                    break;
                  case Widget::OutputProducerOptions::MainCursorDisplay::
                      kInactive:
                    options.modifiers_main_cursor = {LineModifier::kBlue};
                    options.modifiers_inactive_cursors = {LineModifier::kBlue};
                    break;
                }
              }
              return LineWithCursor::View(std::move(options));
            },
            output_producer_options.size.column,
            output_producer_options.main_cursor_display,
            MakeWithHash(line_contents, compute_hash(*line_contents)),
            screen_line, buffer.Read(buffer_variables::atomic_lines),
            buffer.Read(buffer_variables::multiple_cursors), buffer.position(),
            (editor_keyboard_redirect == nullptr ? buffer.mode()
                                                 : *editor_keyboard_redirect)
                .cursor_mode()));

    if (&current_tree != root.get().get() &&
        screen_line.range.begin.line >= current_tree.range().begin.line &&
        screen_line.range.begin.line <= current_tree.range().end.line) {
      ColumnNumber begin =
          screen_line.range.begin.line == current_tree.range().begin.line
              ? current_tree.range().begin.column
              : ColumnNumber(0);
      ColumnNumber end =
          screen_line.range.begin.line == current_tree.range().end.line
              ? current_tree.range().end.column
              : line_contents->EndColumn();
      generator = ParseTreeHighlighter(begin, end, std::move(generator));
    } else if (!buffer.parse_tree()->children().empty()) {
      generator = ParseTreeHighlighterTokens(root, screen_line.range,
                                             std::move(generator));
    }

    if (buffer.Read(buffer_variables::atomic_lines) &&
        buffer.active_cursors().cursors_in_line(line)) {
      generator = LineHighlighter(std::move(generator));
    }

    if (VisualOverlayMap overlays =
            FilterOverlays(buffer.visual_overlay_map(), screen_line.range);
        !overlays.empty())
      generator = ApplyVisualOverlay(std::move(overlays), std::move(generator));

    output.lines.push_back(generator);
  }

  return output;
}

namespace {
const bool tests_registration = tests::Register(L"BufferOutputProducer", [] {
  return std::vector<tests::Test>{
      {.name = L"ViewBiggerThanBuffer", .callback = [&] {
         auto buffer = NewBufferForTests();
         std::vector<BufferContentsViewLayout::Line> screen_lines;
         screen_lines.push_back(
             {.range = Range(LineColumn(), LineColumn(LineNumber(1))),
              .has_active_cursor = false,
              .current_cursors = {}});
         auto lines = ProduceBufferView(
             buffer.ptr().value(), screen_lines,
             Widget::OutputProducerOptions{
                 .size = LineColumnDelta(LineNumberDelta(50),
                                         ColumnNumberDelta(80)),
                 .main_cursor_display = Widget::OutputProducerOptions::
                     MainCursorDisplay::kInactive});
         CHECK_EQ(lines.size(), LineNumberDelta(1));
       }}};
}());
}
}  // namespace afc::editor

#include "src/buffer_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/substring.h"
#include "src/line.h"
#include "src/line_column.h"
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
using language::VisitPointer;
using language::WithHash;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;

LineWithCursor::Generator ApplyVisualOverlay(
    VisualOverlayMap overlays, LineWithCursor::Generator generator) {
  return LineWithCursor::Generator{
      std::nullopt, [overlays, generator]() {
        auto output = generator.generate();
        LineBuilder line_options(output.line.value());
        for (std::pair<LineColumn, VisualOverlay> overlay : overlays) {
          ColumnNumber column = overlay.first.column;
          NonNull<std::shared_ptr<LazyString>> content = VisitPointer(
              overlay.second.content,
              [](NonNull<std::shared_ptr<LazyString>> input) { return input; },
              [&] {
                return Substring(line_options.contents(), column,
                                 ColumnNumberDelta(1));
              });
          for (ColumnNumberDelta i; i < content->size(); ++i) {
            line_options.SetCharacter(column + i,
                                      content->get(ColumnNumber() + i),
                                      overlay.second.modifiers);
          }
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
              Line::OutputOptions options{
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

              return line_contents_with_hash.value->Output(std::move(options));
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

    if (auto overlay_it =
            buffer.visual_overlay_map().lower_bound(screen_line.range.begin);
        overlay_it != buffer.visual_overlay_map().end() &&
        overlay_it->first < screen_line.range.end) {
      VisualOverlayMap overlays;
      while (overlay_it != buffer.visual_overlay_map().end() &&
             overlay_it->first < screen_line.range.end) {
        CHECK_EQ(overlay_it->first.line, screen_line.range.end.line);
        CHECK_GE(overlay_it->first.column, screen_line.range.begin.column);
        overlays.insert(std::make_pair(
            overlay_it->first - screen_line.range.begin.column.ToDelta(),
            overlay_it->second));
        ++overlay_it;
      }
      generator = ApplyVisualOverlay(std::move(overlays), std::move(generator));
    }

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

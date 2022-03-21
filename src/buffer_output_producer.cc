#include "src/buffer_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/dirname.h"
#include "src/hash.h"
#include "src/line.h"
#include "src/line_column.h"
#include "src/parse_tree.h"
#include "src/terminal.h"

namespace afc::editor {
namespace {
// Use to highlight entire lines (for variable `atomic_lines`).
OutputProducer::Generator LineHighlighter(OutputProducer::Generator generator) {
  return OutputProducer::Generator{
      std::nullopt, [generator]() {
        auto output = generator.generate();
        Line::Options line_options(*output.line);
        line_options.modifiers.insert({ColumnNumber(0), {}});
        for (auto& m : line_options.modifiers) {
          auto it = m.second.insert(LineModifier::REVERSE);
          if (!it.second) {
            m.second.erase(it.first);
          }
        }
        output.line = std::make_shared<Line>(std::move(line_options));
        return output;
      }};
}

OutputProducer::Generator ParseTreeHighlighter(
    ColumnNumber begin, ColumnNumber end, OutputProducer::Generator generator) {
  return OutputProducer::Generator{
      std::nullopt, [=]() {
        OutputProducer::LineWithCursor output = generator.generate();
        Line::Options line_options(std::move(*output.line));
        LineModifierSet modifiers = {LineModifier::BLUE};
        line_options.modifiers.erase(line_options.modifiers.lower_bound(begin),
                                     line_options.modifiers.lower_bound(end));
        line_options.modifiers[begin] = {LineModifier::BLUE};
        output.line = std::make_shared<Line>(std::move(line_options));
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
    Range range, const ParseTree* tree, LineModifierSet syntax_modifiers,
    std::map<ColumnNumber, LineModifierSet>* output) {
  CHECK(tree);
  VLOG(5) << "Getting syntax for " << range << " from " << tree->range();
  if (range.Intersection(tree->range()).IsEmpty()) return;
  auto PushCurrentModifiers = [&](LineColumn tree_position) {
    if (tree_position.line != range.begin.line) return;
    auto column = tree_position.column.MinusHandlingOverflow(
        range.begin.column.ToDelta());
    (*output)[column] = syntax_modifiers;
  };

  PushCurrentModifiers(tree->range().end);
  syntax_modifiers.insert(tree->modifiers().begin(), tree->modifiers().end());
  PushCurrentModifiers(std::max(range.begin, tree->range().begin));

  const auto& children = tree->children();
  auto it = std::upper_bound(
      children.begin(), children.end(), range.begin,
      [](const LineColumn& position, const ParseTree& candidate) {
        return position < candidate.range().end;
      });

  while (it != children.end() && (*it).range().begin <= range.end) {
    GetSyntaxModifiersForLine(range, &*it, syntax_modifiers, output);
    ++it;
  }
}

OutputProducer::Generator ParseTreeHighlighterTokens(
    const ParseTree* root, Range range, OutputProducer::Generator generator) {
  CHECK(root != nullptr);
  generator.inputs_hash =
      hash_combine(hash_combine(generator.inputs_hash.value(), root->hash()),
                   std::hash<Range>{}(range));
  generator.generate = [root, range, generator = std::move(generator)]() {
    OutputProducer::LineWithCursor input = generator.generate();
    Line::Options options(std::move(*input.line));

    std::map<ColumnNumber, LineModifierSet> syntax_modifiers;
    GetSyntaxModifiersForLine(range, root, {}, &syntax_modifiers);
    LOG(INFO) << "Syntax tokens for " << range << ": "
              << syntax_modifiers.size();

    // Merge them.
    std::map<ColumnNumber, LineModifierSet> merged_modifiers;
    auto parent_it = options.modifiers.begin();
    auto syntax_it = syntax_modifiers.begin();
    LineModifierSet current_parent_modifiers;
    LineModifierSet current_syntax_modifiers;
    while ((syntax_it != syntax_modifiers.end() &&
            syntax_it->first <= options.EndColumn()) ||
           parent_it != options.modifiers.end()) {
      if (syntax_it == syntax_modifiers.end()) {
        merged_modifiers.insert(*parent_it);
        ++parent_it;
        if (parent_it == options.modifiers.end()) {
          current_parent_modifiers = options.end_of_line_modifiers;
        }
        continue;
      }
      if (parent_it == options.modifiers.end() ||
          parent_it->first > syntax_it->first) {
        current_syntax_modifiers = syntax_it->second;
        if (current_parent_modifiers.empty()) {
          merged_modifiers[syntax_it->first] = current_syntax_modifiers;
        }
        ++syntax_it;
        continue;
      }
      CHECK(parent_it != options.modifiers.end());
      CHECK(syntax_it != syntax_modifiers.end());
      CHECK_LE(parent_it->first, syntax_it->first);
      current_parent_modifiers = parent_it->second;
      merged_modifiers[parent_it->first] = current_parent_modifiers.empty()
                                               ? current_syntax_modifiers
                                               : current_parent_modifiers;
      ++parent_it;
    }
    options.modifiers = std::move(merged_modifiers);

    input.line = std::make_shared<Line>(std::move(options));
    return input;
  };
  return generator;
}
}  // namespace

BufferOutputProducer::BufferOutputProducer(
    std::shared_ptr<OpenBuffer> buffer,
    std::list<BufferContentsWindow::Line> lines,
    Widget::OutputProducerOptions output_producer_options)
    : buffer_(std::move(buffer)),
      output_producer_options_(output_producer_options),
      root_(buffer_->parse_tree()),
      current_tree_(buffer_->current_tree(root_.get())),
      lines_(std::move(lines)) {
  if (buffer_->Read(buffer_variables::reload_on_display)) {
    buffer_->Reload();
  }
}

OutputProducer::Generator BufferOutputProducer::Next() {
  if (lines_.empty()) return Generator::Empty();

  BufferContentsWindow::Line screen_line = lines_.front();
  lines_.pop_front();

  auto line = screen_line.range.begin.line;

  if (line > buffer_->EndLine()) return Generator::Empty();

  std::shared_ptr<const Line> line_contents = buffer_->LineAt(line);

  std::shared_ptr<EditorMode> editor_keyboard_redirect =
      buffer_->editor()->keyboard_redirect();
  Generator output = Generator::New(CaptureAndHash(
      [](ColumnNumberDelta size_columns,
         Widget::OutputProducerOptions::MainCursorBehavior main_cursor_behavior,
         WithHash<std::shared_ptr<const Line>> line_contents, Range range,
         bool atomic_lines, bool multiple_cursors, LineColumn position,
         HashableContainer<std::set<ColumnNumber>> cursors,
         EditorMode::CursorMode cursor_mode) {
        Line::OutputOptions options{
            .initial_column = range.begin.column,
            .width = size_columns,
            .input_width = range.begin.line == range.end.line
                               ? range.end.column - range.begin.column
                               : std::numeric_limits<ColumnNumberDelta>::max()};
        if (!atomic_lines) {
          std::set<ColumnNumber> current_cursors;
          // TODO(easy): Compute these things from `data`?
          for (auto& c : cursors.container) {
            if (LineColumn(range.begin.line, c) == position) {
              options.active_cursor_column = c;
            } else {
              options.inactive_cursor_columns.insert(c);
            }
          }
          if (main_cursor_behavior ==
              Widget::OutputProducerOptions::MainCursorBehavior::kHighlight) {
            switch (cursor_mode) {
              case EditorMode::CursorMode::kDefault:
                options.modifiers_main_cursor = {LineModifier::REVERSE,
                                                 multiple_cursors
                                                     ? LineModifier::GREEN
                                                     : LineModifier::CYAN};
                break;
              case EditorMode::CursorMode::kInserting:
                options.modifiers_main_cursor = {LineModifier::YELLOW,
                                                 multiple_cursors
                                                     ? LineModifier::GREEN
                                                     : LineModifier::CYAN};
                break;
              case EditorMode::CursorMode::kOverwriting:
                options.modifiers_main_cursor = {LineModifier::RED,
                                                 LineModifier::UNDERLINE};
                break;
            }
          } else {
            switch (cursor_mode) {
              case EditorMode::CursorMode::kDefault:
                options.modifiers_main_cursor = {LineModifier::WHITE};
                break;
              case EditorMode::CursorMode::kInserting:
                options.modifiers_main_cursor = {LineModifier::YELLOW,
                                                 LineModifier::UNDERLINE};
                break;
              case EditorMode::CursorMode::kOverwriting:
                options.modifiers_main_cursor = {LineModifier::RED,
                                                 LineModifier::UNDERLINE};
                break;
            }
          }
          options.modifiers_inactive_cursors = {
              LineModifier::REVERSE,
              multiple_cursors ? LineModifier::CYAN : LineModifier::BLUE};
        }

        return line_contents.value->Output(std::move(options));
      },
      output_producer_options_.size.column,
      output_producer_options_.main_cursor_behavior,
      MakeWithHash(line_contents, compute_hash(*line_contents)),
      screen_line.range, buffer_->Read(buffer_variables::atomic_lines),
      buffer_->Read(buffer_variables::multiple_cursors), buffer_->position(),
      HashableContainer(std::move(screen_line.current_cursors)),
      (editor_keyboard_redirect == nullptr ? *buffer_->mode()
                                           : *editor_keyboard_redirect)
          .cursor_mode()));

  if (current_tree_ != root_.get() &&
      screen_line.range.begin.line >= current_tree_->range().begin.line &&
      screen_line.range.begin.line <= current_tree_->range().end.line) {
    ColumnNumber begin =
        screen_line.range.begin.line == current_tree_->range().begin.line
            ? current_tree_->range().begin.column
            : ColumnNumber(0);
    ColumnNumber end =
        screen_line.range.begin.line == current_tree_->range().end.line
            ? current_tree_->range().end.column
            : line_contents->EndColumn();
    output = ParseTreeHighlighter(begin, end, std::move(output));
  } else if (!buffer_->parse_tree()->children().empty()) {
    output = ParseTreeHighlighterTokens(root_.get(), screen_line.range,
                                        std::move(output));
  }

  CHECK(line_contents->contents() != nullptr);
  if (buffer_->Read(buffer_variables::atomic_lines) &&
      buffer_->active_cursors()->cursors_in_line(line)) {
    output = LineHighlighter(std::move(output));
  }

  return output;
}
}  // namespace afc::editor

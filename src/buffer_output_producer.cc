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

namespace afc {
namespace editor {
namespace {
OutputProducer::Generator LineHighlighter(OutputProducer::Generator generator) {
  return OutputProducer::Generator{
      std::nullopt, [generator]() {
        auto output = generator.generate();
        Line::Options line_options(*output.line);
        line_options.modifiers.insert({ColumnNumber(0), LineModifierSet()});
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

// Adds to `output` all modifiers for the tree for the current line.
void GetSyntaxModifiersForLine(
    LineNumber line, const ParseTree* tree, LineModifierSet syntax_modifiers,
    std::map<ColumnNumber, LineModifierSet>* output) {
  CHECK(tree);
  LOG(INFO) << "Getting syntax for " << line << " from " << tree->range();
  if (tree->range().end.line == line) {
    (*output)[tree->range().end.column] = syntax_modifiers;
  }

  syntax_modifiers.insert(tree->modifiers().begin(), tree->modifiers().end());
  (*output)[tree->range().begin.line == line ? tree->range().begin.column
                                             : ColumnNumber(0)] =
      syntax_modifiers;

  auto it = tree->children().UpperBound(
      LineColumn(line),
      [](const LineColumn& position, const ParseTree& candidate) {
        return position < candidate.range().end;
      });

  while (it != tree->children().end() && (*it).range().begin.line <= line) {
    GetSyntaxModifiersForLine(line, &*it, syntax_modifiers, output);
    ++it;
  }
}

OutputProducer::Generator ParseTreeHighlighterTokens(
    const ParseTree* root, LineColumn initial_position,
    OutputProducer::Generator generator) {
  CHECK(root != nullptr);
  generator.inputs_hash = hash_combine(generator.inputs_hash.value(),
                                       initial_position, root->hash());
  generator.generate = [root, initial_position,
                        generator = std::move(generator)]() {
    OutputProducer::LineWithCursor input = generator.generate();
    Line::Options options(std::move(*input.line));

    std::map<ColumnNumber, LineModifierSet> syntax_modifiers;
    GetSyntaxModifiersForLine(initial_position.line, root, LineModifierSet(),
                              &syntax_modifiers);
    LOG(INFO) << "Syntax tokens for " << initial_position << ": "
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
    std::shared_ptr<LineScrollControl::Reader> line_scroll_control_reader,
    LineNumberDelta lines_shown, ColumnNumberDelta columns_shown,
    ColumnNumber initial_column)
    : buffer_(std::move(buffer)),
      line_scroll_control_reader_(std::move(line_scroll_control_reader)),
      lines_shown_(lines_shown),
      columns_shown_(columns_shown),
      initial_column_(initial_column),
      root_(buffer_->parse_tree()),
      current_tree_(buffer_->current_tree(root_.get())) {
  CHECK(line_scroll_control_reader_ != nullptr);
  if (buffer_->Read(buffer_variables::reload_on_display)) {
    buffer_->Reload();
  }
}

OutputProducer::Generator BufferOutputProducer::Next() {
  auto optional_range = line_scroll_control_reader_->GetRange();
  if (!optional_range.has_value()) {
    return Generator::Empty();
  }

  auto range = optional_range.value();
  auto line = range.begin.line;

  if (line > buffer_->EndLine()) {
    line_scroll_control_reader_->RangeDone();
    return Generator::Empty();
  }

  std::optional<ColumnNumber> active_cursor_column;
  auto line_contents = buffer_->LineAt(line);

  Generator output;

  bool atomic_lines = buffer_->Read(buffer_variables::atomic_lines);
  bool multiple_cursors = buffer_->Read(buffer_variables::multiple_cursors);
  auto position = buffer_->position();
  auto cursors = line_scroll_control_reader_->GetCurrentCursors();

  line_scroll_control_reader_->RangeDone();

  output.inputs_hash =
      hash_combine(hash_combine(range, atomic_lines, multiple_cursors),
                   hash_combine(columns_shown_, line_contents->GetHash()));
  if (position.line == line) {
    output.inputs_hash = hash_combine(output.inputs_hash.value(), position);
  }
  for (auto& c : cursors) {
    output.inputs_hash = hash_combine(output.inputs_hash.value(), c);
  }

  output.generate = [line_contents, range, atomic_lines, multiple_cursors,
                     columns_shown = columns_shown_, position, cursors]() {
    Line::OutputOptions options;
    options.initial_column = range.begin.column;
    if (range.begin.line == range.end.line) {
      CHECK_GE(range.end.column, range.begin.column);
      CHECK_LE(range.end.column - range.begin.column, columns_shown);
      options.width = range.end.column - range.begin.column;
    } else {
      options.width = columns_shown;
    }

    if (!atomic_lines) {
      std::set<ColumnNumber> current_cursors;
      for (auto& c : cursors) {
        if (LineColumn(range.begin.line, c) == position) {
          options.active_cursor_column = c;
        } else {
          options.inactive_cursor_columns.insert(c);
        }
      }
      options.modifiers_inactive_cursors = {
          LineModifier::REVERSE,
          multiple_cursors ? LineModifier::CYAN : LineModifier::BLUE};
    }

    return line_contents->Output(std::move(options));
  };

  if (current_tree_ != root_.get() &&
      range.begin.line >= current_tree_->range().begin.line &&
      range.begin.line <= current_tree_->range().end.line) {
    ColumnNumber begin = range.begin.line == current_tree_->range().begin.line
                             ? current_tree_->range().begin.column
                             : ColumnNumber(0);
    ColumnNumber end = range.begin.line == current_tree_->range().end.line
                           ? current_tree_->range().end.column
                           : line_contents->EndColumn();
    output = ParseTreeHighlighter(begin, end, std::move(output));
  } else if (!buffer_->parse_tree()->children().empty()) {
    output =
        ParseTreeHighlighterTokens(root_.get(), range.begin, std::move(output));
  }

  CHECK(line_contents->contents() != nullptr);
  if (buffer_->Read(buffer_variables::atomic_lines) &&
      buffer_->active_cursors()->cursors_in_line(line)) {
    output = LineHighlighter(std::move(output));
  }

  return output;
}
}  // namespace editor
}  // namespace afc

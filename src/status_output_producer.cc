#include "src/status_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/horizontal_split_output_producer.h"
#include "src/line_marks.h"
#include "src/section_brackets_producer.h"
#include "src/vertical_split_output_producer.h"

namespace afc::editor {
namespace {
wstring GetBufferContext(const OpenBuffer& buffer) {
  auto marks = buffer.GetLineMarks();
  auto current_line_marks = marks->find(buffer.position().line.line);
  if (current_line_marks != marks->end()) {
    auto mark = current_line_marks->second;
    auto source = buffer.editor()->buffers()->find(mark.source);
    if (source != buffer.editor()->buffers()->end() &&
        LineNumber(0) + source->second->contents()->size() > mark.source_line) {
      return source->second->contents()->at(mark.source_line)->ToString();
    }
  }
  return buffer.Read(buffer_variables::name);
}

class InfoProducer : public OutputProducer {
 public:
  InfoProducer(const Status* status, const OpenBuffer* buffer,
               Modifiers modifiers)
      : status_(status), buffer_(buffer), modifiers_(modifiers) {
    CHECK(status_ != nullptr);
  }

  OutputProducer::Generator Next() {
    return Generator{
        std::nullopt, [this]() {
          wstring output;
          if (buffer_ != nullptr &&
              status_->GetType() != Status::Type::kWarning) {
            output.push_back('[');
            if (buffer_->current_position_line() >
                buffer_->contents()->EndLine()) {
              output += L"<EOF>";
            } else {
              output += buffer_->current_position_line().ToUserString();
            }
            output += L" of " + buffer_->contents()->EndLine().ToUserString() +
                      L", " + buffer_->current_position_col().ToUserString();
            output += L"] ";

            auto marks_text = buffer_->GetLineMarksText();
            if (!marks_text.empty()) {
              output += marks_text + L" ";
            }

            auto active_cursors = buffer_->active_cursors();
            if (active_cursors->size() != 1) {
              output += L" " +
                        (buffer_->Read(buffer_variables::multiple_cursors)
                             ? std::wstring(L"CURSORS")
                             : std::wstring(L"cursors")) +
                        L":" +
                        std::to_wstring(active_cursors->current_index() + 1) +
                        L"/" + std::to_wstring(active_cursors->size()) + L" ";
            }

            auto flags = buffer_->Flags();
            if (modifiers_.repetitions.has_value()) {
              flags.insert(
                  {std::to_wstring(modifiers_.repetitions.value()), L""});
            }
            if (modifiers_.default_direction == Direction::kBackwards) {
              flags.insert({L"REVERSE", L""});
            } else if (modifiers_.direction == Direction::kBackwards) {
              flags.insert({L"reverse", L""});
            }

            if (modifiers_.default_insertion ==
                Modifiers::ModifyMode::kOverwrite) {
              flags.insert({L"OVERWRITE", L""});
            } else if (modifiers_.insertion ==
                       Modifiers::ModifyMode::kOverwrite) {
              flags.insert({L"overwrite", L""});
            }

            if (modifiers_.strength == Modifiers::Strength::kStrong) {
              flags.insert({L"💪", L""});
            }

            wstring structure;
            if (modifiers_.structure == StructureTree()) {
              structure =
                  L"tree<" + std::to_wstring(buffer_->tree_depth()) + L">";
            } else if (modifiers_.structure != StructureChar()) {
              structure = modifiers_.structure->ToString();
            }
            if (!structure.empty()) {
              if (modifiers_.sticky_structure) {
                transform(structure.begin(), structure.end(), structure.begin(),
                          ::toupper);
              }
              flags[L"St:"] = structure;
            }

            if (!flags.empty()) {
              output += L"  " + OpenBuffer::FlagsToString(std::move(flags));
            }

            if (status_->text().empty()) {
              output += L"  “" + GetBufferContext(*buffer_) + L"” ";
            }

            int running = 0;
            int failed = 0;
            for (const auto& it : *buffer_->editor()->buffers()) {
              CHECK(it.second != nullptr);
              if (it.second->child_pid() != -1) {
                running++;
              } else if (it.second->child_exit_status().has_value()) {
                int status = it.second->child_exit_status().value();
                if (WIFEXITED(status) && WEXITSTATUS(status)) {
                  failed++;
                }
              }
            }
            if (running > 0) {
              output += L"  🏃" + std::to_wstring(running) + L"  ";
            }
            if (failed > 0) {
              output += L"  💥" + std::to_wstring(failed) + L"  ";
            }
          }

          LineWithCursor line_with_cursor;
          Line::Options options;

          ColumnNumberDelta status_columns;
          for (auto& c : output) {
            status_columns += ColumnNumberDelta(wcwidth(c));
          }
          LineModifierSet modifiers =
              status_->GetType() == Status::Type::kWarning
                  ? LineModifierSet({LineModifier::RED, LineModifier::BOLD})
                  : LineModifierSet();

          options.AppendString(output, modifiers);

          auto text = status_->text();
          if (status_->prompt_buffer() != nullptr) {
            auto contents = status_->prompt_buffer()->current_line();
            auto column = min(contents->EndColumn(),
                              status_->prompt_buffer()->current_position_col());
            VLOG(5) << "Setting status cursor: " << column;

            options.AppendString(status_->text(), LineModifierSet());
            Line::Options prefix(*contents);
            prefix.DeleteSuffix(column);
            options.Append(Line(std::move(prefix)));
            line_with_cursor.cursor =
                ColumnNumber(0) + options.contents->size();
            Line::Options suffix(*contents);
            suffix.DeleteCharacters(ColumnNumber(0), column.ToDelta());
            options.Append(Line(std::move(suffix)));
          } else {
            VLOG(6) << "Not setting status cursor.";
            options.AppendString(status_->text(), modifiers);
          }
          // options.AddString(ColumnNumberDelta::PaddingString(
          //    options.receiver->width() -
          //    options.receiver->column().ToDelta(), L' '));
          line_with_cursor.line = std::make_shared<Line>(std::move(options));
          return line_with_cursor;
        }};
  }

 private:
  const Status* const status_;
  const OpenBuffer* const buffer_;
  const Modifiers modifiers_;
};
}  // namespace

StatusOutputProducerSupplier::StatusOutputProducerSupplier(
    const Status* status, const OpenBuffer* buffer, Modifiers modifiers)
    : status_(status), buffer_(buffer), modifiers_(modifiers) {
  CHECK(status_ != nullptr);
}

LineNumberDelta StatusOutputProducerSupplier::lines() const {
  LineNumberDelta output;
  if (buffer_ != nullptr || status_->GetType() == Status::Type::kPrompt ||
      !status_->text().empty()) {
    ++output;
  }
  auto context = status_->prompt_context();
  if (context != nullptr) {
    static const auto kLinesForStatusContextStatus = LineNumberDelta(1);
    output += std::min(context->lines_size() + kLinesForStatusContextStatus,
                       LineNumberDelta(10));
  }
  return output;
}

std::unique_ptr<OutputProducer>
StatusOutputProducerSupplier::CreateOutputProducer(LineColumnDelta size) {
  const auto total_lines = lines();
  auto base = std::make_unique<InfoProducer>(status_, buffer_, modifiers_);
  if (total_lines <= LineNumberDelta(1) || total_lines > size.line) {
    return base;
  }

  const auto context_lines = total_lines - LineNumberDelta(1);
  std::vector<HorizontalSplitOutputProducer::Row> rows(2);

  std::vector<VerticalSplitOutputProducer::Column> context_columns(2);
  context_columns[0].width = ColumnNumberDelta(1);
  context_columns[0].producer =
      std::make_unique<SectionBracketsProducer>(context_lines);

  BufferOutputProducerInput buffer_producer_input;
  buffer_producer_input.output_producer_options.size =
      LineColumnDelta(context_lines, size.column);
  buffer_producer_input.buffer = status_->prompt_context();

  context_columns[1].producer =
      CreateBufferOutputProducer(buffer_producer_input).producer;
  rows[0].lines = total_lines - LineNumberDelta(1);
  rows[0].producer = std::make_unique<VerticalSplitOutputProducer>(
      std::move(context_columns), 1);

  rows[1].lines = LineNumberDelta(1);
  rows[1].producer = std::move(base);
  return std::make_unique<HorizontalSplitOutputProducer>(std::move(rows), 1);
}

}  // namespace afc::editor

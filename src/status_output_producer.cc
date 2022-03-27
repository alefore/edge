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
    auto source = buffer.editor().buffers()->find(mark.source);
    if (source != buffer.editor().buffers()->end() &&
        LineNumber(0) + source->second->contents().size() > mark.source_line) {
      return source->second->contents().at(mark.source_line)->ToString();
    }
  }
  return buffer.Read(buffer_variables::name);
}

// This produces the main view of the status, ignoring the context. It handles
// all valid types of statuses (i.e., all values returned by status->GetType()).
class InfoProducer : public OutputProducer {
 public:
  struct Data {
    const Status& status;
    const OpenBuffer* buffer;
    Modifiers modifiers;
  };
  InfoProducer(const Status& status, const OpenBuffer* buffer,
               Modifiers modifiers)
      : data_(std::make_shared<Data>(
            Data{status, buffer, std::move(modifiers)})) {}

  LineWithCursor::Generator::Vector Produce(LineNumberDelta) {
    return LineWithCursor::Generator::Vector{
        .lines = {LineWithCursor::Generator{
            std::nullopt,
            [data = data_]() {
              wstring output;
              if (data->buffer != nullptr &&
                  data->status.GetType() != Status::Type::kWarning) {
                output.push_back('[');
                if (data->buffer->current_position_line() >
                    data->buffer->contents().EndLine()) {
                  output += L"<EOF>";
                } else {
                  output +=
                      data->buffer->current_position_line().ToUserString();
                }
                output += L" of " +
                          data->buffer->contents().EndLine().ToUserString() +
                          L", " +
                          data->buffer->current_position_col().ToUserString();
                output += L"] ";

                auto marks_text = data->buffer->GetLineMarksText();
                if (!marks_text.empty()) {
                  output += marks_text + L" ";
                }

                auto active_cursors = data->buffer->active_cursors();
                if (active_cursors->size() != 1) {
                  output +=
                      L" " +
                      (data->buffer->Read(buffer_variables::multiple_cursors)
                           ? std::wstring(L"CURSORS")
                           : std::wstring(L"cursors")) +
                      L":" +
                      std::to_wstring(active_cursors->current_index() + 1) +
                      L"/" + std::to_wstring(active_cursors->size()) + L" ";
                }

                auto flags = data->buffer->Flags();
                if (data->modifiers.repetitions.has_value()) {
                  flags.insert(
                      {std::to_wstring(data->modifiers.repetitions.value()),
                       L""});
                }
                if (data->modifiers.default_direction ==
                    Direction::kBackwards) {
                  flags.insert({L"REVERSE", L""});
                } else if (data->modifiers.direction == Direction::kBackwards) {
                  flags.insert({L"reverse", L""});
                }

                if (data->modifiers.default_insertion ==
                    Modifiers::ModifyMode::kOverwrite) {
                  flags.insert({L"OVERWRITE", L""});
                } else if (data->modifiers.insertion ==
                           Modifiers::ModifyMode::kOverwrite) {
                  flags.insert({L"overwrite", L""});
                }

                if (data->modifiers.strength == Modifiers::Strength::kStrong) {
                  flags.insert({L"💪", L""});
                }

                wstring structure;
                if (data->modifiers.structure == StructureTree()) {
                  structure = L"tree<" +
                              std::to_wstring(data->buffer->tree_depth()) +
                              L">";
                } else if (data->modifiers.structure != StructureChar()) {
                  structure = data->modifiers.structure->ToString();
                }
                if (!structure.empty()) {
                  if (data->modifiers.sticky_structure) {
                    transform(structure.begin(), structure.end(),
                              structure.begin(), ::toupper);
                  }
                  flags[L"St:"] = structure;
                }

                if (!flags.empty()) {
                  output += L"  " + OpenBuffer::FlagsToString(std::move(flags));
                }

                if (data->status.text().empty()) {
                  output += L"  “" + GetBufferContext(*data->buffer) + L"” ";
                }

                int running = 0;
                int failed = 0;
                for (const auto& it : *data->buffer->editor().buffers()) {
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
                  data->status.GetType() == Status::Type::kWarning
                      ? LineModifierSet({LineModifier::RED, LineModifier::BOLD})
                      : LineModifierSet();

              options.AppendString(output, modifiers);

              auto text = data->status.text();
              if (data->status.prompt_buffer() != nullptr) {
                auto contents = data->status.prompt_buffer()->current_line();
                auto column =
                    min(contents->EndColumn(),
                        data->status.prompt_buffer()->current_position_col());
                VLOG(5) << "Setting status cursor: " << column;

                options.AppendString(data->status.text(), LineModifierSet());
                Line::Options prefix(*contents);
                prefix.DeleteSuffix(column);
                options.Append(Line(std::move(prefix)));
                line_with_cursor.cursor =
                    ColumnNumber(0) + options.contents->size();
                Line::Options suffix(*contents);
                suffix.DeleteCharacters(ColumnNumber(0), column.ToDelta());
                options.Append(Line(std::move(suffix)));
                options.Append(
                    *data->status.prompt_extra_information()->GetLine());
              } else {
                VLOG(6) << "Not setting status cursor.";
                options.AppendString(text, modifiers);
              }
              // options.AddString(ColumnNumberDelta::PaddingString(
              //    options.receiver->width() -
              //    options.receiver->column().ToDelta(), L' '));
              line_with_cursor.line =
                  std::make_shared<Line>(std::move(options));
              return line_with_cursor;
            }}},
        // TODO: This is not correct?
        .width = ColumnNumberDelta(data_->status.text().size())};
  }

 private:
  const std::shared_ptr<Data> data_;
};

bool has_info_line(const Status& status, const OpenBuffer* buffer) {
  return status.GetType() == Status::Type::kPrompt || !status.text().empty() ||
         buffer != nullptr;
}
}  // namespace

StatusOutputProducerSupplier::StatusOutputProducerSupplier(
    StatusOutputOptions options)
    : options_(std::move(options)) {}

LineNumberDelta StatusOutputProducerSupplier::lines() const {
  LineNumberDelta output = has_info_line(options_.status, options_.buffer)
                               ? LineNumberDelta(1)
                               : LineNumberDelta(0);
  auto context = options_.status.context();
  if (context != nullptr) {
    static const auto kLinesForStatusContextStatus = LineNumberDelta(1);
    output += std::min(context->lines_size() + kLinesForStatusContextStatus,
                       LineNumberDelta(10));
  }
  return output;
}

LineWithCursor::Generator::Vector StatusOutputProducerSupplier::Produce(
    LineColumnDelta size) const {
  size.line = min(size.line, lines());
  if (size.line.IsZero()) return LineWithCursor::Generator::Vector{};

  const auto info_lines = has_info_line(options_.status, options_.buffer)
                              ? LineNumberDelta(1)
                              : LineNumberDelta();
  auto base = std::make_unique<InfoProducer>(options_.status, options_.buffer,
                                             options_.modifiers);
  if (size.line <= info_lines) {
    return base->Produce(size.line);
  }

  const auto context_lines = size.line - info_lines;
  CHECK_GT(context_lines, LineNumberDelta(0));
  std::vector<HorizontalSplitOutputProducer::Row> rows;

  auto context_columns =
      std::make_shared<std::vector<VerticalSplitOutputProducer::Column>>(2);
  context_columns->at(0).width = ColumnNumberDelta(1);
  context_columns->at(0).lines = SectionBrackets(context_lines);

  BufferOutputProducerInput buffer_producer_input;
  buffer_producer_input.output_producer_options.size =
      LineColumnDelta(context_lines, size.column);
  buffer_producer_input.buffer = options_.status.context();
  buffer_producer_input.status_behavior =
      BufferOutputProducerInput::StatusBehavior::kIgnore;

  context_columns->at(1).lines =
      CreateBufferOutputProducer(buffer_producer_input).lines;
  rows.push_back({.callback = [context_columns](LineNumberDelta lines)
                      -> LineWithCursor::Generator::Vector {
                    return VerticalSplitOutputProducer(
                               std::move(*context_columns), 1)
                        .Produce(lines);
                  },
                  .lines = context_lines});

  if (has_info_line(options_.status, options_.buffer)) {
    rows.push_back({.callback = OutputProducer::ToCallback(std::move(base)),
                    .lines = info_lines});
  }
  return HorizontalSplitOutputProducer(std::move(rows), 1).Produce(size.line);
}

}  // namespace afc::editor

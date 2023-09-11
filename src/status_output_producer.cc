#include "src/status_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/columns_vector.h"
#include "src/editor.h"
#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/line_marks.h"
#include "src/section_brackets_producer.h"
#include "src/tests/tests.h"
namespace afc::editor {
namespace {
using infrastructure::Tracker;
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::MakeNonNullShared;
using language::NonNull;
using language::Pointer;
using language::VisitPointer;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::ForEachColumn;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineColumnDelta;
using language::text::LineNumber;
using language::text::LineNumberDelta;

namespace gc = language::gc;

std::wstring GetBufferContext(const OpenBuffer& buffer) {
  auto marks = buffer.GetLineMarks();
  if (auto current_line_marks =
          marks.lower_bound(LineColumn(buffer.position().line));
      current_line_marks != marks.end() &&
      current_line_marks->first.line == buffer.position().line) {
    auto mark = current_line_marks->second;
    auto source = buffer.editor().buffers()->find(mark.source_buffer);
    if (source != buffer.editor().buffers()->end() &&
        LineNumber(0) + source->second.ptr()->contents().size() >
            mark.source_line) {
      return source->second.ptr()->contents().at(mark.source_line)->ToString();
    }
  }
  std::wstring name = buffer.Read(buffer_variables::name);
  std::replace(name.begin(), name.end(), L'\n', L' ');
  return name;
}

// This produces the main view of the status, ignoring the context. It handles
// all valid types of statuses (i.e., all values returned by status->GetType()).
LineWithCursor StatusBasicInfo(const StatusOutputOptions& options) {
  LineBuilder output;
  if (options.buffer != nullptr &&
      options.status.GetType() != Status::Type::kWarning) {
    output.AppendCharacter('[', {LineModifier::kDim});
    if (options.buffer->current_position_line() >
        options.buffer->contents().EndLine()) {
      output.AppendString(NewLazyString(L"<EOF>"));
    } else {
      output.AppendString(NewLazyString(to_wstring(
          options.buffer->current_position_line() + LineNumberDelta(1))));
    }
    output.AppendString(NewLazyString(L" of "), {{LineModifier::kDim}});
    output.AppendString(NewLazyString(
        to_wstring(options.buffer->contents().EndLine() + LineNumberDelta(1))));
    output.AppendString(NewLazyString(L", "), {{LineModifier::kDim}});
    output.AppendString(NewLazyString(to_wstring(
        options.buffer->current_position_col() + ColumnNumberDelta(1))));
    output.AppendString(NewLazyString(L"] "), {{LineModifier::kDim}});

    auto marks_text = options.buffer->GetLineMarksText();
    if (!marks_text.empty()) {
      output.AppendString(NewLazyString(marks_text));
      output.AppendCharacter(' ', {});
    }

    auto active_cursors = options.buffer->active_cursors();
    if (active_cursors.size() != 1) {
      // TODO(trivial, 2023-09-11): Improve use of modifiers.
      output.AppendString(NewLazyString(
          L" " +
          (options.buffer->Read(buffer_variables::multiple_cursors)
               ? std::wstring(L"CURSORS")
               : std::wstring(L"cursors")) +
          L":" + std::to_wstring(active_cursors.current_index() + 1) + L"/" +
          std::to_wstring(active_cursors.size()) + L" "));
    }

    std::map<std::wstring, std::wstring> flags = options.buffer->Flags();
    if (options.modifiers.repetitions.has_value()) {
      flags.insert(
          {std::to_wstring(options.modifiers.repetitions.value()), L""});
    }
    if (options.modifiers.default_direction == Direction::kBackwards) {
      flags.insert({L"REVERSE", L""});
    } else if (options.modifiers.direction == Direction::kBackwards) {
      flags.insert({L"reverse", L""});
    }

    if (options.modifiers.default_insertion ==
        Modifiers::ModifyMode::kOverwrite) {
      flags.insert({L"OVERWRITE", L""});
    } else if (options.modifiers.insertion ==
               Modifiers::ModifyMode::kOverwrite) {
      flags.insert({L"overwrite", L""});
    }

    if (options.modifiers.strength == Modifiers::Strength::kStrong) {
      flags.insert({L"💪", L""});
    }

    std::wstring structure;
    if (options.modifiers.structure == Structure::kTree) {
      structure =
          L"tree<" + std::to_wstring(options.buffer->tree_depth()) + L">";
    } else if (options.modifiers.structure != Structure::kChar) {
      structure = ToString(options.modifiers.structure);
    }
    if (!structure.empty()) {
      if (options.modifiers.sticky_structure) {
        transform(structure.begin(), structure.end(), structure.begin(),
                  ::toupper);
      }
      flags[L"St:"] = structure;
    }

    if (!flags.empty()) {
      output.AppendString(
          NewLazyString(L"  " + OpenBuffer::FlagsToString(std::move(flags))));
    }

    if (options.status.text().empty()) {
      output.AppendString(
          NewLazyString(L"  “" + GetBufferContext(*options.buffer) + L"” "));
    }

    int running = 0;
    int failed = 0;
    for (const auto& entry : *options.buffer->editor().buffers()) {
      OpenBuffer& buffer = entry.second.ptr().value();
      if (buffer.child_pid() != -1) {
        running++;
      } else if (buffer.child_exit_status().has_value()) {
        int status = buffer.child_exit_status().value();
        if (WIFEXITED(status) && WEXITSTATUS(status)) {
          failed++;
        }
      }
    }
    if (running > 0) {
      output.AppendString(
          NewLazyString(L"  🏃" + std::to_wstring(running) + L"  "));
    }
    if (failed > 0) {
      output.AppendString(
          NewLazyString(L"  💥" + std::to_wstring(failed) + L"  "));
    }
  }

  std::optional<ColumnNumber> cursor;

  if (options.status.prompt_buffer().has_value()) {
    NonNull<std::shared_ptr<const Line>> contents =
        options.status.prompt_buffer()->ptr()->CurrentLine();
    auto column =
        std::min(contents->EndColumn(),
                 options.status.prompt_buffer()->ptr()->current_position_col());
    VLOG(5) << "Setting status cursor: " << column;

    output.AppendString(options.status.text(), LineModifierSet());
    LineBuilder prefix(contents.value());
    prefix.DeleteSuffix(column);
    output.Append(std::move(prefix));
    cursor = ColumnNumber(0) + output.contents()->size();
    LineBuilder suffix(contents.value());
    suffix.DeleteCharacters(ColumnNumber(0), column.ToDelta());
    output.Append(std::move(suffix));
    output.Append(LineBuilder(options.status.prompt_extra_information_line()));
  } else {
    VLOG(6) << "Not setting status cursor.";
    output.AppendString(
        options.status.text(),
        options.status.GetType() == Status::Type::kWarning
            ? LineModifierSet({LineModifier::kRed, LineModifier::kBold})
            : LineModifierSet());
    if (options.buffer != nullptr) {
      if (NonNull<std::shared_ptr<LazyString>> editor_status_text =
              NewLazyString(options.buffer->editor().status().text());
          !editor_status_text->size().IsZero()) {
        output.AppendString(NewLazyString(L" 🌼 "));
        output.AppendString(editor_status_text);
      }
    }
  }
  return LineWithCursor{
      .line = MakeNonNullShared<Line>(std::move(output).Build()),
      .cursor = cursor};
}

LineNumberDelta context_lines(const StatusOutputOptions& options) {
  return VisitPointer(
      options.status.context(),
      [](gc::Root<OpenBuffer> context) {
        static const auto kLinesForStatusContextStatus = LineNumberDelta(1);
        return std::min(
            context.ptr()->lines_size() + kLinesForStatusContextStatus,
            LineNumberDelta(10));
      },
      [] { return LineNumberDelta(); });
}

auto status_basic_info_tests_registration = tests::Register(
    L"StatusBasicInfo",
    {{.name = L"BufferNameHasEnter", .callback = [] {
        gc::Root<OpenBuffer> buffer = NewBufferForTests();
        buffer.ptr()->Set(buffer_variables::name, L"foo\nbar\nhey");
        buffer.ptr()->Set(buffer_variables::path, L"");
        StatusBasicInfo(StatusOutputOptions{
            .status = buffer.ptr()->status(),
            .buffer = &buffer.ptr().value(),
            .modifiers = {},
            .size =
                LineColumnDelta(LineNumberDelta(1), ColumnNumberDelta(80))});
      }}});
}  // namespace

LineWithCursor::Generator::Vector StatusOutput(StatusOutputOptions options) {
  static Tracker tracker(L"StatusOutput");
  auto call = tracker.Call();

  const auto info_lines = options.status.GetType() == Status::Type::kPrompt ||
                                  !options.status.text().empty() ||
                                  options.buffer != nullptr
                              ? LineNumberDelta(1)
                              : LineNumberDelta();

  options.size.line =
      std::min(options.size.line, info_lines + context_lines(options));
  if (options.size.line.IsZero()) return LineWithCursor::Generator::Vector{};

  std::optional<gc::Root<OpenBuffer>> status_context_optional =
      options.status.context();
  if (options.size.line <= info_lines || !status_context_optional.has_value()) {
    return RepeatLine(StatusBasicInfo(options), options.size.line);
  }
  OpenBuffer& context_buffer = status_context_optional->ptr().value();

  const auto context_lines = options.size.line - info_lines;
  CHECK_GT(context_lines, LineNumberDelta(0));

  ColumnsVector context_columns_vector{.index_active = 1};
  context_columns_vector.push_back(
      {.lines = SectionBrackets(context_lines, SectionBracketsSide::kLeft),
       .width = ColumnNumberDelta(1)});
  CHECK_EQ(context_columns_vector.back().lines.size(), context_lines);

  BufferOutputProducerOutput buffer_output =
      CreateBufferOutputProducer(BufferOutputProducerInput{
          .output_producer_options = {.size = LineColumnDelta(
                                          context_lines, options.size.column)},
          .buffer = context_buffer,
          .buffer_display_data = context_buffer.display_data(),
          .view_start = {},
          .status_behavior =
              BufferOutputProducerInput::StatusBehavior::kIgnore});

  context_columns_vector.push_back({.lines = buffer_output.lines});
  CHECK_EQ(context_columns_vector.back().lines.size(), context_lines);

  auto context_rows = OutputFromColumnsVector(context_columns_vector);
  context_rows.RemoveCursor();
  CHECK_EQ(context_rows.size(), context_lines);

  if (info_lines.IsZero()) return context_rows;

  context_rows.Append(RepeatLine(StatusBasicInfo(options), info_lines));
  return context_rows;
}

}  // namespace afc::editor

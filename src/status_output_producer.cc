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
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/line_marks.h"
#include "src/section_brackets_producer.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
using infrastructure::Tracker;
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::FromByteString;
using language::MakeNonNullShared;
using language::NonNull;
using language::Pointer;
using language::VisitPointer;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::ForEachColumn;
using language::lazy_string::LazyString;
using language::lazy_string::UpperCase;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineColumnDelta;
using language::text::LineNumber;
using language::text::LineNumberDelta;

namespace gc = language::gc;

LazyString GetBufferContext(const OpenBuffer& buffer) {
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
      return source->second.ptr()->contents().at(mark.source_line).contents();
    }
  }
  // TODO(trivial, 2024-08-28): Switch this to ReadLazyString, avoid explicit
  // conversion below.
  std::wstring name = buffer.Read(buffer_variables::name);
  std::replace(name.begin(), name.end(), L'\n', L' ');
  return LazyString{name};
}

// This produces the main view of the status, ignoring the context. It handles
// all valid types of statuses (i.e., all values returned by status->GetType()).
LineWithCursor StatusBasicInfo(const StatusOutputOptions& options) {
  LineBuilder output;
  if (options.buffer != nullptr &&
      options.status.GetType() != Status::Type::kWarning) {
    if (options.buffer->current_position_line() >
        options.buffer->contents().EndLine()) {
      output.AppendString(LazyString{L"🚀"});
    } else {
      output.AppendString(LazyString{to_wstring(
          options.buffer->current_position_line() + LineNumberDelta(1))});
    }
    output.AppendString(LazyString{L" of "}, {{LineModifier::kDim}});
    output.AppendString(LazyString{
        to_wstring(options.buffer->contents().EndLine() + LineNumberDelta(1))});
    output.AppendString(LazyString{L", "}, {{LineModifier::kDim}});
    output.AppendString(LazyString{to_wstring(
        options.buffer->current_position_col() + ColumnNumberDelta(1))});
    output.AppendString(LazyString{L" 🧭 "}, {{LineModifier::kDim}});

    if (LazyString marks_text = options.buffer->GetLineMarksText();
        !marks_text.IsEmpty()) {
      output.AppendString(marks_text);
      output.AppendCharacter(' ', {});
    }

    auto active_cursors = options.buffer->active_cursors();
    if (active_cursors.size() != 1) {
      output.AppendString(
          LazyString{L" "} +
              (options.buffer->Read(buffer_variables::multiple_cursors)
                   ? LazyString{L"✨"}
                   : LazyString{L"👥"}),
          std::nullopt);
      output.AppendString(LazyString{L":"},
                          LineModifierSet{LineModifier::kDim});
      output.AppendString(
          LazyString{std::to_wstring(active_cursors.current_index() + 1)},
          std::nullopt);
      output.AppendString(LazyString{L"/"},
                          LineModifierSet{LineModifier::kDim});
      output.AppendString(
          LazyString{std::to_wstring(active_cursors.size())} + LazyString{L" "},
          std::nullopt);
    }

    std::map<BufferFlagKey, BufferFlagValue> flags = options.buffer->Flags();
    if (options.modifiers.repetitions.has_value()) {
      flags.insert({BufferFlagKey{LazyString{std::to_wstring(
                        options.modifiers.repetitions.value())}},
                    BufferFlagValue{}});
    }
    if (options.modifiers.default_direction == Direction::kBackwards) {
      flags.insert({BufferFlagKey{LazyString{L"REVERSE"}}, BufferFlagValue{}});
    } else if (options.modifiers.direction == Direction::kBackwards) {
      flags.insert({BufferFlagKey{LazyString{L"reverse"}}, BufferFlagValue{}});
    }

    if (options.modifiers.default_insertion ==
        Modifiers::ModifyMode::kOverwrite) {
      flags.insert(
          {BufferFlagKey{LazyString{L"OVERWRITE"}}, BufferFlagValue{}});
    } else if (options.modifiers.insertion ==
               Modifiers::ModifyMode::kOverwrite) {
      flags.insert(
          {BufferFlagKey{LazyString{L"overwrite"}}, BufferFlagValue{}});
    }

    if (options.modifiers.strength == Modifiers::Strength::kStrong) {
      flags.insert({BufferFlagKey{LazyString{L"💪"}}, BufferFlagValue{}});
    }

    LazyString structure;
    if (options.modifiers.structure == Structure::kTree) {
      structure = LazyString{
          L"tree<" + std::to_wstring(options.buffer->tree_depth()) + L">"};
    } else if (options.modifiers.structure != Structure::kChar) {
      std::ostringstream oss;
      oss << options.modifiers.structure;
      structure = LazyString{FromByteString(oss.str())};
    }
    if (!structure.IsEmpty()) {
      if (options.modifiers.sticky_structure)
        structure = UpperCase(std::move(structure));
      flags[BufferFlagKey{LazyString{L"St:"}}] = BufferFlagValue{structure};
    }

    if (!flags.empty()) {
      output.AppendString(LazyString{L"  "} +
                          OpenBuffer::FlagsToString(std::move(flags)));
    }

    if (options.status.text().empty()) {
      output.AppendString(LazyString{L"  “"} +
                          GetBufferContext(*options.buffer) +
                          LazyString{L"” "});
    }

    int running = 0;
    int failed = 0;
    for (const auto& entry : *options.buffer->editor().buffers()) {
      OpenBuffer& buffer = entry.second.ptr().value();
      if (buffer.child_pid().has_value()) {
        running++;
      } else if (buffer.child_exit_status().has_value()) {
        int status = buffer.child_exit_status().value();
        if (WIFEXITED(status) && WEXITSTATUS(status)) {
          failed++;
        }
      }
    }
    if (running > 0) {
      output.AppendString(LazyString{L"  🏃"} +
                          LazyString{std::to_wstring(running)} +
                          LazyString{L"  "});
    }
    if (failed > 0) {
      output.AppendString(LazyString{L"  💥"} +
                          LazyString{std::to_wstring(failed)} +
                          LazyString{L"  "});
    }
  }

  std::optional<ColumnNumber> cursor;

  if (options.status.prompt_buffer().has_value()) {
    const Line& contents = options.status.prompt_buffer()->ptr()->CurrentLine();
    auto column =
        std::min(contents.EndColumn(),
                 options.status.prompt_buffer()->ptr()->current_position_col());
    VLOG(5) << "Setting status cursor: " << column;

    output.Append(LineBuilder(options.status.text()));
    LineBuilder prefix(contents);
    prefix.DeleteSuffix(column);
    output.Append(std::move(prefix));
    cursor = ColumnNumber(0) + output.contents().size();
    LineBuilder suffix(contents);
    suffix.DeleteCharacters(ColumnNumber(0), column.ToDelta());
    output.Append(std::move(suffix));
    output.Append(LineBuilder(options.status.prompt_extra_information_line()));
  } else {
    VLOG(6) << "Not setting status cursor.";
    output.Append(LineBuilder(options.status.text()));
    if (options.buffer != nullptr) {
      if (Line editor_status_text = options.buffer->editor().status().text();
          !editor_status_text.empty()) {
        output.AppendString(LazyString{L" 🌼 "});
        output.Append(LineBuilder{editor_status_text});
      }
    }
  }
  return LineWithCursor{.line = std::move(output).Build(), .cursor = cursor};
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
        NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
        gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
        buffer.ptr()->Set(buffer_variables::name, LazyString{L"foo\nbar\nhey"});
        buffer.ptr()->Set(buffer_variables::path, LazyString{L""});
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

  const LineNumberDelta info_lines =
      options.status.GetType() == Status::Type::kPrompt ||
              !options.status.text().empty() || options.buffer != nullptr
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

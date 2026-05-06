#include "src/log_model_vm.h"

#include "src/buffer.h"
#include "src/editor.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/log_model.h"
#include "src/vm/types.h"

namespace gc = afc::language::gc;

using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::PossibleError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::lazy_string::ToSingleLine;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::vm::Identifier;

namespace afc::editor {
const Identifier& kOpenLogLineIdentifier() {
  static Identifier output = IDENTIFIER_CONSTANT(L"OpenLogLine");
  return output;
}

namespace {
SingleLine ToString(LogEntryValueType value_type) {
  switch (value_type) {
    using enum LogEntryValueType;
    case Path:
      return SINGLE_LINE_CONSTANT(L"path");
    case String:
      return SINGLE_LINE_CONSTANT(L"");
  }
  LOG(FATAL) << "Invalid value_type.";
  std::unreachable();
}

futures::Value<PossibleError> GenerateContents(EditorState&, LogLine log_line,
                                               OpenBuffer& target) {
  LOG(INFO) << "Generate Contents for Log Line View";
  MutableLineSequence output =
      MutableLineSequence::WithLine(Line{SingleLine{L"# Log Line Details"}});

  output.push_back(L"");

  output.append_back(
      log_line.ValueGroups() |
      std::views::transform(
          [](std::pair<LogEntryName, std::vector<LogEntryValue>> data)
              -> LineSequence {
            MutableLineSequence section = MutableLineSequence::WithLine(
                Line{SINGLE_LINE_CONSTANT(L"## ") +
                     language::lazy_string::ToSingleLine(data.first)});

            if (data.second.size() == 1) {
              // TODO(2026-05-05, log, P2): Avoid std::get, don't assume that
              // value is a variant; that couples us too tightly with
              // LogEntryValue.
              SingleLine value_type_description =
                  ToString(data.second.front().value_type);
              if (!value_type_description.empty())
                value_type_description = SINGLE_LINE_CONSTANT(L" (type: ") +
                                         value_type_description +
                                         SINGLE_LINE_CONSTANT(L")");
              section.push_back(
                  Line(SINGLE_LINE_CONSTANT(L"Value: ") +
                       LineSequence::BreakLines(
                           std::get<LazyString>(data.second.front().value))
                           .FoldLines() +
                       value_type_description));
            }
            section.push_back(L"");
            return std::move(section).snapshot();
          }) |
      std::views::join);
  output.push_back(L"");
  target.InsertInPosition(output.snapshot(), target.contents().range().end(),
                          std::nullopt);
  return EmptyValue{};
}
}  // namespace

PossibleError OpenLogLine(OpenBuffer& buffer) {
  LOG(INFO) << "OpenLogLine: " << buffer.name();
  // TODO(2026-05-04, P2): Figure out how to deal with multiple active cursors.
  // Each of them triggers this execution, so this runs N^2 times. That requires
  // a way for HandleVmURL to signal which cursor it is executing.
  std::optional<LogType> log_type = buffer.log_type();
  if (!log_type) return Error{L"Buffer has no valid log type."};

  std::ranges::for_each(
      buffer.active_cursors() | std::views::transform([](LineColumn pos) {
        return pos.line;
      }) | std::ranges::to<std::set>(),
      [&](LineNumber line_number) -> PossibleError {
        LOG(INFO) << "OpenLogLine: " << buffer.name() << ":" << line_number;
        DECLARE_OR_RETURN(
            LogLine log_line,
            log_type->Parse(buffer.contents().at(line_number).contents()));
        LOG(INFO) << "Creating buffer for log line";
        // TODO(trivial, 2024-08-28): Declare a new buffer name?
        BufferName name{LazyString{L"Line Details: "} +
                        ToLazyString(ToSingleLine(buffer.name()) +
                                     SINGLE_LINE_CONSTANT(L":") +
                                     NonEmptySingleLine(line_number.read()))};
        gc::Root<OpenBuffer> output = OpenBuffer::New(OpenBuffer::Options{
            .editor = buffer.editor(),
            .name = name,
            .generate_contents = std::bind_front(
                GenerateContents, std::ref(buffer.editor()), log_line)});
        output->Set(buffer_variables::show_in_buffers_list, false);
        output->Set(buffer_variables::push_positions_to_history, false);
        output->Set(buffer_variables::allow_dirty_delete, true);
        output->Set(buffer_variables::tree_parser,
                    language::lazy_string::ToLazyString(ParserId::Markdown()));
        output->Reload();
        buffer.editor().StartHandlingInterrupts();
        buffer.editor().AddBuffer(output, BuffersList::AddBufferType::Visit);
        buffer.ResetMode();
        return EmptyValue{};
      });
  return EmptyValue{};
}
}  // namespace afc::editor

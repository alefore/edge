#include "src/buffer_state.h"

#include <ranges>

#include "src/buffer_variables.h"
#include "src/language/container.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/language/text/mutable_line_sequence.h"
#include "src/vm/escape.h"

using afc::infrastructure::Path;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::vm::EscapedString;

namespace afc::editor {
namespace {
NonEmptySingleLine SerializeValue(LazyString input) {
  return EscapedString::FromString(input).CppRepresentation();
}

NonEmptySingleLine SerializeValue(int input) {
  return NonEmptySingleLine(input);
}

NonEmptySingleLine SerializeValue(bool input) {
  return input ? NON_EMPTY_SINGLE_LINE_CONSTANT(L"true")
               : NON_EMPTY_SINGLE_LINE_CONSTANT(L"false");
}

NonEmptySingleLine SerializeValue(LineColumn input) {
  return input.ToCppString();
}

template <typename VariableType>
LineSequence AddVariables(std::wstring type_name,
                          const EdgeStruct<VariableType>& variables,
                          const EdgeStructInstance<VariableType>& values) {
  MutableLineSequence contents;
  contents.push_back(L"// Variables: " + type_name);
  // TODO(2023-11-26, ranges): Extend const-tree to be able to receive the range
  // directly. This is tricky because the iterators in the range need to support
  // operator+.
  contents.append_back(language::container::MaterializeVector(
      variables.variables() | std::views::transform([&](const auto& variable) {
        return LineBuilder{
            SingleLine{LazyString{L"buffer.set_"}} + variable.first.read() +
            SingleLine{LazyString{L"("}} +
            SerializeValue(values.Get(&variable.second.value())) +
            SingleLine{LazyString{L");"}}}
            .Build();
      })));
  contents.push_back(L"");
  return contents.snapshot();
}
}  // namespace

LineSequence SerializeState(LineColumn position,
                            const BufferVariablesInstance& variables) {
  MutableLineSequence contents;

  // TODO(2023-11-26, P1): Turn this into an entry in LineColumnStruct.
  contents.push_back(Line{SINGLE_LINE_CONSTANT(L"buffer.set_position(") +
                          position.ToCppString() +
                          SINGLE_LINE_CONSTANT(L");")});
  contents.push_back(L"");

  contents.insert(contents.EndLine(),
                  AddVariables(L"string", *buffer_variables::StringStruct(),
                               variables.string_variables),
                  std::nullopt);

  contents.insert(contents.EndLine(),
                  AddVariables(L"int", *buffer_variables::IntStruct(),
                               variables.int_variables),
                  std::nullopt);

  contents.insert(contents.EndLine(),
                  AddVariables(L"bool", *buffer_variables::BoolStruct(),
                               variables.bool_variables),
                  std::nullopt);

  contents.insert(
      contents.EndLine(),
      AddVariables(L"LineColumn", *buffer_variables::LineColumnStruct(),
                   variables.line_column_variables),
      std::nullopt);
  return contents.snapshot();
}
}  // namespace afc::editor

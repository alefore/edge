#include "src/buffer_flags.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/container.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/line_with_cursor.h"
#include "src/path_flags.h"

namespace container = afc::language::container;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::CaptureAndHash;
using afc::language::GetValueOrDie;
using afc::language::MakeNonNullShared;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;

namespace afc::editor {

std::vector<LineModifier> GetBufferFlag(const OpenBuffer& buffer) {
  using flags::Color;
  using flags::InputKey;
  using flags::InputValue;
  static const InputKey path{NON_EMPTY_SINGLE_LINE_CONSTANT(L"path")};

  static const std::map<Color, LineModifier> modifiers = {
      {Color{NON_EMPTY_SINGLE_LINE_CONSTANT(L"red")}, LineModifier::kRed},
      {Color{NON_EMPTY_SINGLE_LINE_CONSTANT(L"green")}, LineModifier::kGreen},
      {Color{NON_EMPTY_SINGLE_LINE_CONSTANT(L"blue")}, LineModifier::kBlue},
      {Color{NON_EMPTY_SINGLE_LINE_CONSTANT(L"cyan")}, LineModifier::kCyan},
      {Color{NON_EMPTY_SINGLE_LINE_CONSTANT(L"yellow")}, LineModifier::kYellow},
      {Color{NON_EMPTY_SINGLE_LINE_CONSTANT(L"magenta")},
       LineModifier::kMagenta},
      {Color{NON_EMPTY_SINGLE_LINE_CONSTANT(L"white")}, LineModifier::kWhite}};
  static const std::vector<Color> color_values = container::MaterializeVector(
      modifiers | std::views::transform([](auto p) { return p.first; }));
  std::vector<InputKey> spec = {path, path, path};
  std::vector<Color> flag = flags::GenerateFlags(
      spec, color_values,
      {{path, InputValue{buffer.Read(buffer_variables::path)}}});
  CHECK_EQ(flag.size(), spec.size());
  return container::MaterializeVector(flag |
                                      std::views::transform([](Color color) {
                                        return GetValueOrDie(modifiers, color);
                                      }));
}

LineWithCursor::Generator::Vector BufferFlagLines(const OpenBuffer& buffer) {
  return LineWithCursor::Generator::Vector{
      .lines = container::MaterializeVector(
          GetBufferFlag(buffer) | std::views::transform([](auto modifier) {
            return LineWithCursor::Generator::New(CaptureAndHash(
                [](LineModifier m) {
                  LineBuilder options;
                  options.AppendString(
                      SingleLine::Padding<L'█'>(ColumnNumberDelta(80)),
                      LineModifierSet{m});
                  return LineWithCursor{.line = std::move(options).Build()};
                },
                modifier));
          }))};
}
}  // namespace afc::editor

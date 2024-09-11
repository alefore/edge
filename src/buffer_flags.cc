#include "src/buffer_flags.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/container.h"
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
using afc::language::text::Line;
using afc::language::text::LineBuilder;

namespace afc::editor {

std::vector<LineModifier> GetBufferFlag(const OpenBuffer& buffer) {
  using flags::Color;
  using flags::InputKey;
  using flags::InputValue;
  InputKey path(L"path");

  static const std::map<Color, LineModifier> modifiers = {
      {Color(L"red"), LineModifier::kRed},
      {Color(L"green"), LineModifier::kGreen},
      {Color(L"blue"), LineModifier::kBlue},
      {Color(L"cyan"), LineModifier::kCyan},
      {Color(L"yellow"), LineModifier::kYellow},
      {Color(L"magenta"), LineModifier::kMagenta},
      {Color(L"white"), LineModifier::kWhite}};
  static const std::vector<Color> color_values = container::MaterializeVector(
      modifiers | std::views::transform([](auto p) { return p.first; }));
  std::vector<InputKey> spec = {path, path, path};
  std::vector<Color> flag = flags::GenerateFlags(
      spec, color_values,
      // TODO(trivial, 2024-09-11): Avoid call to ToString.
      {{path, InputValue(buffer.Read(buffer_variables::path).ToString())}});
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
                  options.AppendString(LazyString{ColumnNumberDelta(80), L'█'},
                                       LineModifierSet{m});
                  return LineWithCursor{.line = std::move(options).Build()};
                },
                modifier));
          }))};
}
}  // namespace afc::editor

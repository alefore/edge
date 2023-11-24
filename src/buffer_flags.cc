#include "src/buffer_flags.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/container.h"
#include "src/language/lazy_string/padding.h"
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
using afc::language::lazy_string::Padding;
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
  static const std::vector<Color> color_values =
      container::Map([](auto p) { return p.first; }, modifiers);
  std::vector<InputKey> spec = {path, path, path};
  std::vector<Color> flag = flags::GenerateFlags(
      spec, color_values,
      {{path, InputValue(buffer.Read(buffer_variables::path))}});
  CHECK_EQ(flag.size(), spec.size());
  return container::Map(
      [](Color color) { return GetValueOrDie(modifiers, color); }, flag);
}

LineWithCursor::Generator::Vector BufferFlagLines(const OpenBuffer& buffer) {
  return LineWithCursor::Generator::Vector{
      .lines = container::Map(
          [](auto modifier) {
            return LineWithCursor::Generator::New(CaptureAndHash(
                [](LineModifier m) {
                  LineBuilder options;
                  options.AppendString(Padding(ColumnNumberDelta(80), L'█'),
                                       LineModifierSet{m});
                  return LineWithCursor{.line = MakeNonNullShared<Line>(
                                            std::move(options).Build())};
                },
                modifier));
          },
          GetBufferFlag(buffer))};
}
}  // namespace afc::editor

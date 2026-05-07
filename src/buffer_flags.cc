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
using afc::infrastructure::screen::Color;
using afc::infrastructure::screen::StandardColor;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::CaptureAndHash;
using afc::language::GetValueOrDie;
using afc::language::MakeNonNullShared;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LinePartMetadata;

namespace afc::editor {

std::vector<Color> GetBufferFlag(const OpenBuffer& buffer) {
  using flags::InputKey;
  using flags::InputValue;
  static const InputKey path{NON_EMPTY_SINGLE_LINE_CONSTANT(L"path")};

  static const std::vector<Color> colors = {
      StandardColor::Red,  StandardColor::Green,  StandardColor::Blue,
      StandardColor::Cyan, StandardColor::Yellow, StandardColor::Magenta,
      StandardColor::White};
  std::vector<InputKey> spec = {path, path, path};
  return flags::GenerateFlags(
      spec, colors, {{path, InputValue{buffer.Read(buffer_variables::path)}}});
}

LineWithCursor::Generator::Vector BufferFlagLines(const OpenBuffer& buffer) {
  return LineWithCursor::Generator::Vector{
      .lines =
          GetBufferFlag(buffer) | std::views::transform([](auto modifier) {
            return LineWithCursor::Generator::New(CaptureAndHash(
                [](Color m) {
                  LineBuilder options;
                  options.AppendString(
                      SingleLine::Padding<L'█'>(ColumnNumberDelta{80}),
                      LinePartMetadata{.style = Style{.foreground_color = m}});
                  return LineWithCursor{.line = std::move(options).Build()};
                },
                modifier));
          }) |
          std::ranges::to<std::vector>()};
}
}  // namespace afc::editor

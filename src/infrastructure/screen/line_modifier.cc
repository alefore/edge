#include "src/infrastructure/screen/line_modifier.h"

#include <glog/logging.h>

#include <ostream>

#include "src/language/container.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/wstring.h"

using afc::language::compute_hash;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::infrastructure::screen {
namespace {
const std::unordered_map<NonEmptySingleLine, Color>& ColorNames() {
  static const std::unordered_map<NonEmptySingleLine, Color> values = {
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BLACK"), Color::Black},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"RED"), Color::Red},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"GREEN"), Color::Green},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BLUE"), Color::Blue},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"CYAN"), Color::Cyan},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"YELLOW"), Color::Yellow},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"MAGENTA"), Color::Magenta},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"WHITE"), Color::White}};
  return values;
}
}  // namespace

std::expected<Color, language::Error> ColorFromString(
    const NonEmptySingleLine& name) {
  if (auto it = ColorNames().find(name); it != ColorNames().end())
    return it->second;
  return Error{LazyString{L"Unknown color: "} + name};
}

NonEmptySingleLine ColorToString(Color color) {
  static const std::unordered_map<Color, NonEmptySingleLine> values =
      std::views::zip(ColorNames() | std::views::values,
                      ColorNames() | std::views::keys) |
      std::ranges::to<std::unordered_map>();
  auto it = values.find(color);
  CHECK(it != values.end()) << "Invalid color.";
  return it->second;
}

std::ostream& operator<<(std::ostream& os, const Color& s) {
  os << ColorToString(s);
  return os;
}

bool Style::empty() const {
  return foreground_color == std::nullopt && background_color == std::nullopt &&
         attributes == StyleAttribute::None;
}

void Style::Merge(const Style& overlay) {
  if (overlay.foreground_color) foreground_color = overlay.foreground_color;
  if (overlay.background_color) background_color = overlay.background_color;
  attributes =
      static_cast<StyleAttribute>(static_cast<uint16_t>(attributes) |
                                  static_cast<uint16_t>(overlay.attributes));
}

bool operator==(const Style& a, const Style& b) {
  // We deliberately let std::optional<Color> fields stay optional (an
  // alternative implementation could always convert them to their default
  // values: White for foreground, Black for background). We are effectively
  // comparing "deltas" rather than final styles.
  return a.foreground_color == b.foreground_color &&
         a.background_color == b.background_color &&
         a.attributes == b.attributes;
}

/* static */
const std::unordered_map<NonEmptySingleLine, Style>& Style::Names() {
  static const std::unordered_map<NonEmptySingleLine, Style> values = {
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BOLD"),
       Style{.attributes = StyleAttribute::Bold}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"ITALIC"),
       Style{.attributes = StyleAttribute::Italic}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"DIM"),
       Style{.attributes = StyleAttribute::Dim}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"UNDERLINE"),
       Style{.attributes = StyleAttribute::Underline}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"REVERSE"),
       Style{.attributes = StyleAttribute::Reverse}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BLACK"),
       Style{.foreground_color = Color::Black}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"RED"),
       Style{.foreground_color = Color::Red}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"GREEN"),
       Style{.foreground_color = Color::Green}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BLUE"),
       Style{.foreground_color = Color::Blue}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"CYAN"),
       Style{.foreground_color = Color::Cyan}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"YELLOW"),
       Style{.foreground_color = Color::Yellow}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"MAGENTA"),
       Style{.foreground_color = Color::Magenta}},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BG_RED"),
       Style{.background_color = Color::Red}}};
  return values;
}

NonEmptySingleLine Style::ToString() const {
  static const std::unordered_map<Style, NonEmptySingleLine> values =
      std::views::zip(Style::Names() | std::views::values,
                      Style::Names() | std::views::keys) |
      std::ranges::to<std::unordered_map>();
  // TODO(2026-05-05, P0, style): GetValueOrDie is very questionable here.
  return GetValueOrDie(values, *this);
}

/* static */
std::expected<Style, Error> Style::FromString(NonEmptySingleLine modifier) {
  const std::unordered_map<NonEmptySingleLine, Style>& values = Style::Names();
  if (auto it = values.find(modifier); it != values.end()) return it->second;
  return Error{LazyString{L"Unknown modifier: "} + modifier};
}

NonEmptySingleLine StyleAttributeToString(StyleAttribute attribute) {
  static const std::vector<std::pair<StyleAttribute, NonEmptySingleLine>>
      names = {
          {StyleAttribute::Bold, NON_EMPTY_SINGLE_LINE_CONSTANT(L"Bold")},
          {StyleAttribute::Italic, NON_EMPTY_SINGLE_LINE_CONSTANT(L"Italic")},
          {StyleAttribute::Dim, NON_EMPTY_SINGLE_LINE_CONSTANT(L"Dim")},
          {StyleAttribute::Underline,
           NON_EMPTY_SINGLE_LINE_CONSTANT(L"Underline")},
          {StyleAttribute::Reverse, NON_EMPTY_SINGLE_LINE_CONSTANT(L"Reverse")},
          {StyleAttribute::Blink, NON_EMPTY_SINGLE_LINE_CONSTANT(L"Blink")},
      };

  return NonEmptySingleLine::New(
             Concatenate(
                 names |
                 std::views::filter(
                     [attribute](
                         std::pair<StyleAttribute, NonEmptySingleLine> data) {
                       return has_attribute(attribute, data.first);
                     }) |
                 std::views::transform(
                     [](std::pair<StyleAttribute, NonEmptySingleLine> data) {
                       return data.second;
                     }) |
                 Intersperse(NON_EMPTY_SINGLE_LINE_CONSTANT(L"+"))))
      .value_or(NON_EMPTY_SINGLE_LINE_CONSTANT(L"None"));
}

std::ostream& operator<<(std::ostream& os, const Style& s) {
  std::string separator;
  os << "{Style";
  if (s.foreground_color)
    os << " foreground: " << ColorToString(s.foreground_color.value());
  if (s.background_color)
    os << " background: " << ColorToString(s.background_color.value());
  if (s.attributes != StyleAttribute::None)
    os << " attributes: " << StyleAttributeToString(s.attributes);
  os << "}";
  return os;
}

Style HashToStyle(const size_t hash_value,
                  const HashToStyleBold bold_behavior) {
  static const std::vector<Color> foreground_colors = {
      Color::Cyan, Color::Yellow, Color::Red,
      Color::Blue, Color::Green,  Color::Magenta};
  static const std::vector<Color> background_colors = {
      Color::Gray0, Color::Gray1, Color::Gray2,  Color::Gray3,
      Color::Gray4, Color::Gray5, Color::Gray6,  Color::Gray7,
      Color::Gray8, Color::Gray9, Color::Gray10, Color::Gray11,
  };
  Style output{.foreground_color =
                   foreground_colors[hash_value % foreground_colors.size()],
               .background_color =
                   background_colors[(hash_value / foreground_colors.size()) %
                                     background_colors.size()]};
  if (bold_behavior == HashToStyleBold::Sometimes &&
      ((hash_value / (foreground_colors.size() * background_colors.size())) %
       2) == 0)
    output.attributes |= StyleAttribute::Bold;
  return output;
}
}  // namespace afc::infrastructure::screen
namespace std {
std::size_t hash<afc::infrastructure::screen::Style>::operator()(
    const afc::infrastructure::screen::Style& style) const {
  return compute_hash(style.foreground_color, style.background_color,
                      style.attributes);
}
}  // namespace std

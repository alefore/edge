#ifndef __AFC_EDITOR_INFRASTRUCTURE_LINE_MODIFIER_H__
#define __AFC_EDITOR_INFRASTRUCTURE_LINE_MODIFIER_H__

#include <functional>
#include <string>
#include <unordered_set>

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"

namespace afc::infrastructure::screen {
enum class StandardColor {
  Black,
  Red,
  Green,
  Yellow,
  Blue,
  Magenta,
  Cyan,
  White,
  BrightBlack,
  BrightRed,
  BrightGreen,
  BrightYellow,
  BrightBlue,
  BrightMagenta,
  BrightCyan,
  BrightWhite
};

// TODO(2026-05-11, easy): Validate at construction that all coordinates are
// between 0 and 5?
struct ColorCube {
  uint8_t r, g, b;

  static const ColorCube Yellow;
  static const ColorCube Cyan;
  static const ColorCube Magenta;

  ColorCube InterpolateTo(ColorCube target, double transition) const;

  bool operator==(const ColorCube&) const = default;
};

inline constexpr ColorCube ColorCube::Yellow{5, 5, 0};
inline constexpr ColorCube ColorCube::Cyan{0, 5, 5};
inline constexpr ColorCube ColorCube::Magenta{5, 0, 5};

// TODO(2026-05-11, trivial): Add validator that it's between 0 and 23.
struct ColorGrayscale : public language::GhostType<ColorGrayscale, uint8_t> {
  using GhostType::GhostType;
};

using Color = std::variant<StandardColor, ColorCube, ColorGrayscale>;

std::expected<Color, language::Error> ColorFromString(
    const language::lazy_string::NonEmptySingleLine&);
language::lazy_string::NonEmptySingleLine ColorToString(Color);
std::ostream& operator<<(std::ostream& os, const Color& s);

enum class StyleAttribute : uint16_t {
  None = 0,
  Bold = 1 << 0,
  Italic = 1 << 1,
  Dim = 1 << 2,
  Underline = 1 << 3,
  Reverse = 1 << 4,
  Blink = 1 << 5
};

constexpr StyleAttribute operator|(StyleAttribute lhs, StyleAttribute rhs) {
  return static_cast<StyleAttribute>(static_cast<uint16_t>(lhs) |
                                     static_cast<uint16_t>(rhs));
}

constexpr StyleAttribute& operator|=(StyleAttribute& lhs, StyleAttribute rhs) {
  lhs = lhs | rhs;
  return lhs;
}

constexpr StyleAttribute operator^(StyleAttribute lhs, StyleAttribute rhs) {
  return static_cast<StyleAttribute>(static_cast<uint16_t>(lhs) ^
                                     static_cast<uint16_t>(rhs));
}

constexpr StyleAttribute& operator^=(StyleAttribute& lhs, StyleAttribute rhs) {
  lhs = lhs ^ rhs;
  return lhs;
}

// Example: if ((attributes & StyleAttribute::Reverse) != StyleAttribute::None)
constexpr StyleAttribute operator&(StyleAttribute lhs, StyleAttribute rhs) {
  return static_cast<StyleAttribute>(static_cast<uint16_t>(lhs) &
                                     static_cast<uint16_t>(rhs));
}

constexpr StyleAttribute& operator&=(StyleAttribute& lhs, StyleAttribute rhs) {
  lhs = lhs & rhs;
  return lhs;
}

constexpr bool has_attribute(StyleAttribute value, StyleAttribute flag) {
  return (value & flag) == flag;
}

constexpr StyleAttribute operator~(StyleAttribute val) {
  return static_cast<StyleAttribute>(~static_cast<uint16_t>(val));
}

language::lazy_string::NonEmptySingleLine StyleAttributeToString(
    StyleAttribute);

// TODO(P2, easy, 2026-05-06): Make Style deeply immutable.
struct Style {
  std::optional<Color> foreground_color = std::nullopt;
  std::optional<Color> background_color = std::nullopt;
  StyleAttribute attributes = StyleAttribute::None;

  bool empty() const;

  // `name` must be one of the simple styles returned by `Names`.
  static std::expected<Style, language::Error> FromString(
      language::lazy_string::NonEmptySingleLine name);

  void Merge(const Style& overlay);

  // Returns a few simple styles by name.
  static const std::unordered_map<language::lazy_string::NonEmptySingleLine,
                                  Style>&
  Names();

  language::lazy_string::NonEmptySingleLine ToString() const;
};

bool operator==(const Style&, const Style&);

enum class HashToStyleBold { Sometimes, Never };

Style HashToStyle(size_t hash_value, HashToStyleBold bold_behavior);
std::ostream& operator<<(std::ostream& os, const Style& s);

}  // namespace afc::infrastructure::screen
namespace std {
template <>
struct hash<afc::infrastructure::screen::Style> {
  std::size_t operator()(const afc::infrastructure::screen::Style& style) const;
};
template <>
struct hash<afc::infrastructure::screen::ColorCube> {
  std::size_t operator()(const afc::infrastructure::screen::ColorCube&) const;
};
}  // namespace std

#endif  // __AFC_EDITOR_INFRASTRUCTURE_LINE_MODIFIER_H__

#ifndef __AFC_LANGUAGE_LAZY_STRING_COLUMN_NUMBER_H__
#define __AFC_LANGUAGE_LAZY_STRING_COLUMN_NUMBER_H__

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"

namespace afc::language::lazy_string {
struct ColumnNumberDelta : public GhostType<ColumnNumberDelta, int> {
  using GhostType::GhostType;

  bool IsZero() const { return read() == 0; }

  std::strong_ordering operator<=>(const ColumnNumberDelta& other) const {
    return read() <=> other.read();
  }

  std::strong_ordering operator<=>(const InternalType& other) const {
    return read() <=> other;
  }

  std::strong_ordering operator<=>(const size_t& other) const {
    if (read() < 0) return std::strong_ordering::less;
    return static_cast<size_t>(read()) <=> other;
  }
};

struct ColumnNumber : public GhostType<ColumnNumber, size_t> {
  using GhostType::GhostType;

  ColumnNumber previous() const {
    CHECK_GT(read(), 0ul);
    return ColumnNumber{read() - 1ul};
  }
  ColumnNumber next() const { return ColumnNumber{read() + 1ul}; }

  ColumnNumberDelta ToDelta() const;

  bool IsZero() const { return read() == 0ul; }

  ColumnNumber operator+(ColumnNumberDelta delta) const {
    return ColumnNumber{read() + delta.read()};
  }

  ColumnNumber operator-(ColumnNumberDelta delta) const {
    return ColumnNumber{read() - delta.read()};
  }

  static ColumnNumberDelta Subtract(size_t a, size_t b) {
    return ColumnNumberDelta{static_cast<int>(a) - static_cast<int>(b)};
  }

  std::strong_ordering operator<=>(const ColumnNumber& other) const {
    return read() <=> other.read();
  }

  std::strong_ordering operator<=>(const InternalType& other) const {
    return read() <=> other;
  }

  ColumnNumber MinusHandlingOverflow(ColumnNumberDelta delta) const {
    return *this - std::min(delta, ToDelta());
  }

  ColumnNumber& operator+=(ColumnNumberDelta delta) {
    *this = ColumnNumber{
        static_cast<size_t>(static_cast<int>(read()) + delta.read())};
    return *this;
  }
};
}  // namespace afc::language::lazy_string
#endif

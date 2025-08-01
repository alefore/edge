#ifndef __AFC_LANGUAGE_LAZY_STRING_COLUMN_NUMBER_H__
#define __AFC_LANGUAGE_LAZY_STRING_COLUMN_NUMBER_H__

#include "src/language/ghost_type_class.h"

namespace afc::language::lazy_string {
struct ColumnNumberDelta : public GhostType<ColumnNumberDelta, int> {
  using GhostType::GhostType;

  bool IsZero() const { return read() == 0; }
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

  ColumnNumber MinusHandlingOverflow(ColumnNumberDelta delta) const {
    return *this - std::min(delta, ToDelta());
  }

  ColumnNumber& operator+=(ColumnNumberDelta delta) {
    if (delta < ColumnNumberDelta{0})
      CHECK_GE(read(), static_cast<size_t>(-delta.read()));
    *this = ColumnNumber{
        static_cast<size_t>(static_cast<int>(read()) + delta.read())};
    return *this;
  }
};
}  // namespace afc::language::lazy_string
#endif

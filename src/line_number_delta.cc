#include <glog/logging.h>

#include <set>
#include <vector>

#include "src/char_buffer.h"
#include "src/language/wstring.h"
#include "src/lazy_string.h"
#include "src/line_column.h"
#include "src/line_column_vm.h"
#include "src/vm/public/environment.h"

namespace afc {
using language::MakeNonNullUnique;
using language::NonNull;

namespace gc = language::gc;
namespace editor {
bool LineNumberDelta::IsZero() const { return *this == LineNumberDelta(); }

bool operator==(const LineNumberDelta& a, const LineNumberDelta& b) {
  return a.line_delta == b.line_delta;
}

bool operator!=(const LineNumberDelta& a, const LineNumberDelta& b) {
  return !(a == b);
}

std::ostream& operator<<(std::ostream& os, const LineNumberDelta& lc) {
  os << "[line delta: " << lc.line_delta << "]";
  return os;
}

bool operator<(const LineNumberDelta& a, const LineNumberDelta& b) {
  return a.line_delta < b.line_delta;
}
bool operator<=(const LineNumberDelta& a, const LineNumberDelta& b) {
  return a.line_delta <= b.line_delta;
}

bool operator>(const LineNumberDelta& a, const LineNumberDelta& b) {
  return a.line_delta > b.line_delta;
}

bool operator>=(const LineNumberDelta& a, const LineNumberDelta& b) {
  return a.line_delta >= b.line_delta;
}

LineNumberDelta operator+(LineNumberDelta a, const LineNumberDelta& b) {
  a.line_delta += b.line_delta;
  return a;
}

LineNumberDelta operator-(LineNumberDelta a, const LineNumberDelta& b) {
  a.line_delta -= b.line_delta;
  return a;
}

LineNumberDelta operator-(LineNumberDelta a) {
  a.line_delta = -a.line_delta;
  return a;
}

LineNumberDelta operator*(LineNumberDelta a, const size_t& b) {
  a.line_delta *= b;
  return a;
}

LineNumberDelta operator*(const size_t& a, LineNumberDelta b) {
  b.line_delta *= a;
  return b;
}

LineNumberDelta operator*(LineNumberDelta a, const double& b) {
  a.line_delta *= b;
  return a;
}

LineNumberDelta operator*(const double& a, LineNumberDelta b) {
  b.line_delta *= a;
  return b;
}

LineNumberDelta operator/(LineNumberDelta a, const size_t& b) {
  a.line_delta /= b;
  return a;
}

LineNumberDelta& operator+=(LineNumberDelta& a, const LineNumberDelta& value) {
  a.line_delta += value.line_delta;
  return a;
}

LineNumberDelta& operator-=(LineNumberDelta& a, const LineNumberDelta& value) {
  a.line_delta -= value.line_delta;
  return a;
}

LineNumberDelta& operator++(LineNumberDelta& a) {
  a.line_delta++;
  return a;
}

LineNumberDelta operator++(LineNumberDelta& a, int) {
  LineNumberDelta copy = a;
  a.line_delta++;
  return copy;
}

LineNumberDelta& operator--(LineNumberDelta& a) {
  a.line_delta--;
  return a;
}

LineNumberDelta operator--(LineNumberDelta& a, int) {
  LineNumberDelta copy = a;
  a.line_delta--;
  return copy;
}
}  // namespace editor
}  // namespace afc

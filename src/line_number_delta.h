#ifndef __AFC_EDITOR_LINE_NUMBER_DELTA__
#define __AFC_EDITOR_LINE_NUMBER_DELTA__
namespace afc::editor {
struct LineNumberDelta {
  LineNumberDelta() = default;
  explicit LineNumberDelta(int value) : line_delta(value) {}

  bool IsZero() const;

  int line_delta = 0;
};

bool operator==(const LineNumberDelta& a, const LineNumberDelta& b);
bool operator!=(const LineNumberDelta& a, const LineNumberDelta& b);
std::ostream& operator<<(std::ostream& os, const LineNumberDelta& lc);
bool operator<(const LineNumberDelta& a, const LineNumberDelta& b);
bool operator<=(const LineNumberDelta& a, const LineNumberDelta& b);
bool operator>(const LineNumberDelta& a, const LineNumberDelta& b);
bool operator>=(const LineNumberDelta& a, const LineNumberDelta& b);
LineNumberDelta operator+(LineNumberDelta a, const LineNumberDelta& b);
LineNumberDelta operator-(LineNumberDelta a, const LineNumberDelta& b);
LineNumberDelta operator-(LineNumberDelta a);
LineNumberDelta operator*(LineNumberDelta a, const size_t& b);
LineNumberDelta operator*(const size_t& a, LineNumberDelta b);
LineNumberDelta operator*(LineNumberDelta a, const double& b);
LineNumberDelta operator*(const double& a, LineNumberDelta b);
LineNumberDelta operator/(LineNumberDelta a, const size_t& b);
LineNumberDelta& operator+=(LineNumberDelta& a, const LineNumberDelta& value);
LineNumberDelta& operator-=(LineNumberDelta& a, const LineNumberDelta& value);
LineNumberDelta& operator++(LineNumberDelta& a);
LineNumberDelta operator++(LineNumberDelta& a, int);
LineNumberDelta& operator--(LineNumberDelta& a);
LineNumberDelta operator--(LineNumberDelta& a, int);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_LINE_NUMBER_DELTA__

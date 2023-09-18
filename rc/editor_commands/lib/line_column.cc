// TODO(2023-09-18): Allow operator< to be defined so that we can just compare
// them directly? Define it close to LineColumn?
bool LessThan(LineColumn a, LineColumn b) {
  return a.line() < b.line() ||
         (a.line() == b.line() && a.column() < b.column());
}

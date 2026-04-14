#include "src/language/lazy_string/convert.h"

namespace afc::language::lazy_string {
ValueOrError<int> AsInt(LazyString value) {
  try {
    return stoi(value.ToString());
  } catch (const std::invalid_argument& ia) {
    return Error{LazyString{L"stoi failed: invalid argument: "} + value};
  } catch (const std::out_of_range& ia) {
    return Error{LazyString{L"stoi failed: out of range: "} + value};
  }
}
}  // namespace afc::language::lazy_string

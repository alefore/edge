#include "src/value_or_error.h"

#include "glog/logging.h"
#include "src/wstring.h"

namespace afc::editor {

std::ostream& operator<<(std::ostream& os, const Error& p) {
  os << "[Error: " << p.description << "]";
  return os;
}

ValueOrError<EmptyValue> Success() { return ValueType(EmptyValue()); }

}  // namespace afc::editor

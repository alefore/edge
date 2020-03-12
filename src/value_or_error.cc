#include "src/value_or_error.h"

#include "glog/logging.h"
#include "src/wstring.h"

namespace afc::editor {

ValueOrError<EmptyValue> Success() { return ValueType(EmptyValue()); }

}  // namespace afc::editor

#include "src/value_or_error.h"

#include "glog/logging.h"
#include "src/wstring.h"

namespace afc::editor {

PossibleError Success() {
  return ValueOrError<EmptyValue>::Value(EmptyValue());
}

PossibleError Error(std::wstring description) {
  LOG(INFO) << "Error detected: " << description;
  return ValueOrError<EmptyValue>::Error(description);
}

}  // namespace afc::editor

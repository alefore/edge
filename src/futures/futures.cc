#include "src/futures/futures.h"

#include "src/value_or_error.h"

namespace afc::futures {
Value<editor::ValueOrError<editor::EmptyValue>> IgnoreErrors(
    Value<editor::PossibleError> value) {
  Future<editor::PossibleError> output;
  value.SetConsumer(
      [consumer = std::move(output.consumer)](const editor::PossibleError&) {
        consumer(editor::Success());
      });
  return output.value;
}

}  // namespace afc::futures

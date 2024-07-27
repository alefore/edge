#ifndef __AFC_LANGUAGE_TEXT_LINE_PROCESSOR_MAP_H__
#define __AFC_LANGUAGE_TEXT_LINE_PROCESSOR_MAP_H__

#include <functional>
#include <map>

#include "src/futures/futures.h"
#include "src/futures/listenable_value.h"
#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::language::text {

GHOST_TYPE(LineProcessorKey, language::lazy_string::LazyString);
GHOST_TYPE(LineProcessorInput, language::lazy_string::LazyString);
GHOST_TYPE(LineProcessorOutput, language::lazy_string::LazyString);

struct LineProcessorOutputFuture {
  LineProcessorOutput initial_value;
  futures::ListenableValue<LineProcessorOutput> value;
};

// TODO(trivial, 2024-07-27): Make this class thread-safe.
class LineProcessorMap {
 public:
  using Callback = std::function<ValueOrError<LineProcessorOutputFuture>(
      LineProcessorInput)>;

 private:
  std::map<LineProcessorKey, Callback> callbacks_;

 public:
  void Add(LineProcessorKey key, Callback callback);

  std::map<LineProcessorKey, LineProcessorOutputFuture> Process(
      LineProcessorInput input) const;
};
}  // namespace afc::language::text

#endif

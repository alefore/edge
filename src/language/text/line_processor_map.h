#ifndef __AFC_LANGUAGE_TEXT_LINE_PROCESSOR_MAP_H__
#define __AFC_LANGUAGE_TEXT_LINE_PROCESSOR_MAP_H__

#include <functional>
#include <map>

#include "src/futures/futures.h"
#include "src/futures/listenable_value.h"
#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"

namespace afc::language::text {

class LineProcessorKey
    : public language::GhostType<LineProcessorKey,
                                 language::lazy_string::SingleLine> {
  using GhostType::GhostType;
};
class LineProcessorInput
    : public language::GhostType<LineProcessorInput,
                                 language::lazy_string::LazyString> {
  using GhostType::GhostType;
};
class LineProcessorOutput
    : public language::GhostType<LineProcessorOutput,
                                 language::lazy_string::SingleLine> {
  using GhostType::GhostType;
};

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

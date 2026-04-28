#ifndef __AFC_LANGUAGE_TEXT_LINE_PROCESSOR_MAP_H__
#define __AFC_LANGUAGE_TEXT_LINE_PROCESSOR_MAP_H__

#include <functional>
#include <map>

#include "src/concurrent/protected.h"
#include "src/futures/futures.h"
#include "src/futures/progressive.h"
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

struct LogLine {
  class EntryName
      : public language::GhostType<EntryName,
                                   language::lazy_string::NonEmptySingleLine> {
   public:
    using GhostType::GhostType;
  };

  struct EntryValue {
    std::variant<language::lazy_string::LazyString> value;
  };

  std::unordered_map<EntryName, EntryValue> values;
};

using LineProcessorOutputFutureVariant =
    std::variant<futures::Progressive<language::lazy_string::SingleLine>,
                 futures::Progressive<LogLine>>;

class LineProcessorMap {
 public:
  using Callback = std::function<ValueOrError<LineProcessorOutputFutureVariant>(
      LineProcessorInput)>;

 private:
  concurrent::Protected<std::map<LineProcessorKey, Callback>> callbacks_;

 public:
  void Add(LineProcessorKey key, Callback callback);

  std::map<LineProcessorKey, LineProcessorOutputFutureVariant> Process(
      LineProcessorInput input) const;
};
}  // namespace afc::language::text

#endif

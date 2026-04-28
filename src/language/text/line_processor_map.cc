#include "src/language/text/line_processor_map.h"

#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/error/view.h"
#include "src/language/overload.h"

using afc::language::view::SkipErrors;

namespace afc::language::text {
void LineProcessorMap::Add(LineProcessorKey key, Callback callback) {
  callbacks_.lock(
      [&key, &callback](std::map<LineProcessorKey, Callback>& data) {
        InsertOrDie(data, {key, std::move(callback)});
      });
}

std::map<LineProcessorKey, LineProcessorOutputFutureVariant>
LineProcessorMap::Process(LineProcessorInput input) const {
  TRACK_OPERATION(LineProcessorMap_Process);
  return callbacks_.lock([&input](
                             const std::map<LineProcessorKey, Callback>& data) {
    return data |
           std::views::transform(
               [&input](const std::pair<const LineProcessorKey, Callback>& p)
                   -> ValueOrError<std::pair<
                       LineProcessorKey, LineProcessorOutputFutureVariant>> {
                 DECLARE_OR_RETURN(LineProcessorOutputFutureVariant value,
                                   p.second(input));
                 return std::make_pair(p.first, value);
               }) |
           SkipErrors | std::ranges::to<std::map>();
  });
}

}  // namespace afc::language::text

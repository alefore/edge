#include "src/language/text/line_processor_map.h"

#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/overload.h"

namespace afc::language::text {
void LineProcessorMap::Add(LineProcessorKey key, Callback callback) {
  callbacks_.lock(
      [&key, &callback](std::map<LineProcessorKey, Callback>& data) {
        InsertOrDie(data, {key, std::move(callback)});
      });
}

std::map<LineProcessorKey, LineProcessorOutputFuture> LineProcessorMap::Process(
    LineProcessorInput input) const {
  TRACK_OPERATION(LineProcessorMap_Process);
  return callbacks_.lock([&input](
                             const std::map<LineProcessorKey, Callback>& data) {
    std::map<LineProcessorKey, LineProcessorOutputFuture> output;
    std::ranges::for_each(
        data,
        [&output, &input](const std::pair<LineProcessorKey, Callback>& p) {
          std::visit(overload{[&output, &p](LineProcessorOutputFuture value) {
                                output.insert({p.first, value});
                              },
                              IgnoreErrors{}},
                     p.second(input));
        });
    return output;
  });
}

}  // namespace afc::language::text

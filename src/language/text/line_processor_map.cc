#include "src/language/text/line_processor_map.h"

#include "src/language/container.h"
#include "src/language/overload.h"

namespace afc::language::text {
void LineProcessorMap::Add(LineProcessorKey key, Callback callback) {
  InsertOrDie(callbacks_, {key, std::move(callback)});
}

std::map<LineProcessorKey, LineProcessorOutputFuture> LineProcessorMap::Process(
    LineProcessorInput input) const {
  std::map<LineProcessorKey, LineProcessorOutputFuture> output;
  std::ranges::for_each(
      callbacks_,
      [&output, &input](const std::pair<LineProcessorKey, Callback>& p) {
        std::visit(overload{[&output, &p](LineProcessorOutputFuture value) {
                              output.insert({p.first, value});
                            },
                            IgnoreErrors{}},
                   p.second(input));
      });
  return output;
}

}  // namespace afc::language::text

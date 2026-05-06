#include "src/path_flags.h"

#include <glog/logging.h>

#include "src/language/hash.h"

using afc::infrastructure::screen::Color;
using afc::language::compute_hash;

namespace afc::editor::flags {

std::vector<Color> GenerateFlags(const std::vector<InputKey>& spec,
                                 const std::vector<Color>& colors,
                                 std::map<InputKey, InputValue> inputs) {
  CHECK(!spec.empty());
  CHECK(!colors.empty());

  std::vector<Color> output;
  output.reserve(spec.size());

  std::map<InputKey, size_t> previous_count_map;

  for (const InputKey& key : spec) {
    auto& previous_count = previous_count_map[key];
    auto it_value = inputs.find(key);
    output.push_back(
        colors[compute_hash(previous_count,
                            (it_value == inputs.end() ? InputValue()
                                                      : it_value->second)) %
               colors.size()]);
    previous_count++;
  }
  return output;
}

}  // namespace afc::editor::flags

#include "src/vm/filter_similar_names.h"

#include <algorithm>

namespace afc::vm {
std::vector<Identifier> FilterSimilarNames(Identifier name,
                                           std::vector<Identifier> candidates) {
  // TODO(easy, 2022-11-26): This can be significantly more flexible.
  candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                  [&name](Identifier candidate) {
                                    return !std::equal(
                                        name.read().begin(), name.read().end(),
                                        candidate.read().begin(),
                                        [](wchar_t a, wchar_t b) {
                                          return tolower(a) == tolower(b);
                                        });
                                  }),
                   candidates.end());
  return candidates;
}
}  // namespace afc::vm

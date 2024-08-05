#include "src/vm/filter_similar_names.h"

#include <algorithm>

namespace afc::vm {
std::vector<Identifier> FilterSimilarNames(Identifier name,
                                           std::vector<Identifier> candidates) {
  // TODO(easy, 2022-11-26): This can be significantly more flexible.
  std::wstring name_str = name.read().ToString();
  candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                  [&name_str](Identifier candidate) {
                                    return !std::equal(
                                        name_str.begin(), name_str.end(),
                                        candidate.read().ToString().begin(),
                                        [](wchar_t a, wchar_t b) {
                                          return tolower(a) == tolower(b);
                                        });
                                  }),
                   candidates.end());
  return candidates;
}
}  // namespace afc::vm

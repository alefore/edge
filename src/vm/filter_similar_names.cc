#include "src/vm/filter_similar_names.h"

#include <algorithm>

namespace afc::vm {
std::vector<std::wstring> FilterSimilarNames(
    std::wstring name, std::vector<std::wstring> candidates) {
  // TODO(easy, 2022-11-26): This can be significantly more flexible.
  // TODO(trivial, P0): Doesn't this have a bug? It should actually remove!
  std::remove_if(
      candidates.begin(), candidates.end(), [&name](std::wstring candidate) {
        return !std::equal(
            name.begin(), name.end(), candidate.begin(),
            [](wchar_t a, wchar_t b) { return tolower(a) == tolower(b); });
      });
  return candidates;
}
}  // namespace afc::vm

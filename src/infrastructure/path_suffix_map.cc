#include "src/infrastructure/path_suffix_map.h"

#include "src/language/error/value_or_error.h"
#include "src/language/overload.h"

using afc::language::Error;
using afc::language::overload;

namespace afc::infrastructure {

namespace {
// "a/b/c" => {{"c"}, {"b", "c"}, {"a", "b", "c"}}
std::list<std::list<PathComponent>> GetSuffixes(const Path& path) {
  DECLARE_OR_RETURN_OTHER(std::list<PathComponent> components,
                          path.DirectorySplit(),
                          std::list<std::list<PathComponent>>{});

  std::list<std::list<PathComponent>> output;
  while (!components.empty()) {
    output.push_front(components);
    components.pop_front();
  }
  return output;
}
}  // namespace

PathSuffixMap::Data::Data() : paths(&GetSuffixes) {}

void PathSuffixMap::Clear() {
  data_.lock([](Data& data) { data.paths.Clear(); });
}

void PathSuffixMap::Insert(const Path& path) {
  data_.lock([&path](Data& data) { data.paths.Insert(path); });
}

void PathSuffixMap::Erase(const Path& path) {
  data_.lock([&path](Data& data) { data.paths.Erase(path); });
}

std::set<Path> PathSuffixMap::FindPathWithSuffix(const Path& suffix) const {
  DECLARE_OR_RETURN_OTHER(std::list<PathComponent> suffix_components,
                          suffix.DirectorySplit(), std::set<Path>{});
  return data_.lock([&suffix_components](const Data& data) {
    return data.paths.Find(suffix_components);
  });
}
}  // namespace afc::infrastructure

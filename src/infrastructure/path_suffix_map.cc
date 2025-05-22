#include "src/infrastructure/path_suffix_map.h"

#include "src/language/error/value_or_error.h"
#include "src/language/overload.h"

using afc::language::Error;
using afc::language::overload;

namespace afc::infrastructure {

namespace {
// "a/b/c" => {{"c"}, {"b", "c"}, {"a", "b", "c"}}
std::list<std::list<PathComponent>> GetSuffixes(const Path& path) {
  return std::visit(
      overload{[](std::list<PathComponent> components) {
                 std::list<std::list<PathComponent>> output;
                 while (!components.empty()) {
                   output.push_front(components);
                   components.pop_front();
                 }
                 return output;
               },
               [](Error) { return std::list<std::list<PathComponent>>{}; }},
      path.DirectorySplit());
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
  return std::visit(
      overload{[this](std::list<PathComponent> suffix_components) {
                 return data_.lock([&suffix_components](const Data& data) {
                   return data.paths.Find(suffix_components);
                 });
               },
               [](Error) { return std::set<Path>{}; }},
      suffix.DirectorySplit());
}
}  // namespace afc::infrastructure

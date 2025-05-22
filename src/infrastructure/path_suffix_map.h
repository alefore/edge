#ifndef __AFC_EDITOR_INFRASTRUCTURE_PATH_SUFFIX_MAP_H__
#define __AFC_EDITOR_INFRASTRUCTURE_PATH_SUFFIX_MAP_H__

#include <algorithm>
#include <map>
#include <set>

#include "src/concurrent/protected.h"
#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"

namespace afc::infrastructure {
// Suppose that each Key element can be mapped to multiple Value elements.
// InvertedMap lets you look-up all the keys that map to a given Value.
//
// This class is not thread-safe.
template <typename Key, typename Value>
class InvertedMap {
  using ValueSupplier = std::function<std::list<Value>(const Key&)>;
  const ValueSupplier value_supplier_;
  std::map<Value, std::set<Key>> table_;

 public:
  explicit InvertedMap(ValueSupplier value_supplier)
      : value_supplier_(std::move(value_supplier)) {
    CHECK(value_supplier_ != nullptr);
  }

  void Clear() { table_.clear(); }

  void Insert(const Key& key) {
    std::ranges::for_each(
        value_supplier_(key),
        [this, &key](const Value& value) { table_[value].insert(key); });
  }

  void Erase(const Key& key) {
    std::ranges::for_each(
        value_supplier_(key), [this, &key](const Value& value) {
          if (auto keys = table_.find(value); keys != table_.end()) {
            keys->second.erase(key);
            if (keys->second.empty()) table_.erase(keys);
          }
        });
  }

  const std::set<Key>& Find(const Value& value) const {
    if (auto it = table_.find(value); it != table_.end()) return it->second;
    static const std::set<Key> empty_set;
    return empty_set;
  }
};

// If you add path foo/bar/quux, you can then look it up by "quux", "bar/quux"
// or "foo/bar/quux".
class PathSuffixMap {
  using SuffixList = std::list<PathComponent>;
  struct Data {
    Data();
    InvertedMap<Path, SuffixList> paths;
  };

  concurrent::Protected<Data> data_;

 public:
  PathSuffixMap() = default;
  void Clear();
  void Insert(const Path& path);
  void Erase(const Path& path);

  std::set<Path> FindPathWithSuffix(const Path& suffix) const;
};
}  // namespace afc::infrastructure
#endif
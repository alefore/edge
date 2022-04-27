#ifndef __AFC_EDITOR_LRU_CACHE_H__
#define __AFC_EDITOR_LRU_CACHE_H__

#include <cwchar>
#include <list>
#include <memory>
#include <string>

#include "src/concurrent/protected.h"

namespace afc::editor {

// This class is thread-safe.
template <typename Key, typename Value>
class LRUCache {
  struct Data {
    size_t max_size;

    // Most recent at the front.
    struct Entry {
      Key key;
      Value value;
    };
    std::list<Entry> access_order = {};
    std::unordered_map<Key, decltype(access_order.begin())> map = {};
  };

 public:
  LRUCache(size_t max_size)
      : data_(Data{.max_size = max_size}, LRUCache::ValidateInvariants) {}

  void SetMaxSize(size_t max_size) {
    data_.lock([max_size](Data& data) {
      data.max_size = max_size;
      DeleteExpiredEntries(data);
    });
  }

  void Clear() {
    data_.lock([](Data& data) {
      LOG(INFO) << "Clearing LRU Cache (size: " << data.access_order.size();
      data.map.clear();
      data.access_order.clear();
    });
  }

  // If the key is currently in the map, just returns its value.
  //
  // Otherwise, runs the Creator callback, a function that receives zero
  // arguments and returns a Value. The returned value is stored in the map and
  // returned.
  //
  // Creator shouldn't attempt to use the map; otherwise, deadlocks are likely
  // to occur.
  template <typename Creator>
  Value* Get(Key key, Creator creator) {
    return data_.lock([&key, &creator](Data& data) {
      auto [it, inserted] =
          data.map.insert({std::move(key), data.access_order.end()});
      if (inserted) {
        VLOG(4) << "Inserted a new entry: " << it->first;
        data.access_order.push_front({it->first, creator()});
        it->second = data.access_order.begin();
        DeleteExpiredEntries(data);
      } else if (it->second != data.access_order.begin()) {
        VLOG(5) << "Entry already existed, but wasn't at front: " << it->first;
        data.access_order.push_front(std::move(*it->second));
        data.access_order.erase(it->second);
        it->second = data.access_order.begin();
      } else {
        VLOG(5) << "Entry is already at front.";
      }
      return &data.access_order.front().value;
    });
  }

 private:
  static void ValidateInvariants(const Data& data) {
    CHECK_EQ(data.access_order.size(), data.map.size());
  }

  static void DeleteExpiredEntries(Data& data) {
    while (data.access_order.size() >= data.max_size) {
      VLOG(5) << "Expiring entry with key: " << data.access_order.back().key;
      size_t erase_result = data.map.erase(data.access_order.back().key);
      CHECK_EQ(erase_result, 1ul);
      data.access_order.pop_back();
    }
  }

  concurrent::Protected<Data, decltype(&LRUCache::ValidateInvariants)> data_;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_LRU_CACHE_H__

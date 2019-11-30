#ifndef __AFC_EDITOR_LRU_CACHE_H__
#define __AFC_EDITOR_LRU_CACHE_H__

#include <cwchar>
#include <list>
#include <memory>
#include <string>

#include "editor.h"
#include "output_producer.h"
#include "screen.h"

namespace afc {
namespace editor {

template <typename Key, typename Value>
class LRUCache {
 public:
  LRUCache(size_t max_size) : max_size_(max_size) {}

  void Clear() {
    ValidateInvariants();
    LOG(INFO) << "Clearing LRU Cache (size: " << access_order_.size();
    map_.clear();
    access_order_.clear();
    ValidateInvariants();
  }

  // If the key is currently in the map, just returns its value.
  //
  // Otherwise, runs the Creator callback, a function that receives zero
  // arguments and returns a Value. The returned value is stored in the map and
  // returned.
  template <typename Creator>
  Value* Get(Key key, Creator creator) {
    ValidateInvariants();
    auto insert_result =
        map_.insert(std::make_pair(std::move(key), access_order_.end()));
    if (insert_result.second) {
      VLOG(4) << "Inserted a new entry: " << insert_result.first->first;
      access_order_.push_front({insert_result.first->first, creator()});
      insert_result.first->second = access_order_.begin();

      if (access_order_.size() >= max_size_) {
        VLOG(5) << "Expiring entry with key: " << access_order_.back().key;
        size_t erase_result = map_.erase(access_order_.back().key);
        CHECK_EQ(erase_result, 1ul);
        access_order_.pop_back();
      }
    } else if (insert_result.first->second != access_order_.begin()) {
      VLOG(5) << "Entry already existed, but wasn't at front: "
              << insert_result.first->first;
      access_order_.push_front(std::move(*insert_result.first->second));
      access_order_.erase(insert_result.first->second);
      insert_result.first->second = access_order_.begin();
    } else {
      VLOG(5) << "Entry is already at front.";
    }
    ValidateInvariants();
    return &access_order_.front().value;
  }

 private:
  void ValidateInvariants() {
    CHECK_GT(max_size_, 0ul);
    CHECK_EQ(access_order_.size(), map_.size());
  }

  const size_t max_size_;

  // Most recent at the front.
  struct AccessEntry {
    Key key;
    Value value;
  };
  std::list<AccessEntry> access_order_;
  std::unordered_map<Key, decltype(access_order_.begin())> map_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LRU_CACHE_H__

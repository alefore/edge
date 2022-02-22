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

  void SetMaxSize(size_t max_size) {
    ValidateInvariants();
    max_size_ = max_size;
    DeleteExpiredEntries();
    ValidateInvariants();
  }

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
    auto [it, inserted] = map_.insert({std::move(key), access_order_.end()});
    if (inserted) {
      VLOG(4) << "Inserted a new entry: " << it->first;
      access_order_.push_front({it->first, creator()});
      it->second = access_order_.begin();
      DeleteExpiredEntries();
    } else if (it->second != access_order_.begin()) {
      VLOG(5) << "Entry already existed, but wasn't at front: " << it->first;
      access_order_.push_front(std::move(*it->second));
      access_order_.erase(it->second);
      it->second = access_order_.begin();
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

  void DeleteExpiredEntries() {
    while (access_order_.size() >= max_size_) {
      VLOG(5) << "Expiring entry with key: " << access_order_.back().key;
      size_t erase_result = map_.erase(access_order_.back().key);
      CHECK_EQ(erase_result, 1ul);
      access_order_.pop_back();
    }
  }

  size_t max_size_;

  // Most recent at the front.
  struct Entry {
    Key key;
    Value value;
  };
  std::list<Entry> access_order_;
  std::unordered_map<Key, decltype(access_order_.begin())> map_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LRU_CACHE_H__

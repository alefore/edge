// Unordered container of objects, optimized to allow concurrent operations
// spread across a thread-pool.

#ifndef __AFC_EDITOR_CONCURRENT_BAG_H__
#define __AFC_EDITOR_CONCURRENT_BAG_H__

#include <list>
#include <utility>

#include "src/concurrent/operation.h"
#include "src/concurrent/protected.h"
#include "src/infrastructure/tracker.h"

namespace afc::concurrent {
struct BagOptions {
  size_t shards = 64;
};

// This class is thread-safe.
template <typename T>
class Bag {
 public:
  Bag(BagOptions options)
      : options_(std::move(options)), shards_(options_.shards) {
    CHECK_EQ(options_.shards, shards_.size());
  }

  Bag(Bag&&) = default;
  Bag& operator=(Bag&&) = default;

  struct iterator {
    typename std::list<T>::iterator it;
    size_t shard;
  };

  size_t size() const {
    size_t output = 0;
    ForEachShardSerial([&](const std::list<T>& s) { output += s.size(); });
    return output;
  }

  bool empty() const { return size() == 0; }

  iterator Add(T t) {
    size_t shard = rand() % shards_.size();
    return iterator{
        .it = shards_[shard].lock(
            [&](language::NonNull<std::unique_ptr<std::list<T>>>& l) {
              l->push_back(std::move(t));
              return std::prev(l->end());
            }),
        .shard = shard};
  }

  template <class Predicate>
  void remove_if(const Operation& operation, Predicate predicate) {
    ForEachShard(operation,
                 [&predicate](std::list<T>& s) { s.remove_if(predicate); });
  }

  void erase(iterator position) {
    CHECK_LT(position.shard, shards_.size());
    shards_[position.shard].lock(
        [&](language::NonNull<std::unique_ptr<std::list<T>>>& shard) {
          shard->erase(position.it);
        });
  }

  template <typename Callable>
  void ForEachShard(const Operation& operation, Callable callable) {
    CHECK_EQ(options_.shards, shards_.size());
    for (size_t i = 0; i < options_.shards; i++)
      operation.Add([&shard = shards_[i], callable] {
        shard.lock(
            [callable](language::NonNull<std::unique_ptr<std::list<T>>>& s) {
              callable(s.value());
            });
      });
  }

  template <typename Callable>
  void ForEachSerial(Callable callable) const {
    ForEachShardSerial([&callable](const std::list<T>& s) {
      for (const auto& t : s) callable(t);
    });
  }

  template <typename Callable>
  void ForEachSerial(Callable callable) {
    ForEachShardSerial(
        [&callable](language::NonNull<std::unique_ptr<std::list<T>>>& s) {
          for (auto& t : s.value()) callable(t);
        });
  }

 private:
  template <typename Callable>
  void ForEachShardSerial(Callable callable) {
    CHECK_EQ(options_.shards, shards_.size());
    for (size_t i = 0; i < options_.shards; i++) shards_[i].lock(callable);
  }

  template <typename Callable>
  void ForEachShardSerial(Callable callable) const {
    CHECK_EQ(options_.shards, shards_.size());
    for (size_t i = 0; i < options_.shards; i++)
      shards_[i].lock(
          [callable](
              const language::NonNull<std::unique_ptr<std::list<T>>>& shard) {
            callable(shard.value());
          });
  }

  // Not const to allow move operations.
  BagOptions options_;
  // These is a vector of NonNull<std::unique_ptr<>> to be able to support moves
  // that don't invalidate iterators.
  std::vector<Protected<language::NonNull<std::unique_ptr<std::list<T>>>>>
      shards_;
};
}  // namespace afc::concurrent
#endif

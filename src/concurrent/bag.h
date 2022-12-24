// Unordered container of objects, optimized to allow concurrent operations
// spread across a thread-pool.

#ifndef __AFC_EDITOR_CONCURRENT_BAG_H__
#define __AFC_EDITOR_CONCURRENT_BAG_H__

#include <list>

#include "src/concurrent/operation.h"
#include "src/concurrent/protected.h"
#include "src/concurrent/thread_pool.h"
#include "src/infrastructure/tracker.h"

namespace afc::concurrent {
struct BagOptions {
  size_t shards = 64;
};

// BagIterators is similar to Bag: an unsorted container of iterators into a
// Bag. This is provided to allow efficient concurrent removal of large sets of
// iterators at once.
template <typename T>
class BagIterators;

template <typename T>
class Bag {
 public:
  using Iterators = BagIterators<T>;

  Bag(BagOptions options)
      : options_(std::move(options)), shards_(options_.shards) {
    CHECK_EQ(options_.shards, shards_.size());
  }

  Bag(Bag&&) = default;
  Bag& operator=(Bag&&) = default;

  struct iterator {
    std::list<T>::iterator it;
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
  void remove_if(ThreadPool& pool, Predicate predicate) {
    ForEachShard(pool,
                 [&predicate](std::list<T>& s) { s.remove_if(predicate); });
  }

  void erase(iterator position) {
    CHECK_LT(position.shard, shards_.size());
    shards_[position.shard].lock(
        [&](language::NonNull<std::unique_ptr<std::list<T>>>& shard) {
          shard->erase(position.it);
        });
  }

  void Consume(ThreadPool& pool, Bag<T> other) {
    Protected<size_t> read_position(0);
    ForEachShard(pool, [&read_position, &other](std::list<T>& shard) {
      while (true) {
        size_t position = read_position.lock([&](size_t& p) {
          if (p < other.shards_.size()) return p++;
          return p;
        });
        if (position >= other.shards_.size()) return;
        language::NonNull<std::unique_ptr<std::list<T>>> values =
            other.shards_[position].lock(
                [](language::NonNull<std::unique_ptr<std::list<T>>>& s) {
                  return std::move(s);
                });
        shard.insert(shard.end(), values->begin(), values->end());
      }
    });
  }

  template <typename Callable>
  void ForEachShard(ThreadPool& pool, Callable callable) {
    CHECK_EQ(options_.shards, shards_.size());
    Operation operation(pool);
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

  void Clear(ThreadPool& pool) {
    ForEachShard(pool, [](std::list<T>& shard) { shard.clear(); });
  }

 private:
  friend class BagIterators<T>;

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

template <typename T>
class BagIterators {
 public:
  BagIterators(Bag<T>& bag)
      : bag_(bag), iterator_shards_(bag_.shards_.size()) {}
  BagIterators(BagIterators&&) = default;

  void Add(Bag<T>::iterator it) {
    CHECK_EQ(iterator_shards_.size(), bag_.shards_.size());
    CHECK_LT(it.shard, iterator_shards_.size());

    iterator_shards_[it.shard].lock(
        [&](std::vector<typename std::list<T>::iterator>& shard) {
          shard.push_back(it.it);
        });
  }

  size_t size() const {
    size_t output = 0;
    for (const auto& s : iterator_shards_)
      output += s.lock(
          [](const std::vector<typename std::list<T>::iterator>& iterators) {
            return iterators.size();
          });
    return output;
  }

  void erase(Operation& operation) && {
    CHECK_EQ(iterator_shards_.size(), bag_.shards_.size());
    for (size_t i = 0; i < iterator_shards_.size(); i++)
      operation.Add([&bag = bag_, i,
                     iterator_shard =
                         std::move(iterator_shards_[i])]() mutable {
        iterator_shard.lock(
            [&](std::vector<typename std::list<T>::iterator>& iterators) {
              if (iterators.empty()) return;  // Optimization: avoid lock.
              bag.shards_[i].lock(
                  [&iterators](
                      language::NonNull<std::unique_ptr<std::list<T>>>& shard) {
                    TRACK_OPERATION(BagIterators_erase);
                    if (iterators.size() == shard->size()) {
                      TRACK_OPERATION(BagIterators_erase_optimized_path);
                      shard->clear();
                      return;
                    }
                    for (auto& it : iterators) shard->erase(it);
                  });
            });
      });
  }

 private:
  Bag<T>& bag_;

  std::vector<Protected<std::vector<typename std::list<T>::iterator>>>
      iterator_shards_;
};

}  // namespace afc::concurrent
#endif

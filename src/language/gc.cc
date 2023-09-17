#include "src/language/gc.h"

#include <cmath>
#include <utility>

extern "C" {
#include "execinfo.h"
}

#include "src/concurrent/operation.h"
#include "src/futures/delete_notification.h"
#include "src/infrastructure/time.h"
#include "src/infrastructure/tracker.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::language {
namespace gc {
using concurrent::Bag;
using concurrent::BagOptions;
using concurrent::ThreadPool;
using infrastructure::CountDownTimer;
using infrastructure::Tracker;
using language::NonNull;

namespace {
using ObjectMetadataBag =
    NonNull<std::unique_ptr<concurrent::Bag<std::weak_ptr<ObjectMetadata>>>>;

size_t SumContainedSizes(const std::list<ObjectMetadataBag>& container) {
  size_t output = 0;
  for (const ObjectMetadataBag& item : container) output += item->size();
  return output;
}

void PushAndClean(std::list<ObjectMetadataBag>& container,
                  ObjectMetadataBag item) {
  container.push_back(std::move(item));
  container.remove_if([](const ObjectMetadataBag& l) { return l->empty(); });
}
}  // namespace

ObjectMetadata::ObjectMetadata(ConstructorAccessKey, Pool& pool,
                               ExpandCallback expand_callback)
    : pool_(pool), data_(Data{.expand_callback = std::move(expand_callback)}) {}

ObjectMetadata::~ObjectMetadata() {
  if (container_bag_ != nullptr) container_bag_->erase(container_bag_iterator_);
}

/* static */
void ObjectMetadata::AddToBag(
    NonNull<std::shared_ptr<ObjectMetadata>> shared_this,
    concurrent::Bag<std::weak_ptr<ObjectMetadata>>& bag) {
  CHECK(shared_this->container_bag_ == nullptr);
  shared_this->container_bag_iterator_ = bag.Add(shared_this.get_shared());
  shared_this->container_bag_ = &bag;
}

void ObjectMetadata::Orphan() {
  CHECK(container_bag_ != nullptr);
  container_bag_ = nullptr;
}

Pool& ObjectMetadata::pool() const { return pool_; }

bool ObjectMetadata::IsAlive() const {
  return data_.lock(
      [](const Data& data) { return data.expand_callback != nullptr; });
}

Pool::Pool(Options options)
    : options_([&] {
        if (options.thread_pool == nullptr)
          options.thread_pool = std::make_shared<ThreadPool>(8, nullptr);
        return std::move(options);
      }()),
      async_work_(*options_.thread_pool) {}

Pool::~Pool() {
  FullCollect();
  async_work_.BlockUntilDone();
  data_.lock([](const Data& data) {
    CHECK(data.expand_list.empty());
    // TODO(gc, 2022-12-08): Enable this validation.
#if 0
    CHECK(data.roots_list.empty())
        << "Found roots (start: "
        << data.roots_list.front()->front().lock() << ")";
#endif
  });
}

void Pool::BlockUntilDone() const { async_work_.BlockUntilDone(); }

size_t Pool::count_objects() const {
  return eden_.lock([](const Eden& eden) {
    return eden.object_metadata->size();
  }) + data_.lock([](const Data& data) {
    return SumContainedSizes(data.object_metadata_list);
  });
}

Pool::CollectOutput Pool::Collect() { return Collect(false); }

Pool::FullCollectStats Pool::FullCollect() {
  return std::get<FullCollectStats>(Collect(true));
}

Pool::CollectOutput Pool::Collect(bool full) {
  TRACK_OPERATION(gc_Pool_Collect);

  std::optional<CountDownTimer> timer;

  size_t data_size = data_.lock([&](const Data& data) {
    return SumContainedSizes(data.object_metadata_list);
  });

  LightCollectStats light_stats;
  std::optional<Eden> eden = eden_.lock([&](Eden& eden_data)
                                            -> std::optional<Eden> {
    if (!full) {
      if (options_.collect_duration_threshold.has_value())
        timer = CountDownTimer(
            std::exp2(eden_data.consecutive_unfinished_collect_calls) *
            options_.collect_duration_threshold.value());

      if (eden_data.expand_list == std::nullopt) {
        size_t max_metadata_size = std::max(1024ul, data_size);
        light_stats.begin_eden_size = eden_data.object_metadata->size();
        if (eden_data.object_metadata->size() > max_metadata_size) {
          VLOG(3) << "CleanEden starts: " << eden_data.object_metadata->size();
          eden_data.object_metadata->remove_if(
              concurrent::Operation(*options_.thread_pool, std::nullopt,
                                    INLINE_TRACKER(gc_Pool_Collect_CleanEden)),
              [](const std::weak_ptr<ObjectMetadata>& object_metadata) {
                return object_metadata.expired();
              });
          VLOG(4) << "CleanEden ends: " << eden_data.object_metadata->size();
        }
        light_stats.end_eden_size = eden_data.object_metadata->size();
        if (eden_data.object_metadata->size() <= max_metadata_size) {
          eden_data.consecutive_unfinished_collect_calls = 0;
          return std::nullopt;
        }
      }
    }
    return std::exchange(
        eden_data, Eden::NewWithExpandList(
                       eden_data.consecutive_unfinished_collect_calls + 1));
  });

  if (eden == std::nullopt) {
    CHECK(!full);
    return light_stats;
  }

  // We collect expired objects here and explicitly delete them once we have
  // unlocked data_. We don't need high parallelism here (the only concurrent
  // operation is adding vectors of objects), so 4 shards are good enough.
  Bag<std::vector<ObjectMetadata::ExpandCallback>> expired_objects_callbacks(
      BagOptions{.shards = 4});

  FullCollectStats stats;
  if (!data_.lock([&](Data& data) {
        while (eden.has_value()) {
          VLOG(3) << "Starting with generations: " << data.roots_list.size();
          stats.eden_size = eden->object_metadata->size();
          ConsumeEden(std::move(*eden), data);

          stats.generations = data.roots_list.size();
          stats.begin_total = SumContainedSizes(data.object_metadata_list);
          stats.roots = SumContainedSizes(data.roots_list);

          ScheduleExpandRoots(concurrent::Operation(
                                  *options_.thread_pool, std::nullopt,
                                  INLINE_TRACKER(gc_Pool_ScheduleExpandRoots)),
                              data.roots_list, data.expand_list);
          VLOG(5) << "Roots registered: " << data.expand_list.size();

          Expand(concurrent::Operation(*options_.thread_pool, std::nullopt,
                                       INLINE_TRACKER(gc_Pool_Expand)),
                 data.expand_list, timer);
          if (!data.expand_list.empty()) {
            VLOG(3) << "Expansion didn't finish. Interrupting.";
            return false;
          }

          eden = eden_.lock([&](Eden& eden_data) -> std::optional<Eden> {
            if (eden_data.IsEmpty()) {
              VLOG(4) << "New eden is empty. We've reached all objects at this "
                         "point. We no longer need to keep an expand_list.";
              eden_data.expand_list = std::nullopt;
              eden_data.consecutive_unfinished_collect_calls = 0;
              return std::nullopt;
            }

            TRACK_OPERATION(gc_Pool_Collect_EdenChanged);
            return std::exchange(
                eden_data, Eden::NewWithExpandList(
                               eden_data.consecutive_unfinished_collect_calls));
          });
        }

        RemoveUnreachable(
            concurrent::Operation(*options_.thread_pool, std::nullopt,
                                  INLINE_TRACKER(gc_Pool_RemoveUnreachable)),
            data.object_metadata_list, expired_objects_callbacks);

        stats.end_total = SumContainedSizes(data.object_metadata_list);
        VLOG(3) << "Data: " << stats.end_total;
        return true;
      })) {
    TRACK_OPERATION(gc_Pool_Collect_Interrupted);
    CHECK(!full);
    CHECK(expired_objects_callbacks.empty());
    return UnfinishedCollectStats();
  }

  async_work_.Add([callbacks = std::move(expired_objects_callbacks)] {
    VLOG(3) << "Allowing lost object to be deleted: " << callbacks.size();
  });

  LOG(INFO) << "Garbage collection results: " << stats;
  return stats;
}

void Pool::ConsumeEden(Eden eden, Data& data) {
  TRACK_OPERATION(gc_Pool_ConsumeEden);

  VLOG(3) << "Removing empty lists of roots.";
  PushAndClean(data.roots_list, std::move(eden.roots));
  PushAndClean(data.object_metadata_list, std::move(eden.object_metadata));

  if (eden.expand_list.has_value()) {
    TRACK_OPERATION(gc_Pool_ConsumeEden_expand_list);
    data.expand_list.Add(std::move(eden.expand_list.value()));
  }
}

bool Pool::Eden::IsEmpty() const {
  return roots->empty() && object_metadata->empty() &&
         (expand_list == std::nullopt || expand_list->empty());
}

/* static */ void Pool::ScheduleExpandRoots(
    const concurrent::Operation& parallel_operation,
    const std::list<ObjectMetadataBag>& roots_list,
    concurrent::Bag<ObjectExpandList>& expand_list) {
  VLOG(3) << "Registering roots: " << roots_list.size();
  for (const ObjectMetadataBag& roots : roots_list)
    roots->ForEachShard(
        parallel_operation,
        [&expand_list](const std::list<std::weak_ptr<ObjectMetadata>>& shard) {
          ObjectExpandList local_expand_list;
          for (const std::weak_ptr<ObjectMetadata>& root_weak : shard)
            VisitPointer(
                root_weak,
                [&local_expand_list](
                    NonNull<std::shared_ptr<ObjectMetadata>> obj) {
                  if (!IsExpandAlreadyScheduled(obj))
                    local_expand_list.push_back(std::move(obj));
                },
                [] { LOG(FATAL) << "Root was dead. Should never happen."; });
          if (!local_expand_list.empty())
            expand_list.Add(std::move(local_expand_list));
        });
}

/* static */ bool Pool::IsExpandAlreadyScheduled(
    const NonNull<std::shared_ptr<ObjectMetadata>>& object) {
  return object->data_.lock([](ObjectMetadata::Data& data) {
    switch (data.expand_state) {
      case ObjectMetadata::ExpandState::kDone:
      case ObjectMetadata::ExpandState::kScheduled:
        return true;
      case ObjectMetadata::ExpandState::kUnreached:
        data.expand_state = ObjectMetadata::ExpandState::kScheduled;
        return false;
    }
    LOG(FATAL) << "Invalid state";
    return false;
  });
}

/* static */
void Pool::Expand(const concurrent::Operation& parallel_operation,
                  concurrent::Bag<ObjectExpandList>& expand_list,
                  const std::optional<CountDownTimer>& count_down_timer) {
  VLOG(3) << "Starting recursive expand (expand_list: " << expand_list.size()
          << ")";

  expand_list.ForEachShard(
      parallel_operation,
      [&count_down_timer](std::list<ObjectExpandList>& shard) {
        TRACK_OPERATION(gc_Pool_Expand_shard);
        while (!shard.empty() &&
               !(count_down_timer.has_value() && count_down_timer->IsDone())) {
          for (NonNull<std::shared_ptr<ObjectMetadata>>& obj : shard.front()) {
            TRACK_OPERATION(gc_Pool_Expand_Step);
            VLOG(5) << "Considering obj: " << obj.get_shared();
            auto expansion = obj->data_.lock(
                [&](ObjectMetadata::Data& object_data)
                    -> std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> {
                  CHECK(object_data.expand_callback != nullptr);
                  switch (object_data.expand_state) {
                    case ObjectMetadata::ExpandState::kDone:
                      return {};
                    case ObjectMetadata::ExpandState::kScheduled: {
                      object_data.expand_state =
                          ObjectMetadata::ExpandState::kDone;
                      TRACK_OPERATION(gc_Pool_Expand_Step_call);
                      return object_data.expand_callback();
                    }
                    case ObjectMetadata::ExpandState::kUnreached:
                      LOG(FATAL) << "Invalid state.";
                  }
                  LOG(FATAL) << "Invalid state.";
                  return {};
                });
            VLOG(6) << "Installing expansion of " << obj.get_shared() << ": "
                    << expansion.size();
            // TODO(easy, 2023-09-15): Align the use of std::list and
            // std::vector between ObjectExpandList and related class, and the
            // return value of expand_callback; that should allow us to avoid
            // having to convert here.
            ObjectExpandList expansion_list(expansion.begin(), expansion.end());
            expansion_list.remove_if(IsExpandAlreadyScheduled);
            if (!expansion.empty()) shard.push_back(std::move(expansion_list));
          }
          shard.pop_front();
        }
      });
}

void Pool::RemoveUnreachable(const concurrent::Operation& parallel_operation,
                             std::list<ObjectMetadataBag>& object_metadata_list,
                             Bag<std::vector<ObjectMetadata::ExpandCallback>>&
                                 expired_objects_callbacks) {
  VLOG(3) << "Building survivor list.";

  // TODO(gc, 2022-12-03): Add a timer and find a way to allow this function
  // to be interrupted.
  for (ObjectMetadataBag& sublist : object_metadata_list)
    sublist->ForEachShard(parallel_operation, [&expired_objects_callbacks](
                                                  std::list<std::weak_ptr<
                                                      ObjectMetadata>>& shard) {
      std::vector<ObjectMetadata::ExpandCallback>
          local_expired_objects_callbacks;
      shard.remove_if([&local_expired_objects_callbacks](
                          const std::weak_ptr<ObjectMetadata>& obj_weak) {
        return VisitPointer(
            obj_weak,
            [&](NonNull<std::shared_ptr<ObjectMetadata>> obj) -> bool {
              return obj->data_.lock([&](ObjectMetadata::Data& object_data) {
                switch (object_data.expand_state) {
                  case ObjectMetadata::ExpandState::kUnreached:
                    obj->Orphan();
                    local_expired_objects_callbacks.push_back(
                        std::move(object_data.expand_callback));
                    return true;
                  case ObjectMetadata::ExpandState::kDone:
                    object_data.expand_state =
                        ObjectMetadata::ExpandState::kUnreached;
                    return false;
                  case ObjectMetadata::ExpandState::kScheduled:
                    LOG(FATAL)
                        << "Invalid State: Removing unreachable objects while "
                           "some objects are scheduled for expansion.";
                }
                LOG(FATAL) << "Unhandled case.";
                return false;
              });
            },
            []() -> bool {
              // The object should handle its own removal. Maybe we lost a race.
              return false;
            });
      });
      if (!local_expired_objects_callbacks.empty())
        expired_objects_callbacks.Add(
            std::move(local_expired_objects_callbacks));
    });
  VLOG(4) << "Done building survivor list: "
          << SumContainedSizes(object_metadata_list);
}

Pool::RootRegistration Pool::AddRoot(
    std::weak_ptr<ObjectMetadata> object_metadata) {
  bool* ptr = new bool(false);
  VLOG(5) << "Adding root: " << object_metadata.lock() << " at " << ptr;
  if (VLOG_IS_ON(10)) {
    static const size_t kBufferSize = 128;
    void* buffer[kBufferSize];
    int nptrs = backtrace(buffer, kBufferSize);
    VLOG(10) << "backtrace():";
    CHECK_GE(nptrs, 0);
    char** strings = backtrace_symbols(buffer, nptrs);
    CHECK(strings != nullptr);
    for (size_t i = 0; i < static_cast<size_t>(nptrs); i++)
      VLOG(10) << "  " << strings[i];
    free(strings);
  }
  return eden_.lock([&](Eden& eden) {
    return RootRegistration(
        ptr, [&roots_list = eden.roots.value(),
              it = eden.roots->Add(object_metadata)](bool* value) {
          delete value;
          VLOG(5) << "Erasing root: " << value;
          roots_list.erase(it);
        });
  });
}

void Pool::AddToEdenExpandList(
    language::NonNull<std::shared_ptr<ObjectMetadata>> object_metadata) {
  eden_.lock([&](Eden& eden) {
    if (eden.expand_list.has_value() &&
        !IsExpandAlreadyScheduled(object_metadata))
      eden.expand_list->push_back(std::move(object_metadata));
  });
}

language::NonNull<std::shared_ptr<ObjectMetadata>> Pool::NewObjectMetadata(
    ObjectMetadata::ExpandCallback expand_callback) {
  language::NonNull<std::shared_ptr<ObjectMetadata>> object_metadata =
      MakeNonNullShared<ObjectMetadata>(ObjectMetadata::ConstructorAccessKey(),
                                        *this, std::move(expand_callback));
  eden_.lock([&](Eden& eden) {
    ObjectMetadata::AddToBag(object_metadata, eden.object_metadata.value());
    VLOG(5) << "Adding object: " << object_metadata.get_shared()
            << " (eden total: " << eden.object_metadata->size() << ")";
  });
  return object_metadata;
}

/* static */ Pool::Eden Pool::Eden::NewWithExpandList(
    size_t consecutive_unfinished_collect_calls) {
  return Eden{.consecutive_unfinished_collect_calls =
                  consecutive_unfinished_collect_calls};
}

std::ostream& operator<<(std::ostream& os,
                         const Pool::FullCollectStats& stats) {
  os << "[roots: " << stats.roots << ", begin_total: " << stats.begin_total
     << ", end_total: " << stats.end_total
     << ", generations: " << stats.generations << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const Pool::LightCollectStats& stats) {
  os << "[begin_eden_size: " << stats.begin_eden_size
     << ", end_eden_size: " << stats.end_eden_size << "]";
  return os;
}

}  // namespace gc

namespace {
using futures::DeleteNotification;
using gc::Pool;

struct Node {
  ~Node() { VLOG(5) << "Deleting Node: " << this; }

  std::vector<gc::Ptr<Node>> children;

  DeleteNotification delete_notification;
};
}  // namespace

namespace gc {
std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> Expand(const Node& node) {
  std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> output;
  for (auto& child : node.children) {
    output.push_back(child.object_metadata());
  }
  VLOG(5) << "Generated expansion of node " << &node << ": " << output.size();
  return output;
}
}  // namespace gc

namespace {
gc::Root<Node> MakeLoop(gc::Pool& pool, int size) {
  gc::Root<Node> start = pool.NewRoot(MakeNonNullUnique<Node>());
  gc::Ptr<Node> last = start.ptr();
  for (int i = 1; i < size; i++) {
    gc::Root<Node> child = pool.NewRoot(MakeNonNullUnique<Node>());
    last->children.push_back(child.ptr());
    last = last->children.back();
  }
  last->children.push_back(start.ptr());
  return start;
}

bool tests_registration = tests::Register(
    L"GC",
    {{.name = L"CollectOnEmpty",
      .callback =
          [] {
            Pool::LightCollectStats stats =
                std::get<Pool::LightCollectStats>(gc::Pool({}).Collect());
            CHECK_EQ(stats.begin_eden_size, 0ul);
            CHECK_EQ(stats.end_eden_size, 0ul);
          }},
     {.name = L"PreservesRoots",
      .callback =
          [] {
            gc::Pool pool({});
            DeleteNotification::Value delete_notification = [&pool] {
              auto root = pool.NewRoot(MakeNonNullUnique<Node>());
              auto output =
                  root.ptr().value().delete_notification.listenable_value();
              pool.Collect();
              pool.BlockUntilDone();
              CHECK(!output.has_value());
              return output;
            }();
            CHECK(delete_notification.has_value());
          }},
     {.name = L"RootAssignment",
      .callback =
          [] {
            gc::Pool pool({});
            DeleteNotification::Value delete_notification = [&pool] {
              auto root = pool.NewRoot(MakeNonNullUnique<Node>());
              auto delete_notification_0 =
                  root.ptr()->delete_notification.listenable_value();
              pool.Collect();
              pool.BlockUntilDone();
              CHECK_EQ(pool.count_objects(), 1ul);

              CHECK(!delete_notification_0.has_value());

              LOG(INFO) << "Overriding root.";
              {
                auto other_root = pool.NewRoot(MakeNonNullUnique<Node>());
                CHECK_EQ(pool.count_objects(), 2ul);
                root = std::move(other_root);
                CHECK_EQ(pool.count_objects(), 2ul);
              }
              CHECK_EQ(pool.count_objects(), 1ul);

              auto delete_notification_1 =
                  root.ptr()->delete_notification.listenable_value();

              CHECK(delete_notification_0.has_value());
              CHECK(!delete_notification_1.has_value());

              LOG(INFO) << "Start collect.";
              auto stats = pool.FullCollect();
              CHECK_EQ(stats.begin_total, 1ul);
              CHECK_EQ(stats.roots, 1ul);
              CHECK_EQ(stats.end_total, 1ul);

              pool.BlockUntilDone();
              CHECK(delete_notification_0.has_value());
              CHECK(!delete_notification_1.has_value());

              return delete_notification_1;
            }();
            CHECK(delete_notification.has_value());

            LOG(INFO) << "Start 2nd collect.";
            Pool::FullCollectStats stats = pool.FullCollect();
            CHECK_EQ(stats.begin_total, 0ul);
            CHECK_EQ(stats.roots, 0ul);
            CHECK_EQ(stats.end_total, 0ul);
            LOG(INFO) << "Done.";
          }},
     {.name = L"BreakLoop",
      .runs = 50,
      .callback =
          [] {
            gc::Pool pool({});
            DeleteNotification::Value delete_notification = [&pool] {
              gc::Root<Node> root = pool.NewRoot(MakeNonNullUnique<Node>());
              auto delete_notification_0 =
                  root.ptr()->delete_notification.listenable_value();
              pool.Collect();
              pool.BlockUntilDone();
              CHECK(!delete_notification_0.has_value());

              auto child_notification = [&] {
                VLOG(5) << "Creating child.";
                gc::Ptr<Node> child =
                    pool.NewRoot(MakeNonNullUnique<Node>()).ptr();

                VLOG(5) << "Storing root in child.";
                child->children.push_back(root.ptr());
                CHECK_EQ(&child->children[0].value(), &root.ptr().value());

                VLOG(5) << "Storing child in root.";
                root.ptr()->children.push_back(child);

                VLOG(5) << "Returning (deleting child pointer).";
                return child->delete_notification.listenable_value();
              }();

              CHECK(!delete_notification_0.has_value());
              CHECK(!child_notification.has_value());

              VLOG(5) << "Trigger collect.";
              pool.Collect();
              pool.BlockUntilDone();

              CHECK(!delete_notification_0.has_value());
              CHECK(!child_notification.has_value());

              VLOG(5) << "Override root value.";
              root = pool.NewRoot(MakeNonNullUnique<Node>());

              auto delete_notification_1 =
                  root.ptr()->delete_notification.listenable_value();

              CHECK(!child_notification.has_value());
              CHECK(!delete_notification_0.has_value());
              CHECK(!delete_notification_1.has_value());

              pool.FullCollect();
              pool.BlockUntilDone();

              CHECK(child_notification.has_value());
              CHECK(delete_notification_0.has_value());
              CHECK(!delete_notification_1.has_value());

              return delete_notification_1;
            }();
            CHECK(delete_notification.has_value());
          }},
     {.name = L"RootsReplaceLoop",
      .callback =
          [] {
            gc::Pool pool({});
            gc::Root root = MakeLoop(pool, 10);
            auto old_notification =
                root.ptr()->delete_notification.listenable_value();

            {
              auto stats = pool.FullCollect();
              CHECK_EQ(stats.begin_total, 10ul);
              CHECK_EQ(stats.end_total, 10ul);
              pool.BlockUntilDone();
              CHECK(!old_notification.has_value());
            }

            VLOG(5) << "Replacing loop.";
            root = MakeLoop(pool, 5);
            CHECK(!old_notification.has_value());
            {
              auto stats = pool.FullCollect();
              CHECK_EQ(stats.begin_total, 15ul);
              CHECK_EQ(stats.end_total, 5ul);
            }
          }},
     {.name = L"BreakLoopHalfway",
      .callback =
          [] {
            gc::Pool pool({});
            gc::Root<Node> root = MakeLoop(pool, 7);
            {
              gc::Ptr<Node> split = root.ptr();
              for (int i = 0; i < 4; i++) split = split->children[0];
              auto notification =
                  split->children[0]->delete_notification.listenable_value();
              CHECK(!notification.has_value());
              CHECK_EQ(pool.count_objects(), 7ul);
              split->children.clear();
              CHECK_EQ(pool.count_objects(), 5ul);
              CHECK(notification.has_value());
            }
            CHECK_EQ(pool.count_objects(), 5ul);
            CHECK(!root.ptr()
                       ->delete_notification.listenable_value()
                       .has_value());
            Pool::FullCollectStats stats = pool.FullCollect();
            pool.BlockUntilDone();
            CHECK_EQ(stats.begin_total, 5ul);
            CHECK_EQ(stats.roots, 1ul);
            CHECK_EQ(stats.end_total, 5ul);
          }},
     {.name = L"WeakPtrNoRefs",
      .callback =
          [] {
            gc::Pool pool({});
            std::optional<gc::Root<Node>> root = MakeLoop(pool, 7);
            gc::WeakPtr<Node> weak_ptr = root->ptr().ToWeakPtr();

            pool.FullCollect();
            pool.BlockUntilDone();
            CHECK(weak_ptr.Lock().has_value());

            root = std::nullopt;
            pool.FullCollect();
            pool.BlockUntilDone();
            CHECK(!weak_ptr.Lock().has_value());
          }},
     {.name = L"WeakPtrWithPtrRef", .callback = [] {
        gc::Pool pool({});
        std::optional<gc::Root<Node>> root = MakeLoop(pool, 7);
        gc::Ptr<Node> ptr = root->ptr();
        gc::WeakPtr<Node> weak_ptr = ptr.ToWeakPtr();

        pool.FullCollect();
        pool.BlockUntilDone();
        CHECK(weak_ptr.Lock().has_value());

        root = std::nullopt;
        pool.FullCollect();
        pool.BlockUntilDone();
        CHECK(!weak_ptr.Lock().has_value());
      }}});

bool full_vs_light_collect_tests_registration = tests::Register(
    L"GC::FullVsLightCollect",
    {
        {.name = L"OnEmpty",
         .callback =
             [] { std::get<Pool::LightCollectStats>(gc::Pool({}).Collect()); }},
        {.name = L"FullOnEmpty",
         .callback = [] { gc::Pool({}).FullCollect(); }},
        {.name = L"NotAfterAHundred",
         .callback =
             [] {
               gc::Pool pool({});
               MakeLoop(pool, 100);
               std::get<Pool::LightCollectStats>(pool.Collect());
             }},
        {.name = L"YesAfterEnough",
         .callback =
             [] {
               gc::Pool pool({});
               std::optional<gc::Root<Node>> obj_0 = MakeLoop(pool, 500);
               CHECK_EQ(pool.count_objects(), 500ul);

               pool.Collect();
               CHECK_EQ(pool.count_objects(), 500ul);

               std::optional<gc::Root<Node>> obj_1 = MakeLoop(pool, 500);
               CHECK_EQ(pool.count_objects(), 1000ul);

               pool.Collect();
               CHECK_EQ(pool.count_objects(), 1000ul);

               obj_0 = std::nullopt;
               obj_1 = std::nullopt;

               pool.Collect();
               CHECK_EQ(pool.count_objects(), 1000ul);

               MakeLoop(pool, 1000);
               CHECK_EQ(pool.count_objects(), 2000ul);

               pool.Collect();
               CHECK_EQ(pool.count_objects(), 0ul);
             }},
        {.name = L"LightAfterCollect",
         .callback =
             [] {
               gc::Pool pool({});
               MakeLoop(pool, 1000);
               MakeLoop(pool, 1000);
               CHECK_EQ(pool.count_objects(), 2000ul);
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 0ul);
             }},
        {.name = L"LightAfterCollectBeforeFills",
         .callback =
             [] {
               gc::Pool pool({});
               MakeLoop(pool, 1000);
               MakeLoop(pool, 1000);
               CHECK_EQ(pool.count_objects(), 2000ul);

               pool.Collect();
               CHECK_EQ(pool.count_objects(), 0ul);

               MakeLoop(pool, 500);
               CHECK_EQ(pool.count_objects(), 500ul);

               pool.Collect();
               CHECK_EQ(pool.count_objects(), 500ul);

               MakeLoop(pool, 1000);
               CHECK_EQ(pool.count_objects(), 1500ul);

               pool.Collect();
               CHECK_EQ(pool.count_objects(), 0ul);
             }},
        {.name = L"SomeSurvivingObjects",
         .callback =
             [] {
               gc::Pool pool({});
               std::optional<gc::Root<Node>> root = MakeLoop(pool, 2048);
               MakeLoop(pool, 1000);
               CHECK_EQ(pool.count_objects(), 3048ul);

               pool.Collect();
               CHECK_EQ(pool.count_objects(), 2048ul);

               MakeLoop(pool, 1024);
               CHECK_EQ(pool.count_objects(), 3072ul);

               pool.Collect();
               CHECK_EQ(pool.count_objects(), 3072ul);

               MakeLoop(pool, 1024 + 4);
               root = std::nullopt;

               pool.Collect();
               CHECK_EQ(pool.count_objects(), 0ul);

               MakeLoop(pool, 500);
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 500ul);
             }},
        {.name = L"LargeTest",
         .callback =
             [] {
               gc::Pool pool({});
               std::optional<gc::Root<Node>> root_big = MakeLoop(pool, 8000);
               std::optional<gc::Root<Node>> root_small = MakeLoop(pool, 2000);
               CHECK_EQ(pool.count_objects(), 10000ul);
               pool.Collect();

               MakeLoop(pool, 11000);
               CHECK_EQ(pool.count_objects(), 21000ul);
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 10000ul);

               MakeLoop(pool, 9000);
               CHECK_EQ(pool.count_objects(), 19000ul);
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 19000ul);

               MakeLoop(pool, 2000);
               CHECK_EQ(pool.count_objects(), 21000ul);
               root_big = std::nullopt;
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 2000ul);

               MakeLoop(pool, 100);
               CHECK_EQ(pool.count_objects(), 2100ul);
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 2100ul);
               pool.FullCollect();
               CHECK_EQ(pool.count_objects(), 2000ul);

               MakeLoop(pool, 1000);
               CHECK_EQ(pool.count_objects(), 3000ul);
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 3000ul);
               MakeLoop(pool, 1000);
               CHECK_EQ(pool.count_objects(), 4000ul);
               MakeLoop(pool, 10);
               CHECK_EQ(pool.count_objects(), 4010ul);
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 2000ul);
             }},
    });

}  // namespace
}  // namespace afc::language

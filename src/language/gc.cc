#include "src/language/gc.h"

#include <utility>

#include "src/futures/delete_notification.h"
#include "src/infrastructure/time.h"
#include "src/infrastructure/tracker.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::language {
namespace gc {
using infrastructure::CountDownTimer;
using infrastructure::Tracker;
using language::NonNull;

ObjectMetadata::ObjectMetadata(ConstructorAccessKey, Pool& pool,
                               ExpandCallback expand_callback)
    : pool_(pool), data_(Data{.expand_callback = std::move(expand_callback)}) {}

Pool& ObjectMetadata::pool() const { return pool_; }

bool ObjectMetadata::IsAlive() const {
  return data_.lock(
      [](const Data& data) { return data.expand_callback != nullptr; });
}

Pool::~Pool() {
  // TODO(gc, 2022-05-11): Enable this validation:
  // CHECK(roots_.empty());
  FullCollect();
}

size_t Pool::count_objects() const {
  return eden_.lock([](const Eden& eden) {
    return eden.object_metadata.size();
  }) + survivors_.lock([](const Survivors& survivors) {
    return survivors.object_metadata.size();
  });
}

Pool::CollectOutput Pool::Collect() { return Collect(false); }

Pool::FullCollectStats Pool::FullCollect() {
  return std::get<FullCollectStats>(Collect(true));
}

Pool::CollectOutput Pool::Collect(bool full) {
  TRACK_OPERATION(gc_Pool_Collect);

  size_t survivors_size = survivors_.lock([&](const Survivors& survivors) {
    return survivors.object_metadata.size();
  });

  LightCollectStats light_stats;
  std::optional<Eden> eden =
      eden_.lock([&](Eden& eden_data) -> std::optional<Eden> {
        if (!full && eden_data.expand_list == std::nullopt) {
          auto done = [&] {
            return eden_data.object_metadata.size() +
                       eden_data.roots_deleted.size() <=
                   std::max(1024ul, survivors_size);
          };
          light_stats.begin_eden_size = eden_data.object_metadata.size();
          if (!done()) {
            TRACK_OPERATION(gc_Pool_Collect_CleanEden);
            VLOG(3) << "CleanEden starts: " << eden_data.object_metadata.size();
            eden_data.object_metadata.remove_if(
                [](std::weak_ptr<ObjectMetadata>& object_metadata) {
                  return object_metadata.expired();
                });
            VLOG(4) << "CleanEden ends: " << eden_data.object_metadata.size();
          }
          light_stats.end_eden_size = eden_data.object_metadata.size();
          if (done()) return std::nullopt;
        }

        return std::exchange(eden_data, Eden::NewWithExpandList());
      });

  if (eden == std::nullopt) {
    CHECK(!full);
    return light_stats;
  }

  // We collect expired objects here and explicitly delete them once we have
  // unlocked data_.
  std::vector<ObjectMetadata::ExpandCallback> expired_objects_callbacks;

  FullCollectStats stats;
  if (!survivors_.lock([&](Survivors& survivors) {
        while (eden.has_value()) {
          VLOG(3) << "Starting with generations: " << survivors.roots.size();
          ConsumeEden(std::move(*eden), survivors);

          stats.generations = survivors.roots.size();
          stats.begin_total = survivors.object_metadata.size();
          stats.eden_size = eden->object_metadata.size();

          for (const NonNull<std::unique_ptr<ObjectMetadataList>>& l :
               survivors.roots)
            stats.roots += l->size();

          ScheduleExpandRoots(survivors);
          Expand(survivors,
                 full ? std::optional<double>() : std::make_optional(0.05));
          if (!survivors.expand_list.empty()) {
            VLOG(3) << "Expansion didn't finish. Interrupting.";
            return false;
          }

          eden = eden_.lock([&](Eden& eden_data) -> std::optional<Eden> {
            if (IsEmpty(eden_data)) {
              VLOG(4) << "New eden is empty.";
              UpdateSurvivorsList(survivors, expired_objects_callbacks);
              eden_data.expand_list = std::nullopt;
              return std::nullopt;
            }

            TRACK_OPERATION(gc_Pool_Collect_EdenChanged);
            return std::exchange(eden_data, Eden::NewWithExpandList());
          });
        }
        stats.end_total = survivors.object_metadata.size();
        VLOG(3) << "Survivors: " << stats.end_total;
        return true;
      })) {
    TRACK_OPERATION(gc_Pool_Collect_Interrupted);
    CHECK(!full);
    CHECK(expired_objects_callbacks.empty());
    return UnfinishedCollectStats();
  }

  VLOG(3) << "Allowing lost object to be deleted: "
          << expired_objects_callbacks.size();
  {
    TRACK_OPERATION(gc_Pool_Collect_DeleteLost);
    expired_objects_callbacks.clear();
  }

  LOG(INFO) << "Garbage collection results: " << stats;
  return stats;
}

void Pool::ConsumeEden(Eden eden, Survivors& survivors) {
  TRACK_OPERATION(gc_Pool_ConsumeEden);

  {
    TRACK_OPERATION(gc_Pool_ConsumeEden_roots_deleted);
    VLOG(3) << "Removing deleted roots: " << eden.roots_deleted.size();
    for (const Eden::RootIterator& d : eden.roots_deleted)
      d.roots_list.erase(d.it);
  }

  VLOG(3) << "Removing empty lists of roots.";
  survivors.roots.push_back(std::move(eden.roots));
  survivors.roots.remove_if(
      [](const NonNull<std::unique_ptr<ObjectMetadataList>>& l) {
        return l->empty();
      });

  {
    TRACK_OPERATION(gc_Pool_ConsumeEden_object_metadata);
    survivors.object_metadata.insert(survivors.object_metadata.end(),
                                     eden.object_metadata.begin(),
                                     eden.object_metadata.end());
  }

  if (eden.expand_list.has_value()) {
    TRACK_OPERATION(gc_Pool_ConsumeEden_expand_list);
    survivors.expand_list.insert(survivors.expand_list.end(),
                                 eden.expand_list->begin(),
                                 eden.expand_list->end());
  }
}

/*  static */ bool Pool::IsEmpty(const Eden& eden) {
  return eden.roots->empty() && eden.roots_deleted.empty() &&
         eden.object_metadata.empty() &&
         (eden.expand_list == std::nullopt || eden.expand_list->empty());
}

/* static */ void Pool::ScheduleExpandRoots(Survivors& survivors) {
  VLOG(3) << "Registering roots.";
  TRACK_OPERATION(gc_Pool_ScheduleExpandRoots);

  for (const NonNull<std::unique_ptr<ObjectMetadataList>>& l : survivors.roots)
    for (const std::weak_ptr<ObjectMetadata>& root_weak : l.value()) {
      VisitPointer(
          root_weak,
          std::bind_front(MaybeScheduleExpand, std::ref(survivors.expand_list)),
          [] { LOG(FATAL) << "Root was dead. Should never happen."; });
    }

  VLOG(5) << "Roots registered: " << survivors.expand_list.size();
}

/* static */ void Pool::MaybeScheduleExpand(
    ObjectExpandList& output, NonNull<std::shared_ptr<ObjectMetadata>> object) {
  if (object->data_.lock([](ObjectMetadata::Data& data) {
        switch (data.state) {
          case ObjectMetadata::State::kExpanded:
          case ObjectMetadata::State::kScheduled:
            return false;
          case ObjectMetadata::State::kLost:
            data.state = ObjectMetadata::State::kScheduled;
            return true;
        }
        LOG(FATAL) << "Invalid state";
        return false;
      }))
    output.push_back(std::move(object));
}

/* static */
void Pool::Expand(Survivors& survivors, std::optional<double> timeout) {
  VLOG(3) << "Starting recursive expand (expand_list: "
          << survivors.expand_list.size() << ")";

  TRACK_OPERATION(gc_Pool_Expand);

  std::optional<CountDownTimer> timer = timeout.has_value()
                                            ? CountDownTimer(*timeout)
                                            : std::optional<CountDownTimer>();

  while (!survivors.expand_list.empty() &&
         !(timer.has_value() && timer->IsDone())) {
    TRACK_OPERATION(gc_Pool_Expand_Step);

    auto& front = survivors.expand_list.front();
    VLOG(5) << "Considering obj: " << front.get_shared();
    auto expansion = front->data_.lock(
        [&](ObjectMetadata::Data& object_data)
            -> std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> {
          CHECK(object_data.expand_callback != nullptr);
          switch (object_data.state) {
            case ObjectMetadata::State::kExpanded:
              return {};
            case ObjectMetadata::State::kScheduled: {
              object_data.state = ObjectMetadata::State::kExpanded;
              TRACK_OPERATION(gc_Pool_Expand_Step_call);
              return object_data.expand_callback();
            }
            case ObjectMetadata::State::kLost:
              LOG(FATAL) << "Invalid state.";
          }
          LOG(FATAL) << "Invalid state.";
          return {};
        });
    VLOG(6) << "Installing expansion of " << front.get_shared() << ": "
            << expansion.size();
    survivors.expand_list.pop_front();
    for (NonNull<std::shared_ptr<ObjectMetadata>>& child : expansion) {
      MaybeScheduleExpand(survivors.expand_list, std::move(child));
    }
  }
}

/* static */ void Pool::UpdateSurvivorsList(
    Survivors& survivors,
    std::vector<ObjectMetadata::ExpandCallback>& expired_objects_callbacks) {
  VLOG(3) << "Building survivor list.";
  ObjectMetadataList surviving_objects;

  // TODO(gc, 2022-12-03): Add a timer and find a way to allow this function
  // to be interrupted.

  TRACK_OPERATION(gc_Pool_UpdateSurvivorsList);

  for (std::weak_ptr<ObjectMetadata>& obj_weak : survivors.object_metadata)
    VisitPointer(
        obj_weak,
        [&](NonNull<std::shared_ptr<ObjectMetadata>> obj) {
          obj->data_.lock([&](ObjectMetadata::Data& object_data) {
            switch (object_data.state) {
              case ObjectMetadata::State::kLost:
                expired_objects_callbacks.push_back(
                    std::move(object_data.expand_callback));
                break;
              case ObjectMetadata::State::kExpanded:
                object_data.state = ObjectMetadata::State::kLost;
                surviving_objects.push_back(obj.get_shared());
                break;
              case ObjectMetadata::State::kScheduled:
                LOG(FATAL) << "Invalid State: Adding survivors while some "
                              "objects are scheduled for expansion.";
            }
          });
        },
        [] {});
  survivors.object_metadata = std::move(surviving_objects);
  VLOG(4) << "Done building survivor list: "
          << survivors.object_metadata.size();
}

Pool::RootRegistration Pool::AddRoot(
    std::weak_ptr<ObjectMetadata> object_metadata) {
  VLOG(5) << "Adding root: " << object_metadata.lock();
  return eden_.lock([&](Eden& eden) {
    eden.roots->push_back(object_metadata);
    return RootRegistration(
        new bool(false),
        [this, root_deleted = Eden::RootIterator{
                   .roots_list = eden.roots.value(),
                   .it = std::prev(eden.roots->end())}](bool* value) {
          delete value;
          eden_.lock([&root_deleted](Eden& input_eden) {
            VLOG(5) << "Erasing root.";
            input_eden.roots_deleted.push_back(root_deleted);
          });
        });
  });
}

void Pool::AddToEdenExpandList(
    language::NonNull<std::shared_ptr<ObjectMetadata>> object_metadata) {
  eden_.lock([&](Eden& eden) {
    if (eden.expand_list.has_value())
      MaybeScheduleExpand(eden.expand_list.value(), std::move(object_metadata));
  });
}

language::NonNull<std::shared_ptr<ObjectMetadata>> Pool::NewObjectMetadata(
    ObjectMetadata::ExpandCallback expand_callback) {
  language::NonNull<std::shared_ptr<ObjectMetadata>> object_metadata =
      MakeNonNullShared<ObjectMetadata>(ObjectMetadata::ConstructorAccessKey(),
                                        *this, std::move(expand_callback));
  eden_.lock([&](Eden& eden) {
    eden.object_metadata.push_back(object_metadata.get_shared());
    VLOG(5) << "Added object: " << object_metadata.get_shared()
            << " (eden total: " << eden.object_metadata.size() << ")";
  });
  return object_metadata;
}

/* static */ Pool::Eden Pool::Eden::NewWithExpandList() {
  return Eden{.expand_list = ObjectExpandList{}};
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
                std::get<Pool::LightCollectStats>(gc::Pool().Collect());
            CHECK_EQ(stats.begin_eden_size, 0ul);
            CHECK_EQ(stats.end_eden_size, 0ul);
          }},
     {.name = L"PreservesRoots",
      .callback =
          [] {
            gc::Pool pool;
            DeleteNotification::Value delete_notification = [&pool] {
              auto root = pool.NewRoot(MakeNonNullUnique<Node>());
              auto output =
                  root.ptr().value().delete_notification.listenable_value();
              pool.Collect();
              CHECK(!output->has_value());
              return output;
            }();
            CHECK(delete_notification->has_value());
          }},
     {.name = L"RootAssignment",
      .callback =
          [] {
            gc::Pool pool;
            DeleteNotification::Value delete_notification = [&pool] {
              auto root = pool.NewRoot(MakeNonNullUnique<Node>());
              auto delete_notification_0 =
                  root.ptr()->delete_notification.listenable_value();
              pool.Collect();
              CHECK(!delete_notification_0->has_value());

              VLOG(5) << "Overriding root.";
              root = pool.NewRoot(MakeNonNullUnique<Node>());

              auto delete_notification_1 =
                  root.ptr()->delete_notification.listenable_value();

              CHECK(delete_notification_0->has_value());
              CHECK(!delete_notification_1->has_value());

              VLOG(5) << "Start collect.";
              auto stats = pool.FullCollect();
              CHECK_EQ(stats.begin_total + stats.eden_size, 2ul);
              CHECK_EQ(stats.roots, 1ul);
              CHECK_EQ(stats.end_total, 1ul);

              CHECK(delete_notification_0->has_value());
              CHECK(!delete_notification_1->has_value());

              return delete_notification_1;
            }();
            CHECK(delete_notification->has_value());

            Pool::FullCollectStats stats = pool.FullCollect();
            CHECK_EQ(stats.begin_total, 1ul);
            CHECK_EQ(stats.roots, 0ul);
            CHECK_EQ(stats.end_total, 0ul);
          }},
     {.name = L"BreakLoop",
      .callback =
          [] {
            gc::Pool pool;
            DeleteNotification::Value delete_notification = [&pool] {
              gc::Root<Node> root = pool.NewRoot(MakeNonNullUnique<Node>());
              auto delete_notification_0 =
                  root.ptr()->delete_notification.listenable_value();
              pool.Collect();
              CHECK(!delete_notification_0->has_value());

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

              CHECK(!delete_notification_0->has_value());
              CHECK(!child_notification->has_value());

              VLOG(5) << "Trigger collect.";
              pool.Collect();

              CHECK(!delete_notification_0->has_value());
              CHECK(!child_notification->has_value());

              VLOG(5) << "Override root value.";
              root = pool.NewRoot(MakeNonNullUnique<Node>());

              auto delete_notification_1 =
                  root.ptr()->delete_notification.listenable_value();

              CHECK(!child_notification->has_value());
              CHECK(!delete_notification_0->has_value());
              CHECK(!delete_notification_1->has_value());

              pool.FullCollect();

              CHECK(child_notification->has_value());
              CHECK(delete_notification_0->has_value());
              CHECK(!delete_notification_1->has_value());

              return delete_notification_1;
            }();
            CHECK(delete_notification->has_value());
          }},
     {.name = L"RootsReplaceLoop",
      .callback =
          [] {
            gc::Pool pool;
            gc::Root root = MakeLoop(pool, 10);
            auto old_notification =
                root.ptr()->delete_notification.listenable_value();

            {
              auto stats = pool.FullCollect();
              CHECK_EQ(stats.begin_total + stats.eden_size, 10ul);
              CHECK_EQ(stats.end_total, 10ul);
              CHECK(!old_notification->has_value());
            }

            VLOG(5) << "Replacing loop.";
            root = MakeLoop(pool, 5);
            CHECK(!old_notification->has_value());
            {
              auto stats = pool.FullCollect();
              CHECK_EQ(stats.begin_total + stats.eden_size, 15ul);
              CHECK_EQ(stats.end_total, 5ul);
            }
          }},
     {.name = L"BreakLoopHalfway",
      .callback =
          [] {
            gc::Pool pool;
            gc::Root<Node> root = MakeLoop(pool, 7);
            {
              gc::Ptr<Node> split = root.ptr();
              for (int i = 0; i < 4; i++) split = split->children[0];
              auto notification =
                  split->children[0]->delete_notification.listenable_value();
              CHECK(!notification->has_value());
              split->children.clear();
              CHECK(notification->has_value());
            }
            CHECK(!root.ptr()
                       ->delete_notification.listenable_value()
                       ->has_value());
            Pool::FullCollectStats stats = pool.FullCollect();
            CHECK_EQ(stats.begin_total + stats.eden_size, 7ul);
            CHECK_EQ(stats.roots, 1ul);
            CHECK_EQ(stats.end_total, 5ul);
          }},
     {.name = L"WeakPtrNoRefs",
      .callback =
          [] {
            gc::Pool pool;
            std::optional<gc::Root<Node>> root = MakeLoop(pool, 7);
            gc::WeakPtr<Node> weak_ptr = root->ptr().ToWeakPtr();

            pool.FullCollect();
            CHECK(weak_ptr.Lock().has_value());

            root = std::nullopt;
            pool.FullCollect();
            CHECK(!weak_ptr.Lock().has_value());
          }},
     {.name = L"WeakPtrWithPtrRef", .callback = [] {
        gc::Pool pool;
        std::optional<gc::Root<Node>> root = MakeLoop(pool, 7);
        gc::Ptr<Node> ptr = root->ptr();
        gc::WeakPtr<Node> weak_ptr = ptr.ToWeakPtr();

        pool.FullCollect();
        CHECK(weak_ptr.Lock().has_value());

        root = std::nullopt;
        pool.FullCollect();
        CHECK(!weak_ptr.Lock().has_value());
      }}});

bool full_vs_light_collect_tests_registration = tests::Register(
    L"GC::FullVsLightCollect",
    {
        {.name = L"OnEmpty",
         .callback =
             [] { std::get<Pool::LightCollectStats>(gc::Pool().Collect()); }},
        {.name = L"FullOnEmpty", .callback = [] { gc::Pool().FullCollect(); }},
        {.name = L"NotAfterAHundred",
         .callback =
             [] {
               gc::Pool pool;
               MakeLoop(pool, 100);
               std::get<Pool::LightCollectStats>(pool.Collect());
             }},
        {.name = L"YesAfterEnough",
         .callback =
             [] {
               gc::Pool pool;
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
               gc::Pool pool;
               MakeLoop(pool, 1000);
               MakeLoop(pool, 1000);
               CHECK_EQ(pool.count_objects(), 2000ul);
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 0ul);
             }},
        {.name = L"LightAfterCollectBeforeFills",
         .callback =
             [] {
               gc::Pool pool;
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
               gc::Pool pool;
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
               gc::Pool pool;
               std::optional<gc::Root<Node>> root_big = MakeLoop(pool, 8000);
               std::optional<gc::Root<Node>> root_small = MakeLoop(pool, 2000);
               CHECK_EQ(pool.count_objects(), 10000ul);
               pool.Collect();

               MakeLoop(pool, 9000);
               CHECK_EQ(pool.count_objects(), 19000ul);
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 10000ul);

               MakeLoop(pool, 2000);
               CHECK_EQ(pool.count_objects(), 12000ul);
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 12000ul);

               MakeLoop(pool, 11000);
               CHECK_EQ(pool.count_objects(), 23000ul);
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
               pool.Collect();
               CHECK_EQ(pool.count_objects(), 2000ul);
             }},
    });

}  // namespace
}  // namespace afc::language

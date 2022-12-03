#include "src/language/gc.h"

#include <utility>

#include "src/futures/delete_notification.h"
#include "src/infrastructure/tracker.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::language {
namespace gc {
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
  FullReclaim();
}

std::variant<Pool::FullReclaimStats, Pool::LightReclaimStats> Pool::Reclaim() {
  return Reclaim(false);
}

Pool::FullReclaimStats Pool::FullReclaim() {
  return std::get<FullReclaimStats>(Reclaim(true));
}

std::variant<Pool::FullReclaimStats, Pool::LightReclaimStats> Pool::Reclaim(
    bool full) {
  static Tracker tracker(L"gc::Pool::Reclaim");
  auto call = tracker.Call();

  size_t survivors_size = survivors_.lock([&](const Survivors& survivors) {
    return survivors.object_metadata.size();
  });

  LightReclaimStats light_stats;
  std::optional<Eden> eden =
      eden_.lock([&](Eden& eden_data) -> std::optional<Eden> {
        if (!full) {
          static Tracker clean_tracker(L"gc::Pool::CleanEden");
          auto clean_call = clean_tracker.Call();
          LOG(INFO) << "CleanEden starts with size: "
                    << eden_data.object_metadata.size();
          light_stats.begin_eden_size = eden_data.object_metadata.size();
          eden_data.object_metadata.remove_if(
              [](std::weak_ptr<ObjectMetadata>& object_metadata) {
                return object_metadata.expired();
              });
          light_stats.end_eden_size = eden_data.object_metadata.size();
          LOG(INFO) << "CleanEden ends with size: "
                    << eden_data.object_metadata.size();
          if (eden_data.object_metadata.size() <=
              std::max(1024ul, survivors_size))
            return std::nullopt;
        }
        return std::exchange(eden_data, Eden());
      });

  if (eden == std::nullopt) {
    return light_stats;
  }

  // We collect expired objects here and explicitly delete them once we have
  // unlocked data_.
  std::vector<ObjectMetadata::ExpandCallback> expired_objects_callbacks;

  FullReclaimStats stats;
  survivors_.lock([&](Survivors& survivors) {
    VLOG(3) << "Starting with generations: " << survivors.roots.size();
    UpdateRoots(survivors, *eden);

    stats.generations = survivors.roots.size();
    stats.begin_total = survivors.object_metadata.size();
    stats.eden_size = eden->object_metadata.size();

    for (const NonNull<std::unique_ptr<ObjectMetadataList>>& l :
         survivors.roots)
      stats.roots += l->size();

    MarkReachable(RegisterAllRoots(survivors.roots));

    VLOG(3) << "Building survivor list.";
    ObjectMetadataList object_metadata;
    AddSurvivors(std::move(eden->object_metadata), expired_objects_callbacks,
                 object_metadata);
    AddSurvivors(std::move(survivors.object_metadata),
                 expired_objects_callbacks, object_metadata);
    survivors.object_metadata = std::move(object_metadata);

    stats.end_total = survivors.object_metadata.size();
    VLOG(3) << "Survivors: " << stats.end_total;
  });

  VLOG(3) << "Allowing unreachable object to be deleted: "
          << expired_objects_callbacks.size();
  {
    static Tracker delete_tracker(L"gc::Pool::Reclaim::Delete unreachable");
    auto delete_call = delete_tracker.Call();
    expired_objects_callbacks.clear();
  }

  LOG(INFO) << "Garbage collection results: " << stats;
  return stats;
}

void Pool::UpdateRoots(Survivors& survivors, Eden& eden) {
  VLOG(3) << "Removing deleted roots: " << eden.roots_deleted.size();
  for (const Eden::RootDeleted& d : eden.roots_deleted)
    d.roots_list.erase(d.it);

  VLOG(3) << "Removing empty lists of roots.";
  survivors.roots.push_back(std::move(eden.roots));
  survivors.roots.remove_if(
      [](const NonNull<std::unique_ptr<ObjectMetadataList>>& l) {
        return l->empty();
      });
}

/* static */ void Pool::AddReachable(
    NonNull<std::shared_ptr<ObjectMetadata>> object_metadata,
    std::list<NonNull<std::shared_ptr<ObjectMetadata>>>& output) {
  if (!object_metadata->data_.lock([&](ObjectMetadata::Data& data) {
        CHECK(data.expand_callback != nullptr);
        return std::exchange(data.reached, true);
      }))
    output.push_back(std::move(object_metadata));
}

std::list<language::NonNull<std::shared_ptr<ObjectMetadata>>>
Pool::RegisterAllRoots(
    const std::list<NonNull<std::unique_ptr<ObjectMetadataList>>>&
        object_metadata) {
  VLOG(3) << "Registering roots.";
  static Tracker tracker(L"gc::Pool::Reclaim::RegisterAllRoots");
  auto call = tracker.Call();

  std::list<NonNull<std::shared_ptr<ObjectMetadata>>> output;
  for (const NonNull<std::unique_ptr<ObjectMetadataList>>& l : object_metadata)
    for (const std::weak_ptr<ObjectMetadata>& root_weak : l.value()) {
      VisitPointer(
          root_weak,
          [&output](NonNull<std::shared_ptr<ObjectMetadata>> root) {
            AddReachable(root, output);
          },
          [] { LOG(FATAL) << "Root was dead. Should never happen."; });
    }

  VLOG(5) << "Roots registered: " << output.size();
  return output;
}

void Pool::MarkReachable(
    std::list<language::NonNull<std::shared_ptr<ObjectMetadata>>> expand) {
  VLOG(3) << "Starting recursive expansion (roots: " << expand.size() << ")";

  static Tracker tracker(L"gc::Pool::Reclaim::Recursive Expansion");
  auto call = tracker.Call();

  while (!expand.empty()) {
    VLOG(5) << "Considering obj: " << expand.front().get_shared();
    auto expansion =
        expand.front()->data_.lock([&](ObjectMetadata::Data& object_data) {
          CHECK(object_data.expand_callback != nullptr);
          CHECK(object_data.reached);
          return object_data.expand_callback();
        });
    VLOG(6) << "Installing expansion of " << expand.front().get_shared() << ": "
            << expansion.size();
    expand.pop_front();
    for (NonNull<std::shared_ptr<ObjectMetadata>> obj : expansion) {
      AddReachable(obj, expand);
    }
  }
}

void Pool::AddSurvivors(
    Pool::ObjectMetadataList input,
    std::vector<ObjectMetadata::ExpandCallback>& expired_objects_callbacks,
    Pool::ObjectMetadataList& output) {
  static Tracker tracker(L"gc::Pool::Reclaim::AddSurvivors");
  auto call = tracker.Call();

  for (std::weak_ptr<ObjectMetadata>& obj_weak : input)
    VisitPointer(
        obj_weak,
        [&](NonNull<std::shared_ptr<ObjectMetadata>> obj) {
          obj->data_.lock([&](ObjectMetadata::Data& object_data) {
            if (object_data.reached) {
              object_data.reached = false;
              output.push_back(obj.get_shared());
            } else {
              expired_objects_callbacks.push_back(
                  std::move(object_data.expand_callback));
            }
          });
        },
        [] {});
}

Pool::RootRegistration Pool::AddRoot(
    std::weak_ptr<ObjectMetadata> object_metadata) {
  VLOG(5) << "Adding root: " << object_metadata.lock();
  return eden_.lock([&](Eden& eden) {
    eden.roots->push_back(object_metadata);
    return RootRegistration(
        new bool(false),
        [this, root_deleted = Eden::RootDeleted{
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

std::ostream& operator<<(std::ostream& os,
                         const Pool::FullReclaimStats& stats) {
  os << "[roots: " << stats.roots << ", begin_total: " << stats.begin_total
     << ", end_total: " << stats.end_total
     << ", generations: " << stats.generations << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const Pool::LightReclaimStats& stats) {
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
    {{.name = L"ReclaimOnEmpty",
      .callback =
          [] {
            Pool::LightReclaimStats stats =
                std::get<Pool::LightReclaimStats>(gc::Pool().Reclaim());
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
              pool.Reclaim();
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
              pool.Reclaim();
              CHECK(!delete_notification_0->has_value());

              VLOG(5) << "Overriding root.";
              root = pool.NewRoot(MakeNonNullUnique<Node>());

              auto delete_notification_1 =
                  root.ptr()->delete_notification.listenable_value();

              CHECK(delete_notification_0->has_value());
              CHECK(!delete_notification_1->has_value());

              VLOG(5) << "Start reclaim.";
              auto stats = pool.FullReclaim();
              CHECK_EQ(stats.begin_total, 0ul);
              CHECK_EQ(stats.eden_size, 2ul);
              CHECK_EQ(stats.roots, 1ul);
              CHECK_EQ(stats.end_total, 1ul);

              CHECK(delete_notification_0->has_value());
              CHECK(!delete_notification_1->has_value());

              return delete_notification_1;
            }();
            CHECK(delete_notification->has_value());

            Pool::FullReclaimStats stats = pool.FullReclaim();
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
              pool.Reclaim();
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

              VLOG(5) << "Trigger Reclaim.";
              pool.Reclaim();

              CHECK(!delete_notification_0->has_value());
              CHECK(!child_notification->has_value());

              VLOG(5) << "Override root value.";
              root = pool.NewRoot(MakeNonNullUnique<Node>());

              auto delete_notification_1 =
                  root.ptr()->delete_notification.listenable_value();

              CHECK(!child_notification->has_value());
              CHECK(!delete_notification_0->has_value());
              CHECK(!delete_notification_1->has_value());

              pool.FullReclaim();

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
              auto stats = pool.FullReclaim();
              CHECK_EQ(stats.eden_size, 10ul);
              CHECK_EQ(stats.begin_total, 0ul);
              CHECK_EQ(stats.end_total, 10ul);
              CHECK(!old_notification->has_value());
            }

            VLOG(5) << "Replacing loop.";
            root = MakeLoop(pool, 5);
            CHECK(!old_notification->has_value());
            {
              auto stats = pool.FullReclaim();
              CHECK_EQ(stats.eden_size, 5ul);
              CHECK_EQ(stats.begin_total, 10ul);
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
            Pool::FullReclaimStats stats = pool.FullReclaim();
            CHECK_EQ(stats.eden_size, 7ul);
            CHECK_EQ(stats.begin_total, 0ul);
            CHECK_EQ(stats.roots, 1ul);
            CHECK_EQ(stats.end_total, 5ul);
          }},
     {.name = L"WeakPtrNoRefs",
      .callback =
          [] {
            gc::Pool pool;
            std::optional<gc::Root<Node>> root = MakeLoop(pool, 7);
            gc::WeakPtr<Node> weak_ptr = root->ptr().ToWeakPtr();

            pool.FullReclaim();
            CHECK(weak_ptr.Lock().has_value());

            root = std::nullopt;
            pool.FullReclaim();
            CHECK(!weak_ptr.Lock().has_value());
          }},
     {.name = L"WeakPtrWithPtrRef", .callback = [] {
        gc::Pool pool;
        std::optional<gc::Root<Node>> root = MakeLoop(pool, 7);
        gc::Ptr<Node> ptr = root->ptr();
        gc::WeakPtr<Node> weak_ptr = ptr.ToWeakPtr();

        pool.FullReclaim();
        CHECK(weak_ptr.Lock().has_value());

        root = std::nullopt;
        pool.FullReclaim();
        CHECK(!weak_ptr.Lock().has_value());
      }}});

bool wants_reclaim_tests_registration = tests::Register(
    L"GC::FullVsLightReclaim",
    {
        {.name = L"OnEmpty",
         .callback =
             [] { std::get<Pool::LightReclaimStats>(gc::Pool().Reclaim()); }},
        {.name = L"FullOnEmpty", .callback = [] { gc::Pool().FullReclaim(); }},
        {.name = L"NotAfterAHundred",
         .callback =
             [] {
               gc::Pool pool;
               MakeLoop(pool, 100);
               std::get<Pool::LightReclaimStats>(pool.Reclaim());
             }},
        {.name = L"YesAfterEnough",
         .callback =
             [] {
               gc::Pool pool;
               std::optional<gc::Root<Node>> obj_0 = MakeLoop(pool, 1000);
               std::get<Pool::LightReclaimStats>(pool.Reclaim());
               std::optional<gc::Root<Node>> obj_1 = MakeLoop(pool, 1000);
               std::get<Pool::FullReclaimStats>(pool.Reclaim());
               obj_0 = std::nullopt;
               obj_1 = std::nullopt;
               std::get<Pool::LightReclaimStats>(pool.Reclaim());
               MakeLoop(pool, 1500);
             }},
        {.name = L"NotAfterReclaim",
         .callback =
             [] {
               gc::Pool pool;
               MakeLoop(pool, 1000);
               MakeLoop(pool, 1000);
               std::get<Pool::FullReclaimStats>(pool.Reclaim());
               std::get<Pool::LightReclaimStats>(pool.Reclaim());
             }},
        {.name = L"NotAfterReclaimBeforeFills",
         .callback =
             [] {
               gc::Pool pool;
               MakeLoop(pool, 1000);
               MakeLoop(pool, 1000);
               std::get<Pool::FullReclaimStats>(pool.Reclaim());
               std::get<Pool::LightReclaimStats>(pool.Reclaim());
               MakeLoop(pool, 1000);
               std::get<Pool::LightReclaimStats>(pool.Reclaim());
               MakeLoop(pool, 1000);
               std::get<Pool::FullReclaimStats>(pool.Reclaim());
             }},
        {.name = L"SomeSurvivingObjects",
         .callback =
             [] {
               gc::Pool pool;
               std::optional<gc::Root<Node>> root = MakeLoop(pool, 2048);
               MakeLoop(pool, 1000);
               std::get<Pool::FullReclaimStats>(
                   pool.Reclaim());  // Survivors: 2048
               std::get<Pool::LightReclaimStats>(pool.Reclaim());

               MakeLoop(pool, 1024);
               std::get<Pool::LightReclaimStats>(pool.Reclaim());

               MakeLoop(pool, 1024 + 4);
               root = std::nullopt;
               std::get<Pool::FullReclaimStats>(pool.Reclaim());
               std::get<Pool::LightReclaimStats>(pool.Reclaim());
               MakeLoop(pool, 900);
               std::get<Pool::LightReclaimStats>(pool.Reclaim());
             }},
        {.name = L"LargeTest",
         .callback =
             [] {
               gc::Pool pool;
               std::optional<gc::Root<Node>> root_big = MakeLoop(pool, 8000);
               std::optional<gc::Root<Node>> root_small = MakeLoop(pool, 2000);
               std::get<Pool::FullReclaimStats>(pool.Reclaim());
               std::get<Pool::LightReclaimStats>(pool.Reclaim());

               MakeLoop(pool, 9000);
               std::get<Pool::LightReclaimStats>(pool.Reclaim());

               MakeLoop(pool, 2000);
               std::get<Pool::FullReclaimStats>(pool.Reclaim());
               std::get<Pool::LightReclaimStats>(pool.Reclaim());

               MakeLoop(pool, 11000);
               root_big = std::nullopt;
               std::get<Pool::FullReclaimStats>(pool.Reclaim());
               std::get<Pool::LightReclaimStats>(pool.Reclaim());

               MakeLoop(pool, 1900);
               std::get<Pool::LightReclaimStats>(pool.Reclaim());

               MakeLoop(pool, 200);
               std::get<Pool::FullReclaimStats>(pool.Reclaim());
               std::get<Pool::LightReclaimStats>(pool.Reclaim());
             }},
    });

}  // namespace
}  // namespace afc::language

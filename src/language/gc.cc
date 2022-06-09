#include "src/language/gc.h"

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
  Reclaim();
}

Pool::ReclaimObjectsStats Pool::Reclaim() {
  static Tracker tracker(L"gc::Pool::Reclaim");
  auto call = tracker.Call();

  // We collect expired objects here and explicitly delete them once we have
  // unlocked data_.
  std::vector<ObjectMetadata::ExpandCallback> expired_objects_callbacks;

  ReclaimObjectsStats stats;

  Eden frozen_eden;
  eden_.lock([&](Eden& eden) { std::swap(eden, frozen_eden); });

  survivors_.lock([&](Survivors& survivors) {
    VLOG(3) << "Starting with generations: " << survivors.roots.size();
    InstallFrozenEden(survivors, frozen_eden);

    stats.generations = survivors.roots.size();
    stats.begin_total = survivors.object_metadata.size();

    for (const NonNull<std::unique_ptr<ObjectMetadataList>>& l :
         survivors.roots)
      stats.roots += l->size();

    MarkReachable(RegisterAllRoots(survivors.roots));

    VLOG(3) << "Building survivor list.";
    survivors.object_metadata = BuildSurvivorList(
        std::move(survivors.object_metadata), expired_objects_callbacks);
    stats.end_total = survivors.object_metadata.size();
    VLOG(3) << "Survivers: " << stats.end_total;
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

void Pool::InstallFrozenEden(Survivors& survivors, Eden& eden) {
  VLOG(3) << "Removing deleted roots: " << eden.roots_deleted.size();
  for (const Eden::RootDeleted& d : eden.roots_deleted)
    d.roots_list->erase(d.it);

  VLOG(4) << "Installing objects from frozen eden.";
  survivors.object_metadata.insert(survivors.object_metadata.end(),
                                   eden.object_metadata.begin(),
                                   eden.object_metadata.end());

  VLOG(3) << "Removing empty lists of roots.";
  survivors.roots.push_back(std::move(eden.roots));
  survivors.roots.remove_if(
      [](const NonNull<std::unique_ptr<ObjectMetadataList>>& l) {
        return l->empty();
      });
}

std::list<language::NonNull<std::shared_ptr<ObjectMetadata>>>
Pool::RegisterAllRoots(
    const std::list<NonNull<std::unique_ptr<ObjectMetadataList>>>&
        object_metadata) {
  VLOG(3) << "Registering roots.";
  static Tracker tracker(L"gc::Pool::Reclaim::RegisterAllRoots");
  auto call = tracker.Call();

  std::list<language::NonNull<std::shared_ptr<ObjectMetadata>>> output;
  for (const NonNull<std::unique_ptr<ObjectMetadataList>>& l : object_metadata)
    RegisterRoots(l.value(), output);

  VLOG(5) << "Roots registered: " << output.size();
  return output;
}

void Pool::RegisterRoots(
    const ObjectMetadataList& roots,
    std::list<language::NonNull<std::shared_ptr<ObjectMetadata>>>& output) {
  for (const std::weak_ptr<ObjectMetadata>& root_weak : roots) {
    VisitPointer(
        root_weak,
        [&output](language::NonNull<std::shared_ptr<ObjectMetadata>> root) {
          root->data_.lock([&](ObjectMetadata::Data& object_data) {
            CHECK(!object_data.reached);
            CHECK(object_data.expand_callback != nullptr);
          });
          output.push_back(root);
        },
        [] { LOG(FATAL) << "Root was dead. Should never happen."; });
  }
}

void Pool::MarkReachable(
    std::list<language::NonNull<std::shared_ptr<ObjectMetadata>>> expand) {
  VLOG(3) << "Starting recursive expansion (roots: " << expand.size() << ")";

  static Tracker tracker(L"gc::Pool::Reclaim::Recursive Expansion");
  auto call = tracker.Call();

  while (!expand.empty()) {
    language::NonNull<std::shared_ptr<ObjectMetadata>> front =
        std::move(expand.front());
    expand.pop_front();
    VLOG(5) << "Considering obj: " << front.get_shared();
    auto expansion = front->data_.lock([&](ObjectMetadata::Data& object_data) {
      CHECK(object_data.expand_callback != nullptr);
      if (object_data.reached)
        return std::vector<NonNull<std::shared_ptr<ObjectMetadata>>>();
      object_data.reached = true;
      return object_data.expand_callback();
    });
    VLOG(6) << "Installing expansion of " << front.get_shared() << ": "
            << expansion.size();
    for (language::NonNull<std::shared_ptr<ObjectMetadata>> obj : expansion) {
      obj->data_.lock([&](ObjectMetadata::Data& object_data) {
        CHECK(object_data.expand_callback != nullptr);
        if (!object_data.reached) {
          expand.push_back(std::move(obj));
        }
      });
    }
  }
}

Pool::ObjectMetadataList Pool::BuildSurvivorList(
    Pool::ObjectMetadataList input,
    std::vector<ObjectMetadata::ExpandCallback>& expired_objects_callbacks) {
  static Tracker tracker(L"gc::Pool::Reclaim::Build Survivor List");
  auto call = tracker.Call();

  ObjectMetadataList output;
  for (std::weak_ptr<ObjectMetadata>& obj_weak : input) {
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
  return output;
}

Pool::RootRegistration Pool::AddRoot(
    std::weak_ptr<ObjectMetadata> object_metadata) {
  VLOG(5) << "Adding root: " << object_metadata.lock();
  return eden_.lock([&](Eden& eden) {
    eden.roots->push_back(object_metadata);
    return RootRegistration(
        new bool(false),
        [this, root_deleted = Eden::RootDeleted{
                   .roots_list = NonNull<ObjectMetadataList*>::AddressOf(
                       eden.roots.value()),
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
                         const Pool::ReclaimObjectsStats& stats) {
  os << "[roots: " << stats.roots << ", begin_total: " << stats.begin_total
     << ", end_total: " << stats.end_total
     << ", generations: " << stats.generations << "]";
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
            Pool::ReclaimObjectsStats stats = gc::Pool().Reclaim();
            CHECK_EQ(stats.begin_total, 0ul);
            CHECK_EQ(stats.end_total, 0ul);
            CHECK_EQ(stats.roots, 0ul);
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
              auto stats = pool.Reclaim();
              CHECK_EQ(stats.begin_total, 2ul);
              CHECK_EQ(stats.roots, 1ul);
              CHECK_EQ(stats.end_total, 1ul);

              CHECK(delete_notification_0->has_value());
              CHECK(!delete_notification_1->has_value());

              return delete_notification_1;
            }();
            CHECK(delete_notification->has_value());

            Pool::ReclaimObjectsStats stats = pool.Reclaim();
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

              pool.Reclaim();

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
              auto stats = pool.Reclaim();
              CHECK_EQ(stats.begin_total, 10ul);
              CHECK_EQ(stats.end_total, 10ul);
              CHECK(!old_notification->has_value());
            }

            VLOG(5) << "Replacing loop.";
            root = MakeLoop(pool, 5);
            CHECK(!old_notification->has_value());
            {
              auto stats = pool.Reclaim();
              CHECK_EQ(stats.begin_total, 15ul);
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
            Pool::ReclaimObjectsStats stats = pool.Reclaim();
            CHECK_EQ(stats.begin_total, 7ul);
            CHECK_EQ(stats.roots, 1ul);
            CHECK_EQ(stats.end_total, 5ul);
          }},
     {.name = L"WeakPtrNoRefs",
      .callback =
          [] {
            gc::Pool pool;
            std::optional<gc::Root<Node>> root = MakeLoop(pool, 7);
            gc::WeakPtr<Node> weak_ptr = root->ptr().ToWeakPtr();

            pool.Reclaim();
            CHECK(weak_ptr.Lock().has_value());

            root = std::nullopt;
            pool.Reclaim();
            CHECK(!weak_ptr.Lock().has_value());
          }},
     {.name = L"WeakPtrWithPtrRef", .callback = [] {
        gc::Pool pool;
        std::optional<gc::Root<Node>> root = MakeLoop(pool, 7);
        gc::Ptr<Node> ptr = root->ptr();
        gc::WeakPtr<Node> weak_ptr = ptr.ToWeakPtr();

        pool.Reclaim();
        CHECK(weak_ptr.Lock().has_value());

        root = std::nullopt;
        pool.Reclaim();
        CHECK(!weak_ptr.Lock().has_value());
      }}});
}  // namespace
}  // namespace afc::language

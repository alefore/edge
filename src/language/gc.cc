#include "src/language/gc.h"

#include "src/concurrent/notification.h"
#include "src/infrastructure/tracker.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::language {
namespace gc {
using infrastructure::Tracker;
using language::NonNull;

Pool::~Pool() {
  // TODO(gc, 2022-05-11): Enable this validation:
  // CHECK(roots_.empty());
  Reclaim();
}

bool Pool::Generation::IsEmpty() const {
  return objects.empty() && roots.empty() && roots_deleted.empty();
}

Pool::ReclaimObjectsStats Pool::Reclaim() {
  static Tracker tracker(L"gc::Pool::Reclaim");
  auto call = tracker.Call();

  // We collect expired objects here and explicitly delete them once we have
  // unlocked data_.
  std::vector<ControlFrame::ExpandCallback> expired_objects_callbacks;

  ReclaimObjectsStats stats;

  NonNull<std::unique_ptr<Generation>> new_generation;
  current_generation_.lock(
      [&](NonNull<std::unique_ptr<Generation>>& generation) {
        std::swap(new_generation, generation);
      });

  generations_.lock(
      [&](std::list<NonNull<std::unique_ptr<Generation>>>& generations) {
        VLOG(3) << "Starting with generations: " << generations.size();
        VLOG(3) << "Removing deleted roots: "
                << new_generation->roots_deleted.size();
        for (const Generation::RootDeleted& d : new_generation->roots_deleted)
          d.generation->roots.erase(d.it);
        new_generation->roots_deleted.clear();

        VLOG(3) << "Marking reachability and counting initial dead.";
        for (auto it = generations.begin(); it != generations.end();) {
          if ((*it)->IsEmpty()) {
            VLOG(4) << "Erasing empty generation: " << &it->value();
            generations.erase(it++);
          } else
            ++it;
        }

        if (generations.empty() || !new_generation->IsEmpty()) {
          VLOG(3) << "Pushing new generation: " << new_generation->IsEmpty();
          generations.push_back(std::move(new_generation));
        }

        stats.generations = generations.size();

        for (const NonNull<std::unique_ptr<Generation>>& g : generations)
          stats.begin_total += g->objects.size();

        {
          static Tracker tracker(L"gc::Pool::Reclaim::Initial Dead");
          auto call = tracker.Call();

          for (const NonNull<std::unique_ptr<Generation>>& g : generations)
            for (auto& obj_weak : g->objects)
              VisitPointer(
                  obj_weak,
                  [&](NonNull<std::shared_ptr<ControlFrame>> obj) {
                    obj->data_.lock([](ControlFrame::Data& object_data) {
                      object_data.reached = false;
                    });
                  },
                  [&] { stats.begin_dead++; });
        }

        for (const NonNull<std::unique_ptr<Generation>>& g : generations)
          stats.roots += g->roots.size();

        MarkReachable(RegisterAllRoots(generations));

        VLOG(3) << "Building survivers list.";
        std::vector<std::weak_ptr<ControlFrame>> survivers;
        {
          static Tracker tracker(L"gc::Pool::Reclaim::Build Survivers List");
          auto call = tracker.Call();
          for (const NonNull<std::unique_ptr<Generation>>& g : generations)
            while (!g->objects.empty()) {
              VisitPointer(
                  g->objects.front(),
                  [&](NonNull<std::shared_ptr<ControlFrame>> obj) {
                    obj->data_.lock([&](ControlFrame::Data& object_data) {
                      if (object_data.reached) {
                        object_data.reached = false;
                        survivers.push_back(std::move(obj).get_shared());
                      } else {
                        expired_objects_callbacks.push_back(
                            std::move(object_data.expand_callback));
                        object_data.expand_callback = nullptr;
                      }
                    });
                  },
                  [] {});
              g->objects.erase(g->objects.begin());
            }
        }
        VLOG(3) << "Survivers: " << survivers.size();
        generations.front()->objects = std::move(survivers);
        stats.end_total = generations.front()->objects.size();
      });

  VLOG(3) << "Allowing unreachable object to be deleted: "
          << expired_objects_callbacks.size();
  {
    static Tracker tracker(L"gc::Pool::Reclaim::Delete unreachable");
    auto call = tracker.Call();
    expired_objects_callbacks.clear();
  }

  LOG(INFO) << "Garbage collection results: " << stats;
  return stats;
}

std::list<language::NonNull<std::shared_ptr<ControlFrame>>>
Pool::RegisterAllRoots(
    const std::list<NonNull<std::unique_ptr<Generation>>>& generations) {
  VLOG(3) << "Registering roots.";
  static Tracker tracker(L"gc::Pool::Reclaim::RegisterAllRoots");
  auto call = tracker.Call();

  std::list<language::NonNull<std::shared_ptr<ControlFrame>>> output;
  for (const NonNull<std::unique_ptr<Generation>>& generation : generations)
    RegisterRoots(generation.value(), output);

  VLOG(5) << "Roots registered: " << output.size();
  return output;
}

void Pool::RegisterRoots(
    const Generation& generation,
    std::list<language::NonNull<std::shared_ptr<ControlFrame>>>& output) {
  for (const std::weak_ptr<ControlFrame>& root_weak : generation.roots) {
    VisitPointer(
        root_weak,
        [&output](language::NonNull<std::shared_ptr<ControlFrame>> root) {
          root->data_.lock([&](ControlFrame::Data& object_data) {
            CHECK(!object_data.reached);
            CHECK(object_data.expand_callback != nullptr);
          });
          output.push_back(root);
        },
        [] { LOG(FATAL) << "Root was dead. Should never happen."; });
  }
}

void Pool::MarkReachable(
    std::list<language::NonNull<std::shared_ptr<ControlFrame>>> expand) {
  VLOG(3) << "Starting recursive expansion (roots: " << expand.size() << ")";

  static Tracker tracker(L"gc::Pool::Reclaim::Recursive Expansion");
  auto call = tracker.Call();

  while (!expand.empty()) {
    language::NonNull<std::shared_ptr<ControlFrame>> front =
        std::move(expand.front());
    expand.pop_front();
    VLOG(5) << "Considering obj: " << front.get_shared();
    auto expansion = front->data_.lock([&](ControlFrame::Data& object_data) {
      CHECK(object_data.expand_callback != nullptr);
      if (object_data.reached)
        return std::vector<NonNull<std::shared_ptr<ControlFrame>>>();
      object_data.reached = true;
      return object_data.expand_callback();
    });
    VLOG(6) << "Installing expansion of " << front.get_shared() << ": "
            << expansion.size();
    for (language::NonNull<std::shared_ptr<ControlFrame>> obj : expansion) {
      obj->data_.lock([&](ControlFrame::Data& object_data) {
        CHECK(object_data.expand_callback != nullptr);
        if (!object_data.reached) {
          expand.push_back(std::move(obj));
        }
      });
    }
  }
}

Pool::RootRegistration Pool::AddRoot(
    std::weak_ptr<ControlFrame> control_frame) {
  VLOG(5) << "Adding root: " << control_frame.lock();
  return current_generation_.lock(
      [&](NonNull<std::unique_ptr<Generation>>& generation) {
        generation->roots.push_back(control_frame);
        return RootRegistration(
            new bool(false),
            [this, root_deleted = Generation::RootDeleted{
                       .generation = generation.get(),
                       .it = std::prev(generation->roots.end())}](bool* value) {
              delete value;
              current_generation_.lock(
                  [&](NonNull<std::unique_ptr<Generation>>& generation) {
                    VLOG(5) << "Erasing roots, generation: "
                            << root_deleted.generation.get();
                    generation->roots_deleted.push_back(root_deleted);
                  });
            });
      });
}

void Pool::AddObj(
    language::NonNull<std::shared_ptr<ControlFrame>> control_frame) {
  current_generation_.lock([&](NonNull<std::unique_ptr<Generation>>& data) {
    data->objects.push_back(control_frame.get_shared());
    VLOG(5) << "Added object: " << control_frame.get_shared()
            << " (total: " << data->objects.size() << ")";
  });
}

std::ostream& operator<<(std::ostream& os,
                         const Pool::ReclaimObjectsStats& stats) {
  os << "[roots: " << stats.roots << ", begin_total: " << stats.begin_total
     << ", begin_dead: " << stats.begin_dead
     << ", end_total: " << stats.end_total
     << ", generations: " << stats.generations << "]";
  return os;
}

}  // namespace gc

namespace {
using concurrent::Notification;
using gc::Pool;

struct Node {
  ~Node() {
    VLOG(5) << "Deleting Node: " << this;
    delete_notification->Notify();
  }

  std::vector<gc::Ptr<Node>> children;

  NonNull<std::shared_ptr<Notification>> delete_notification;
};
}  // namespace

namespace gc {
std::vector<NonNull<std::shared_ptr<ControlFrame>>> Expand(const Node& node) {
  std::vector<NonNull<std::shared_ptr<ControlFrame>>> output;
  for (auto& child : node.children) {
    output.push_back(child.control_frame());
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
            CHECK_EQ(stats.begin_dead, 0ul);
            CHECK_EQ(stats.roots, 0ul);
          }},
     {.name = L"PreservesRoots",
      .callback =
          [] {
            gc::Pool pool;
            NonNull<std::shared_ptr<Notification>> delete_notification =
                [&pool] {
                  auto root = pool.NewRoot(MakeNonNullUnique<Node>());
                  auto delete_notification =
                      root.ptr().value().delete_notification;
                  pool.Reclaim();
                  CHECK(!delete_notification->HasBeenNotified());
                  return delete_notification;
                }();
            CHECK(delete_notification->HasBeenNotified());
          }},
     {.name = L"RootAssignment",
      .callback =
          [] {
            gc::Pool pool;
            NonNull<std::shared_ptr<Notification>> delete_notification =
                [&pool] {
                  auto root = pool.NewRoot(MakeNonNullUnique<Node>());
                  auto delete_notification_0 = root.ptr()->delete_notification;
                  pool.Reclaim();
                  CHECK(!delete_notification_0->HasBeenNotified());

                  VLOG(5) << "Overriding root.";
                  root = pool.NewRoot(MakeNonNullUnique<Node>());

                  auto delete_notification_1 = root.ptr()->delete_notification;

                  CHECK(delete_notification_0->HasBeenNotified());
                  CHECK(!delete_notification_1->HasBeenNotified());

                  VLOG(5) << "Start reclaim.";
                  auto stats = pool.Reclaim();
                  CHECK_EQ(stats.begin_total, 2ul);
                  CHECK_EQ(stats.begin_dead, 1ul);
                  CHECK_EQ(stats.roots, 1ul);
                  CHECK_EQ(stats.end_total, 1ul);

                  CHECK(delete_notification_0->HasBeenNotified());
                  CHECK(!delete_notification_1->HasBeenNotified());

                  return delete_notification_1;
                }();
            CHECK(delete_notification->HasBeenNotified());

            Pool::ReclaimObjectsStats stats = pool.Reclaim();
            CHECK_EQ(stats.begin_total, 1ul);
            CHECK_EQ(stats.begin_dead, 1ul);
            CHECK_EQ(stats.roots, 0ul);
            CHECK_EQ(stats.end_total, 0ul);
          }},
     {.name = L"BreakLoop",
      .callback =
          [] {
            gc::Pool pool;
            NonNull<std::shared_ptr<Notification>> delete_notification =
                [&pool] {
                  gc::Root<Node> root = pool.NewRoot(MakeNonNullUnique<Node>());
                  auto delete_notification_0 = root.ptr()->delete_notification;
                  pool.Reclaim();
                  CHECK(!delete_notification_0->HasBeenNotified());

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
                    return child->delete_notification;
                  }();

                  CHECK(!delete_notification_0->HasBeenNotified());
                  CHECK(!child_notification->HasBeenNotified());

                  VLOG(5) << "Trigger Reclaim.";
                  pool.Reclaim();

                  CHECK(!delete_notification_0->HasBeenNotified());
                  CHECK(!child_notification->HasBeenNotified());

                  VLOG(5) << "Override root value.";
                  root = pool.NewRoot(MakeNonNullUnique<Node>());

                  auto delete_notification_1 = root.ptr()->delete_notification;

                  CHECK(!child_notification->HasBeenNotified());
                  CHECK(!delete_notification_0->HasBeenNotified());
                  CHECK(!delete_notification_1->HasBeenNotified());

                  pool.Reclaim();

                  CHECK(child_notification->HasBeenNotified());
                  CHECK(delete_notification_0->HasBeenNotified());
                  CHECK(!delete_notification_1->HasBeenNotified());

                  return delete_notification_1;
                }();
            CHECK(delete_notification->HasBeenNotified());
          }},
     {.name = L"RootsReplaceLoop",
      .callback =
          [] {
            gc::Pool pool;
            gc::Root root = MakeLoop(pool, 10);
            auto old_notification = root.ptr()->delete_notification;

            {
              auto stats = pool.Reclaim();
              CHECK_EQ(stats.begin_total, 10ul);
              CHECK_EQ(stats.end_total, 10ul);
              CHECK(!old_notification->HasBeenNotified());
            }

            VLOG(5) << "Replacing loop.";
            root = MakeLoop(pool, 5);
            CHECK(!old_notification->HasBeenNotified());
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
              auto notification = split->children[0]->delete_notification;
              CHECK(!notification->HasBeenNotified());
              split->children.clear();
              CHECK(notification->HasBeenNotified());
            }
            CHECK(!root.ptr()->delete_notification->HasBeenNotified());
            Pool::ReclaimObjectsStats stats = pool.Reclaim();
            CHECK_EQ(stats.begin_total, 7ul);
            CHECK_EQ(stats.begin_dead, 2ul);
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

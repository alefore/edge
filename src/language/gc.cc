#include "src/language/gc.h"

#include "src/concurrent/notification.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::language {
namespace gc {
Pool::ReclaimObjectsStats Pool::Reclaim() {
  ReclaimObjectsStats stats;

  LOG(INFO) << "Marking reachability and counting initial dead.";
  stats.begin_total = objects_.size();
  for (auto& obj_weak : objects_)
    if (auto obj = obj_weak.lock(); obj != nullptr)
      obj->reached_ = false;
    else
      stats.begin_dead++;

  LOG(INFO) << "Registering roots.";
  stats.roots = roots_.size();
  std::list<language::NonNull<std::shared_ptr<ControlFrame>>> expand;
  for (const std::weak_ptr<ControlFrame>& root_weak : roots_) {
    VisitPointer(
        root_weak,
        [&expand](language::NonNull<std::shared_ptr<ControlFrame>> root) {
          CHECK(!root->reached_);
          expand.push_back(root);
        },
        [] { LOG(FATAL) << "Root was dead. Should never happen."; });
  }

  LOG(INFO) << "Starting recursive expansion.";
  while (!expand.empty()) {
    language::NonNull<std::shared_ptr<ControlFrame>> front =
        std::move(expand.front());
    expand.pop_front();
    VLOG(5) << "Considering obj: " << front.get_shared();
    if (front->reached_) continue;
    front->reached_ = true;
    for (language::NonNull<std::shared_ptr<ControlFrame>> obj :
         front->expand_callback_()) {
      if (!obj->reached_) {
        expand.push_back(std::move(obj));
      }
    }
  }
  std::vector<std::weak_ptr<ControlFrame>> survivers;
  for (std::weak_ptr<ControlFrame>& obj_weak : objects_) {
    auto obj = obj_weak.lock();
    if (obj == nullptr) continue;
    if (obj->reached_) {
      obj->reached_ = false;
      survivers.push_back(std::move(obj_weak));
    } else {
      LOG(INFO) << "Allowing object to be deleted.";
      obj->expand_callback_ = nullptr;
    }
  }
  LOG(INFO) << "Survivers: " << survivers.size() << " (of " << objects_.size()
            << ")";
  survivers.swap(objects_);
  stats.end_total = objects_.size();
  return stats;
}

Pool::RootRegistration Pool::AddRoot(
    std::weak_ptr<ControlFrame> control_frame) {
  roots_.push_back(control_frame);
  return --roots_.end();
}

void Pool::AddObj(
    language::NonNull<std::shared_ptr<ControlFrame>> control_frame) {
  objects_.push_back(control_frame.get_shared());
  LOG(INFO) << "Added object: " << control_frame.get_shared()
            << " (total: " << objects_.size() << ")";
}

void Pool::EraseRoot(RootRegistration registration) {
  LOG(INFO) << "Erasing root: " << registration->lock();
  roots_.erase(registration);
}
}  // namespace gc

namespace {
using concurrent::Notification;
using gc::Pool;

struct Node {
  ~Node() {
    LOG(INFO) << "Deleting Node: " << this;
    delete_notification->Notify();
  }

  std::vector<gc::Ptr<Node>> children;

  NonNull<std::shared_ptr<Notification>> delete_notification;
};

std::vector<NonNull<std::shared_ptr<gc::ControlFrame>>> Expand(Node& node) {
  std::vector<NonNull<std::shared_ptr<gc::ControlFrame>>> output;
  for (auto& child : node.children) {
    output.push_back(child.control_frame());
  }
  LOG(INFO) << "Generated expansion of node " << &node << ": " << output.size();
  return output;
}

gc::Ptr<Node> MakeLoop(gc::Pool& pool, int size) {
  gc::Ptr<Node> start = pool.NewPtr(std::make_unique<Node>());
  Node* last = start.value();
  for (int i = 1; i < size; i++) {
    gc::Ptr<Node> child = pool.NewPtr(std::make_unique<Node>());
    last->children.push_back(child);
    last = child.value();
  }
  last->children.push_back(start);
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
                  auto root = pool.NewRoot(std::make_unique<Node>());
                  auto delete_notification =
                      root.value().value()->delete_notification;
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
                  auto root = pool.NewRoot(std::make_unique<Node>());
                  auto delete_notification_0 =
                      root.value().value()->delete_notification;
                  pool.Reclaim();
                  CHECK(!delete_notification_0->HasBeenNotified());

                  root.Set(pool.NewPtr(std::make_unique<Node>()));

                  auto delete_notification_1 =
                      root.value().value()->delete_notification;

                  CHECK(delete_notification_0->HasBeenNotified());
                  CHECK(!delete_notification_1->HasBeenNotified());

                  LOG(INFO) << "Start reclaim.";
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
                  auto root = pool.NewRoot(std::make_unique<Node>());
                  auto delete_notification_0 =
                      root.value().value()->delete_notification;
                  pool.Reclaim();
                  CHECK(!delete_notification_0->HasBeenNotified());

                  auto child_notification = [&] {
                    LOG(INFO) << "Creating child.";
                    auto child = pool.NewPtr(std::make_unique<Node>());

                    LOG(INFO) << "Storing root in child.";
                    child.value()->children.push_back(root.value());
                    CHECK_EQ(child.value()->children[0].value(),
                             root.value().value());

                    LOG(INFO) << "Storing child in root.";
                    root.value().value()->children.push_back(child);

                    LOG(INFO) << "Returning (deleting child pointer).";
                    return child.value()->delete_notification;
                  }();

                  CHECK(!delete_notification_0->HasBeenNotified());
                  CHECK(!child_notification->HasBeenNotified());

                  LOG(INFO) << "Trigger Reclaim.";
                  pool.Reclaim();

                  CHECK(!delete_notification_0->HasBeenNotified());
                  CHECK(!child_notification->HasBeenNotified());

                  LOG(INFO) << "Override root value.";
                  root.Set(pool.NewPtr(std::make_unique<Node>()));

                  auto delete_notification_1 =
                      root.value().value()->delete_notification;

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
            gc::Root root = pool.NewRoot(MakeLoop(pool, 10));
            auto old_notification = root.value().value()->delete_notification;

            {
              auto stats = pool.Reclaim();
              CHECK_EQ(stats.begin_total, 10ul);
              CHECK_EQ(stats.end_total, 10ul);
              CHECK(!old_notification->HasBeenNotified());
            }

            LOG(INFO) << "Replacing loop.";
            root.Set(MakeLoop(pool, 5));
            CHECK(!old_notification->HasBeenNotified());
            {
              auto stats = pool.Reclaim();
              CHECK_EQ(stats.begin_total, 15ul);
              CHECK_EQ(stats.end_total, 5ul);
            }
          }},
     {.name = L"BreakLoopHalfway", .callback = [] {
        gc::Pool pool;
        gc::Root root = pool.NewRoot(MakeLoop(pool, 7));
        {
          gc::Ptr<Node> split = root.value();
          for (int i = 0; i < 4; i++) split = split.value()->children[0];
          auto notification =
              split.value()->children[0].value()->delete_notification;
          CHECK(!notification->HasBeenNotified());
          split.value()->children.clear();
          CHECK(notification->HasBeenNotified());
        }
        CHECK(!root.value().value()->delete_notification->HasBeenNotified());
        Pool::ReclaimObjectsStats stats = pool.Reclaim();
        CHECK_EQ(stats.begin_total, 7ul);
        CHECK_EQ(stats.begin_dead, 2ul);
        CHECK_EQ(stats.roots, 1ul);
        CHECK_EQ(stats.end_total, 5ul);
      }}});
}  // namespace
};  // namespace afc::language

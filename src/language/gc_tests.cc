#include <map>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/futures/delete_notification.h"
#include "src/language/container.h"
#include "src/language/gc.h"
#include "src/language/gc_view.h"
#include "src/language/safe_types.h"
#include "src/tests/concurrent.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;

using afc::concurrent::Operation;
using afc::concurrent::OperationFactory;
using afc::concurrent::Protected;
using afc::concurrent::ThreadPool;
using afc::futures::DeleteNotification;
using afc::language::NonNull;
using afc::tests::concurrent::TestFlows;

namespace afc::language::gc {
namespace {
struct Node {
  ~Node() { VLOG(5) << "Deleting Node: " << this; }
  std::vector<gc::Ptr<Node>> children;
  DeleteNotification delete_notification;
};
}  // namespace

template <>
struct ExpandHelper<Node> {
  std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> operator()(
      const Node& node) {
    auto output =
        container::MaterializeVector(node.children | gc::view::ObjectMetadata);
    VLOG(5) << "Generated expansion of node " << &node << ": " << output.size();
    return output;
  }
};

namespace {
Root<Node> MakeLoop(Pool& pool, int size) {
  Root<Node> start = pool.NewRoot(MakeNonNullUnique<Node>());
  Ptr<Node> last = start.ptr();
  for (int i = 1; i < size; i++) {
    Root<Node> child = pool.NewRoot(MakeNonNullUnique<Node>());
    last->children.push_back(child.ptr());
    last = last->children.back();
  }
  last->children.push_back(start.ptr());
  return start;
}

bool tests_gc_races_registration = tests::Register(
    L"GCRaces",
    {{.name = L"Simple", .runs = 0, .callback = [] {
        auto thread_pool = MakeNonNullShared<ThreadPool>(64);
        auto operation_factory =
            MakeNonNullShared<OperationFactory>(thread_pool);
        TestFlows({.thread_pool = thread_pool,
                   .start = [operation_factory, thread_pool] {
                     auto pool = MakeNonNullShared<Pool>(Pool::Options{
                         .collect_duration_threshold =
                             afc::infrastructure::Duration(0.02),
                         .operation_factory = operation_factory.get_shared(),
                         .max_bag_shards = 1});
                     thread_pool->RunIgnoringResult([pool] {
                       Root<Node> nodes = MakeLoop(pool.value(), 3);
                       MakeLoop(pool.value(), 2);
                     });
                     thread_pool->RunIgnoringResult(
                         [pool] { pool->Collect(); });
                     // thread_pool->RunIgnoringResult([pool] { pool->Collect();
                     // });
                     LOG(INFO) << "Test set up.";
                   }});
      }}});

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
     {.name = L"BreakLoopHalfway", .callback = [] {
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
        CHECK(!root.ptr()->delete_notification.listenable_value().has_value());
        Pool::FullCollectStats stats = pool.FullCollect();
        pool.BlockUntilDone();
        CHECK_EQ(stats.begin_total, 5ul);
        CHECK_EQ(stats.roots, 1ul);
        CHECK_EQ(stats.end_total, 5ul);
      }}});

bool weak_ptr_tests_registration = tests::Register(
    L"GC::WeakPtr",
    {{.name = L"WeakPtrInitialization",
      .callback =
          [] {
            gc::Pool pool({});
            std::optional<gc::Root<Node>> root = MakeLoop(pool, 0);
            gc::WeakPtr<Node> weak_ptr = root->ptr().ToWeakPtr();
            CHECK(weak_ptr.Lock().has_value());
            CHECK_EQ(&weak_ptr.Lock().value().ptr().value(),
                     &root->ptr().value());
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

bool concurrency_tests = tests::Register(
    L"GC::Concurrency",
    {
        {.name = L"CollectWithAssignment",
         .callback =
             [] {
               gc::Pool pool({});
               Node* last_value = nullptr;
               gc::Root<Node> root = pool.NewRoot(MakeNonNullUnique<Node>());
               Protected<bool> stop_collection(false);
               Protected<size_t> collection_iterations(0);

               // Thread for continuous collection.
               std::thread collection_thread([&] {
                 while (!*stop_collection.lock()) {
                   pool.Collect();
                   (*collection_iterations.lock())++;
                   pool.NewRoot(MakeNonNullUnique<Node>());
                 }
               });

               // Thread for assignment.
               std::thread assignment_thread([&] {
                 size_t roots_created = 0;
                 while (*collection_iterations.lock() < 10 ||
                        roots_created < 5) {
                   root = pool.NewRoot(MakeNonNullUnique<Node>());
                   last_value = &root.ptr().value();
                   roots_created++;
                 }
               });

               assignment_thread.join();  // Wait for assignment to complete.
               *stop_collection.lock() = true;
               collection_thread.join();  // Wait for collection to stop.

               CHECK_EQ(&root.ptr().value(), last_value);
             }},
        {.name = L"CollectWithThreadSafeRefCounting",
         .callback =
             [] {
               gc::Pool pool({});
               std::atomic<bool> stop_collection(false);
               const gc::Root<Node> root =
                   pool.NewRoot(MakeNonNullUnique<Node>());
               const gc::Ptr<Node> ptr = root.ptr();
               Node* const value = &ptr.value();

               // Thread for continuous collection.
               std::thread collection_thread([&] {
                 while (!stop_collection) pool.Collect();
               });

               // Multiple threads for reference counting.
               const int num_threads = 10;
               std::vector<std::thread> threads;
               for (int i = 0; i < num_threads; ++i)
                 threads.emplace_back([&ptr, value] {
                   for (int j = 0; j < 1000; ++j) {
                     gc::Ptr<Node> temp_ptr = ptr;
                     CHECK(&temp_ptr.value() == value);
                   }
                 });

               for (auto& t : threads) t.join();
               stop_collection = true;    // Signal to stop collection.
               collection_thread.join();  // Wait for collection to stop.

               CHECK(&ptr.value() == value);
             }},
        {.name = L"ContinuousCollectWithConcurrentCreation",
         .callback =
             [] {
               gc::Pool pool({});
               std::atomic<bool> stop_collection(false);
               std::atomic<bool> creation_done(false);
               const gc::Root<Node> root =
                   pool.NewRoot(MakeNonNullUnique<Node>());
               const gc::Ptr<Node> ptr = root.ptr();
               Node* const value = &ptr.value();

               // Thread for continuous collection.
               std::thread collection_thread([&] {
                 while (!stop_collection) {
                   LOG(INFO) << "Starting collection.";
                   pool.Collect();
                 }
                 LOG(INFO) << "Collection thread stopping.";
               });

               // Thread for object creation.
               std::thread creation_thread([&] {
                 for (int i = 0; i < 10; ++i) {
                   std::vector<gc::Root<Node>> roots;
                   for (int j = 0; j < 10; ++j) {
                     LOG(INFO) << "Iteration " << i << ", roots: " << j;
                     roots.push_back(MakeLoop(pool, 10));
                   }
                 }
                 LOG(INFO) << "Creation done.";
                 creation_done = true;
               });

               creation_thread.join();    // Wait for creation to complete.
               stop_collection = true;    // Signal to stop collection.
               collection_thread.join();  // Wait for collection to stop.

               CHECK(&ptr.value() == value);
             }},
    });
}  // namespace
}  // namespace afc::language::gc

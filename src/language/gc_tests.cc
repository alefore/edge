#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/tests/concurrent.h"
#include "src/tests/tests.h"

using afc::concurrent::Operation;
using afc::concurrent::OperationFactory;
using afc::concurrent::ThreadPool;
using afc::language::NonNull;
using afc::tests::concurrent::TestFlows;

namespace afc::language::gc {
namespace {
struct Node {
  std::vector<gc::Ptr<Node>> children;
};
}  // namespace

std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> Expand(const Node& node) {
  std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> output;
  for (auto& child : node.children) output.push_back(child.object_metadata());
  return output;
}

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

bool tests_registration = tests::Register(
    L"GCRaces",
    {{.name = L"Simple", .callback = [] {
        auto thread_pool = MakeNonNullShared<ThreadPool>(64, nullptr);
        auto operation_factory =
            MakeNonNullShared<OperationFactory>(thread_pool);
        TestFlows({.thread_pool = thread_pool,
                   .start = [operation_factory, thread_pool] {
                     auto pool = MakeNonNullShared<Pool>(Pool::Options{
                         .collect_duration_threshold =
                             afc::infrastructure::Duration(0.02),
                         .operation_factory = operation_factory.get_shared(),
                         .max_bag_shards = 1});
#if 0
               NonNull<std::unique_ptr<Operation>> operation =
                   operation_factory->New(nullptr);
               thread_pool->RunIgnoringResult([] {
                 tests::concurrent::GetGlobalHandler()->AddBreakpoint("foo", 0);
                 tests::concurrent::GetGlobalHandler()->AddBreakpoint("foo", 1);
               });
               thread_pool->RunIgnoringResult([] {
                 tests::concurrent::GetGlobalHandler()->AddBreakpoint("foo", 2);
                 tests::concurrent::GetGlobalHandler()->AddBreakpoint("foo", 3);
               });
               thread_pool->RunIgnoringResult(
                   [pointer =
                        std::shared_ptr<bool>(new bool(true), [](bool* value) {
                          delete value;
                          tests::concurrent::GetGlobalHandler()->AddBreakpoint(
                              "delete", 5);
                        })] {
                     tests::concurrent::GetGlobalHandler()->AddBreakpoint("foo",
                                                                          4);
                     concurrent::Protected<bool> data =
                         concurrent::Protected<bool>(true);
                     data.lock();
                   });
#endif
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
}  // namespace
}  // namespace afc::language::gc

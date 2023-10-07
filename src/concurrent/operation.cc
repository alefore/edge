#include "src/concurrent/operation.h"

#include "src/concurrent/thread_pool.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

using afc::language::MakeNonNullUnique;
using afc::language::NonNull;

namespace afc::concurrent {
namespace {
const bool tests_registration = tests::Register(
    L"Concurrent::Operation",
    {{.name = L"Empty",
      .callback =
          [] {
            ThreadPool thread_pool(5);
            Operation op(thread_pool);
          }},
     {.name = L"Sleeps", .callback = [] {
        ThreadPool thread_pool(4);
        Protected<int> executions(0);
        auto op = std::make_unique<Operation>(thread_pool);
        for (size_t i = 0; i < 8; i++)
          op->Add([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            (*executions.lock())++;
          });
        op = nullptr;
        CHECK_EQ(*executions.lock(), 8);
      }}});
}

OperationFactory ::OperationFactory(
    NonNull<std::shared_ptr<ThreadPool>> thread_pool)
    : thread_pool_(std::move(thread_pool)) {}

language::NonNull<std::unique_ptr<Operation>> OperationFactory::New(
    std::unique_ptr<bool, std::function<void(bool*)>> tracker_call) {
  return MakeNonNullUnique<Operation>(
      thread_pool_.value(), thread_pool_->size() * 2, std::move(tracker_call));
}

}  // namespace afc::concurrent

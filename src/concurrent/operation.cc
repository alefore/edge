#include "src/concurrent/operation.h"

#include "src/concurrent/thread_pool.h"
#include "src/tests/tests.h"

namespace afc::concurrent {
namespace {
const bool tests_registration = tests::Register(
    L"Concurrent::Operation", {{.name = L"Empty",
                                .callback =
                                    [] {
                                      ThreadPool thread_pool(5, nullptr);
                                      Operation op(thread_pool);
                                    }},
                               {.name = L"Sleeps", .callback = [] {
                                  ThreadPool thread_pool(4, nullptr);
                                  Protected<int> executions(0);
                                  auto op =
                                      std::make_unique<Operation>(thread_pool);
                                  for (size_t i = 0; i < 8; i++)
                                    op->Add([&] {
                                      sleep(0.5);
                                      (*executions.lock())++;
                                    });
                                  op = nullptr;
                                  CHECK_EQ(*executions.lock(), 8);
                                }}});
}
}  // namespace afc::concurrent

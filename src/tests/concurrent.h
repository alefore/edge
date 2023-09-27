#ifndef __AFC_TESTS_CONCURRENT_H__
#define __AFC_TESTS_CONCURRENT_H__

#include <functional>
#include <memory>

#include "src/concurrent/thread_pool.h"
#include "src/infrastructure/time.h"
#include "src/language/safe_types.h"

namespace afc::tests::concurrent {
struct Options {
  language::NonNull<std::shared_ptr<afc::concurrent::ThreadPool>> thread_pool;
  afc::infrastructure::Duration timeout = 0.050;
  std::function<void()> start;
};

void TestFlows(Options options);
};  // namespace afc::tests::concurrent
#endif

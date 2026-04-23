#include "src/concurrent/protected.h"

#include <glog/logging.h>

#include "src/tests/tests.h"

namespace afc::concurrent {
namespace {
const bool tests_registration = tests::Register(
    L"concurrent::Protected", {{.name = L"MoveWorks", .callback = [] {
                                  Protected<int> foo(5);
                                  Protected<int> bar(50);
                                  Protected<int> quux(100);
                                  foo = std::move(bar);
                                  bar = std::move(quux);
                                  auto foo_lock = foo.lock();
                                  auto bar_lock = bar.lock();
                                  CHECK_EQ(*foo_lock, 50);
                                }}});
}
}  // namespace afc::concurrent

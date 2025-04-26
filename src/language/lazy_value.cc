#include "src/language/lazy_value.h"

#include <glog/logging.h>

#include "src/tests/tests.h"

namespace afc::language {
namespace {
const bool filter_to_range_tests_registration = tests::Register(
    L"LazyValue", {{.name = L"NeverRuns",
                    .callback =
                        [] {
                          bool run = false;
                          LazyValue<int>([&run] {
                            CHECK(!run);
                            run = true;
                            return 5;
                          });
                          CHECK(!run);
                        }},
                   {.name = L"RunsAndReturnsValid", .callback = [] {
                      bool run = false;
                      LazyValue<int> lazy_value([&run] {
                        CHECK(!run);
                        run = true;
                        return 549;
                      });
                      for (size_t i = 0; i < 10; i++) {
                        CHECK_EQ(lazy_value.get(), 549);
                        CHECK(run);
                      }
                    }}});
}  // namespace
}  // namespace afc::language

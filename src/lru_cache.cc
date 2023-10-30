#include "src/lru_cache.h"

#include <glog/logging.h>

#include <unordered_map>

#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
using ::operator<<;

bool Get(LRUCache<int, std::string>& cache, int key) {
  std::unordered_map<int, std::string> values = {
      {1, "uno"}, {2, "dos"}, {3, "tres"}, {4, "cuatro"}, {5, "cinco"}};
  bool executed = false;
  CHECK_EQ(cache
               .Get(key,
                    [&values, key, &executed]() {
                      executed = true;
                      return values[key];
                    })
               .value(),
           values[key]);
  return executed;
}

const bool bayes_sort_tests_registration =
    tests::Register(L"LRUCache", {{.name = L"Basic",
                                   .callback =
                                       [] {
                                         LRUCache<int, std::string> cache(5);
                                         for (size_t i = 0; i < 20; i++)
                                           CHECK_EQ(Get(cache, 1), i == 0);
                                       }},
                                  {.name = L"DiffKeys",
                                   .callback =
                                       [] {
                                         LRUCache<int, std::string> cache(3);
                                         for (size_t i = 0; i < 5; i++) {
                                           CHECK_EQ(Get(cache, 1), i == 0);
                                           CHECK_EQ(Get(cache, 2), i == 0);
                                           CHECK_EQ(Get(cache, 3), i == 0);
                                         }
                                       }},
                                  {.name = L"Evicts", .callback = [] {
                                     LRUCache<int, std::string> cache(4);
                                     for (size_t i = 0; i < 5; i++)
                                       for (int j = 0; j < 4; j++)
                                         CHECK_EQ(Get(cache, j), i == 0);
                                     CHECK(Get(cache, 5));
                                     for (size_t i = 0; i < 4; i++)
                                       for (int j = 0; j < 4; j++)
                                         CHECK_EQ(Get(cache, j), i == 0);
                                   }}});
}  // namespace
}  // namespace afc::editor

#include "src/language/hash.h"

#include <glog/logging.h>

#include <vector>

#include "src/tests/tests.h"

namespace afc::language {
namespace {
const bool line_tests_registration = tests::Register(
    L"Hash", {{.name = L"HashableContainerFromVector",
               .callback =
                   [] {
                     HashableContainer<std::vector<std::wstring>> elements(
                         {L"alejo", L"selina", L"tintín"});
                     size_t initial_hash = compute_hash(elements);
                     elements.container.push_back(L"gael");
                     CHECK(compute_hash(elements) != initial_hash);
                     elements.container.pop_back();
                     CHECK(compute_hash(elements) == initial_hash);
                   }},
              {.name = L"WithHash", .callback = [] {
                 auto value = std::make_shared<int>(0);
                 size_t initial_hash =
                     compute_hash(MakeWithHash(value, compute_hash(*value)));
                 *value = 4;
                 CHECK(compute_hash(MakeWithHash(
                           value, compute_hash(*value))) != initial_hash);
               }}});
}
}  // namespace afc::language

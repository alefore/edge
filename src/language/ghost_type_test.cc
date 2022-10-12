#include "src/language/ghost_type.h"

#include <glog/logging.h>

#include <map>

#include "src/tests/tests.h"

namespace afc::editor {
namespace {
using TestMapInternal = std::multimap<int, std::wstring>;
GHOST_TYPE_CONTAINER(TestMap, TestMapInternal);

using TestVectorInternal = std::vector<int>;
GHOST_TYPE_CONTAINER(TestVector, TestVectorInternal)

bool ghost_type_tests_registration = tests::Register(
    L"GhostTypes",
    std::vector<tests::Test>({{.name = L"Multimap",
                               .callback =
                                   [] {
                                     TestMap m;
                                     CHECK_EQ(m.size(), 0);
                                     m.insert({1, L"foo"});
                                     CHECK_EQ(m.size(), 1);
                                     m.insert({5, L"bar"});
                                     CHECK_EQ(m.size(), 2);
                                     auto it = m.lower_bound(2);
                                     CHECK(it != m.end());
                                     CHECK_EQ(it->first, 5);
                                     CHECK(it->second == L"bar");
                                   }},
                              {.name = L"Vector", .callback = [] {
                                 TestVector m;
                                 CHECK(m.empty());
                                 CHECK_EQ(m.size(), 0);

                                 m.push_back(1);
                                 CHECK_EQ(m.size(), 1);
                                 CHECK_EQ(m[0], 1);

                                 m.push_back(5);
                                 CHECK_EQ(m.size(), 2);
                                 CHECK_EQ(m[0], 1);
                                 CHECK_EQ(m[1], 5);
                               }}}));
}  // namespace
}  // namespace afc::editor

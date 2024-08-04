#include "src/vm/callbacks.h"

#include "src/tests/tests.h"
#include "src/vm/types.h"

using afc::language::Error;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::math::numbers::Number;

namespace afc::vm {
const Type VMTypeMapper<bool>::vmtype = types ::Bool{};
const Type VMTypeMapper<Number>::vmtype = types::Number{};
const Type VMTypeMapper<int>::vmtype = types::Number{};
const Type VMTypeMapper<size_t>::vmtype = types::Number{};
const Type VMTypeMapper<double>::vmtype = types::Number{};
const Type VMTypeMapper<std::wstring>::vmtype = types::String{};
const Type VMTypeMapper<LazyString>::vmtype = types ::String{};

namespace {
bool tests_extract_first_error = tests::Register(
    L"ExtractFirstError",
    {{.name = L"Empty",
      .callback =
          [] {
            std::tuple tuple{};
            CHECK(!ExtractFirstError(tuple).has_value());
          }},
     {.name = L"NoError",
      .callback =
          [] {
            std::tuple tuple{1, L"foo", Success(4), Success(L"bar")};
            CHECK(!ExtractFirstError(tuple).has_value());
          }},
     {.name = L"Error", .callback = [] {
        std::tuple tuple{1, L"foo", Success(4),
                         ValueOrError<int>{Error{LazyString{L"quux"}}},
                         Success(L"bar")};
        std::optional<Error> error = ExtractFirstError(tuple);
        CHECK(error->read() == LazyString{L"quux"});
      }}});
}  // namespace

}  // namespace afc::vm

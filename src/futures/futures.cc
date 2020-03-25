#include "src/futures/futures.h"

#include "glog/logging.h"
#include "src/tests/tests.h"
#include "src/value_or_error.h"

namespace afc::futures {
Value<editor::ValueOrError<editor::EmptyValue>> IgnoreErrors(
    Value<editor::PossibleError> value) {
  Future<editor::PossibleError> output;
  value.SetConsumer(
      [consumer = std::move(output.consumer)](const editor::PossibleError&) {
        consumer(editor::Success());
      });
  return output.value;
}

namespace {
// TODO(easy): Add more tests.
class TransformTests : public tests::TestGroup<TransformTests> {
 public:
  std::wstring Name() const override { return L"TransformTests"; }
  std::vector<tests::Test> Tests() const override {
    using editor::Error;
    using editor::Success;
    using editor::ValueOrError;
    return {
        {.name = L"StopsEarlyOnError",
         .callback =
             [] {
               std::optional<ValueOrError<bool>> final_result;
               Future<ValueOrError<bool>> inner_value;
               auto value = futures::Transform(inner_value.value, [](bool) {
                 CHECK(false);
                 return Success(true);
               });
               value.SetConsumer(
                   [&](ValueOrError<bool> result) { final_result = result; });
               inner_value.consumer(Error(L"xyz"));
               CHECK(final_result.has_value());
             }},
        {.name = L"CorrectlyReturnsError",
         .callback =
             [] {
               std::optional<ValueOrError<bool>> final_result;
               Future<ValueOrError<bool>> inner_value;
               auto value = futures::Transform(
                   inner_value.value, [](bool) { return Success(true); });
               value.SetConsumer(
                   [&](ValueOrError<bool> result) { final_result = result; });
               inner_value.consumer(Error(L"xyz"));
               CHECK(final_result.has_value());
               CHECK(final_result.value().IsError());
               CHECK(final_result.value().error().description == L"xyz");
             }},
    };
  }
};

template <>
const bool tests::TestGroup<TransformTests>::registration_ =
    tests::Add<futures::TransformTests>();

class OnErrorTests : public tests::TestGroup<OnErrorTests> {
 public:
  OnErrorTests() : TestGroup<OnErrorTests>() {}
  std::wstring Name() const override { return L"OnErrorTests"; }
  std::vector<tests::Test> Tests() const override {
    using editor::Error;
    using editor::Success;
    using editor::ValueOrError;
    return {{.name = L"WaitsForFuture",
             .callback =
                 [] {
                   Future<ValueOrError<int>> internal;
                   bool executed = false;
                   auto external = OnError(internal.value, [&](Error error) {
                     executed = true;
                     CHECK(error.description == L"Foo");
                     return error;
                   });
                   CHECK(!executed);
                   internal.consumer(Error(L"Foo"));
                   CHECK(executed);
                 }},
            {.name = L"OverridesReturnedValue",
             .callback =
                 [] {
                   std::optional<ValueOrError<int>> value;
                   OnError(futures::Past(ValueOrError<int>(Error(L"Foo"))),
                           [&](Error) { return Success(27); })
                       .SetConsumer(
                           [&](ValueOrError<int> result) { value = result; });
                   CHECK(!value.value().IsError());
                   CHECK_EQ(value.value().value(), 27);
                 }},
            {.name = L"SkippedOnSuccess", .callback = [] {
               OnError(futures::Past(ValueOrError(Success(12))),
                       [&](Error value) {
                         CHECK(false);
                         return value;
                       });
             }}};
  }
};

template <>
const bool tests::TestGroup<OnErrorTests>::registration_ =
    tests::Add<futures::OnErrorTests>();
}  // namespace

}  // namespace afc::futures

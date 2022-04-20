#include "src/futures/futures.h"

#include "glog/logging.h"
#include "src/language/value_or_error.h"
#include "src/tests/tests.h"

namespace afc::futures {
using language::Error;
using language::PossibleError;
using language::Success;

Value<language::ValueOrError<language::EmptyValue>> IgnoreErrors(
    Value<PossibleError> value) {
  Future<PossibleError> output;
  value.SetConsumer([consumer = std::move(output.consumer)](
                        const PossibleError&) { consumer(Success()); });
  return std::move(output.value);
}

namespace {
// TODO(easy): Add more tests.
const bool futures_transform_tests_registration = tests::Register(
    L"TransformTests",
    {
        {.name = L"StopsEarlyOnError",
         .callback =
             [] {
               std::optional<language::ValueOrError<bool>> final_result;
               Future<language::ValueOrError<bool>> inner_value;
               auto value = inner_value.value.Transform([](bool) {
                 CHECK(false);
                 return Success(true);
               });
               value.SetConsumer([&](language::ValueOrError<bool> result) {
                 final_result = result;
               });
               inner_value.consumer(Error(L"xyz"));
               CHECK(final_result.has_value());
             }},
        {.name = L"CorrectlyReturnsError",
         .callback =
             [] {
               std::optional<language::ValueOrError<bool>> final_result;
               Future<language::ValueOrError<bool>> inner_value;
               auto value = inner_value.value.Transform(
                   [](bool) { return Success(true); });
               value.SetConsumer([&](language::ValueOrError<bool> result) {
                 final_result = result;
               });
               inner_value.consumer(Error(L"xyz"));
               CHECK(final_result.has_value());
               CHECK(final_result.value().IsError());
               CHECK(final_result.value().error().description == L"xyz");
             }},
    });

const bool futures_on_error_tests_registration = tests::Register(
    L"OnErrorTests",
    {{.name = L"WaitsForFuture",
      .callback =
          [] {
            Future<language::ValueOrError<int>> internal;
            bool executed = false;
            auto external =
                OnError(std::move(internal.value), [&](Error error) {
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
            std::optional<language::ValueOrError<int>> value;
            OnError(futures::Past(language::ValueOrError<int>(Error(L"Foo"))),
                    [&](Error) { return Success(27); })
                .SetConsumer([&](language::ValueOrError<int> result) {
                  value = result;
                });
            CHECK(!value.value().IsError());
            CHECK_EQ(value.value().value(), 27);
          }},
     {.name = L"SkippedOnSuccess", .callback = [] {
        OnError(futures::Past(language::ValueOrError(Success(12))),
                [&](Error value) {
                  CHECK(false);
                  return value;
                });
      }}});
}  // namespace

}  // namespace afc::futures

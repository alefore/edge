#include "src/futures/futures.h"

#include <variant>

#include "glog/logging.h"
#include "src/language/error/value_or_error.h"
#include "src/tests/tests.h"

namespace afc::futures {
using language::Error;
using language::PossibleError;
using language::Success;

Value<language::ValueOrError<language::EmptyValue>> IgnoreErrors(
    Value<PossibleError> value) {
  Future<PossibleError> output;
  std::move(value).SetConsumer(
      [consumer = std::move(output.consumer)](const PossibleError&) {
        consumer(Success());
      });
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
               std::move(inner_value.value)
                   .Transform([](bool) {
                     CHECK(false);
                     return Success(true);
                   })
                   .SetConsumer([&](language::ValueOrError<bool> result) {
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
               std::move(inner_value.value)
                   .Transform([](bool) { return Success(true); })
                   .SetConsumer([&](language::ValueOrError<bool> result) {
                     final_result = result;
                   });
               inner_value.consumer(Error(L"xyz"));
               CHECK(final_result.has_value());
               CHECK_EQ(std::get<Error>(final_result.value()), Error(L"xyz"));
             }},
        {.name = L"CanConvertToParentWithPreviousValue",
         .callback =
             [] {
               using V = std::variant<int, double, bool>;
               Value<int> int_value = Past(5);
               Value<V> variant_value = std::move(int_value);
               std::optional<V> immediate_value = variant_value.Get();
               CHECK(immediate_value.has_value());
               CHECK_EQ(*std::get_if<int>(&immediate_value.value()), 5);
             }},
        {.name = L"CanConvertToParentAndReceive",
         .callback =
             [] {
               using V = std::variant<int, double, bool>;
               Future<int> int_future;
               Value<V> variant_value = std::move(int_future.value);
               CHECK(!variant_value.Get().has_value());
               int_future.consumer(6);
               std::optional<V> immediate_value = variant_value.Get();
               CHECK_EQ(*std::get_if<int>(&immediate_value.value()), 6);
               std::optional<V> value_received;
               std::move(variant_value).SetConsumer([&](V v) {
                 value_received = v;
               });
               CHECK(value_received.has_value());
               CHECK_EQ(*std::get_if<int>(&*value_received), 6);
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
                  CHECK_EQ(error, Error(L"Foo"));
                  return futures::Past(error);
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
                    [&](Error) { return futures::Past(Success(27)); })
                .SetConsumer([&](language::ValueOrError<int> result) {
                  value = result;
                });
            CHECK_EQ(std::get<int>(value.value()), 27);
          }},
     {.name = L"SkippedOnSuccess", .callback = [] {
        OnError(futures::Past(Success(12)), [&](Error value) {
          CHECK(false);
          return futures::Past(value);
        });
      }}});

const bool double_registration_tests_registration = tests::Register(
    L"FuturesDoubleConsumer",
    {{.name = L"DoubleConsumer",
      .callback =
          [] {
            futures::Future<int> object;
            object.consumer(0);
            CHECK(object.value.Get().has_value());
            CHECK(object.value.has_value());
            tests::ForkAndWaitForFailure([&] { object.consumer(0); });
          }},
     {.name = L"DoubleConsumerWithGet", .callback = [] {
        futures::Future<int> object;
        object.consumer(0);
        CHECK(object.value.Get().has_value());
        CHECK(object.value.has_value());
        tests::ForkAndWaitForFailure([&] { object.consumer(0); });
      }}});
}  // namespace

}  // namespace afc::futures

#include "src/futures/futures.h"

#include <ranges>
#include <variant>
#include <vector>

#include "glog/logging.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::ValueOrDie;
using afc::language::lazy_string::LazyString;

namespace afc::futures {

PossibleError IgnoreErrors(PossibleError value) {
  Future<language::PossibleError> output;
  std::move(value).SetConsumer([consumer = std::move(output.consumer)](
                                   const language::PossibleError&) mutable {
    std::move(consumer)(Success());
  });
  return std::move(output.value);
}

namespace {
const bool futures_ignore_errors_tests_registration = tests::Register(
    L"futures::IgnoreErrors",
    {
        {.name = L"Success",
         .callback =
             [] {
               bool run = false;
               IgnoreErrors(futures::PossibleError{EmptyValue{}})
                   .Transform([&run](EmptyValue) -> futures::PossibleError {
                     run = true;
                     return EmptyValue{};
                   });
               CHECK(run);
             }},
        {.name = L"Error",
         .callback =
             [] {
               bool run = false;
               IgnoreErrors(PossibleError{Error{L"Something bad happened"}})
                   .Transform([&run](EmptyValue) -> PossibleError {
                     run = true;
                     return EmptyValue{};
                   });
               CHECK(run);
             }},
        {.name = L"SanityCheck",
         .callback =
             [] {
               PossibleError(Error{L"Something bad happened"})
                   .Transform([](EmptyValue) -> PossibleError {
                     CHECK(false);
                     return EmptyValue{};
                   });
             }},
    });

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
               std::move(inner_value.consumer)(Error{LazyString{L"xyz"}});
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
               std::move(inner_value.consumer)(Error{LazyString{L"xyz"}});
               CHECK(final_result.has_value());
               CHECK_EQ(GetError(final_result.value()),
                        Error{LazyString{L"xyz"}});
             }},
        {.name = L"CanConvertToParentWithPreviousValue",
         .callback =
             [] {
               using V = std::variant<int, double, bool>;
               Value<int> int_value = 5;
               Value<V> variant_value = std::move(int_value);
               std::optional<V> immediate_value = variant_value.Get();
               CHECK(immediate_value.has_value());
               CHECK_EQ(std::get<int>(immediate_value.value()), 5);
             }},
        {.name = L"CanConvertToParentAndReceive",
         .callback =
             [] {
               using V = std::variant<int, double, bool>;
               Future<int> int_future;
               Value<V> variant_value = std::move(int_future.value);
               CHECK(!variant_value.Get().has_value());
               std::move(int_future.consumer)(6);
               std::optional<V> immediate_value = variant_value.Get();
               CHECK_EQ(std::get<int>(immediate_value.value()), 6);
               std::optional<V> value_received;
               std::move(variant_value).SetConsumer([&](V v) {
                 value_received = v;
               });
               CHECK(value_received.has_value());
               CHECK_EQ(std::get<int>(*value_received), 6);
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
                  CHECK_EQ(error, Error{L"Foo"});
                  return error;
                });
            CHECK(!executed);
            std::move(internal.consumer)(Error{L"Foo"});
            CHECK(executed);
          }},
     {.name = L"OverridesReturnedValue",
      .callback =
          [] {
            std::optional<language::ValueOrError<int>> value;
            OnError(futures::ValueOrError<int>{Error{L"Foo"}},
                    [&](Error) -> language::ValueOrError<int> { return 27; })
                .SetConsumer([&](language::ValueOrError<int> result) {
                  value = result;
                });
            CHECK_EQ(ValueOrDie(value.value()), 27);
          }},
     {.name = L"SkippedOnSuccess", .callback = [] {
        OnError(futures::ValueOrError<int>{12},
                [&](Error value) -> language::ValueOrError<int> {
                  CHECK(false);
                  return value;
                });
      }}});

const bool double_registration_tests_registration = tests::Register(
    L"FuturesDoubleConsumer",
    {{.name = L"DoubleConsumer",
      .callback =
          [] {
            futures::Future<int> object;
            std::move(object.consumer)(0);
            CHECK(object.value.Get().has_value());
            CHECK(object.value.has_value());
            tests::ForkAndWaitForFailure(
                [&] { std::move(object.consumer)(0); });
          }},
     {.name = L"DoubleConsumerWithGet", .callback = [] {
        futures::Future<int> object;
        std::move(object.consumer)(0);
        CHECK(object.value.Get().has_value());
        CHECK(object.value.has_value());
        tests::ForkAndWaitForFailure([&] { std::move(object.consumer)(0); });
      }}});

const bool futures_loop_tests_registration = tests::Register(
    L"FuturesLoop",
    {{.name = L"ForEach",
      .callback =
          [] {
            ForEach(MakeNonNullShared<std::vector<int>>(
                        std::views::iota(0, 1000000) |
                        std::ranges::to<std::vector>()),
                    [](int) -> futures::Value<IterationControlCommand> {
                      return IterationControlCommand::Continue;
                    });
          }},
     {.name = L"While", .callback = [] {
        size_t count = 0;
        While([&count] -> futures::Value<IterationControlCommand> {
          return count++ < 1e6 ? IterationControlCommand::Continue
                               : IterationControlCommand::Stop;
        });
      }}});

}  // namespace

}  // namespace afc::futures

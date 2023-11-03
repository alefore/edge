#include "src/futures/serializer.h"

#include <utility>

#include "glog/logging.h"
#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/tests/tests.h"

using afc::language::EmptyValue;

namespace afc::futures {
void Serializer::Push(Callback callback) {
  futures::Future<language::EmptyValue> new_future;
  // Why not just use something like:
  //
  //     last_execution_ = std::move(last_execution_).Transform(...)?
  //
  // Because `Push` needs to be reentrant. This means we must store the new
  // future value in `last_execution_` before we allow the consumer to start
  // running.
  std::exchange(last_execution_, std::move(new_future.value))
      .SetConsumer(
          [callback = std::move(callback),
           consumer = std::move(new_future.consumer)](language::EmptyValue) {
            callback().SetConsumer(consumer);
          });
}

namespace {
const bool tests_registration = tests::Register(
    L"FuturesSerializer",
    {{.name = L"Empty", .callback = [] { Serializer(); }},
     {.name = L"Sync",
      .callback =
          [] {
            Serializer serializer;
            std::vector<size_t> calls;
            for (size_t i = 0; i < 5; i++)
              serializer.Push([&calls, i] {
                calls.push_back(i);
                return futures::Past(EmptyValue());
              });
            CHECK(calls == std::vector<size_t>({0, 1, 2, 3, 4}));
          }},
     {.name = L"Async",
      .callback =
          [] {
            Serializer serializer;
            std::vector<size_t> calls;
            std::vector<Future<EmptyValue>> futures(6);
            for (size_t i = 0; i < futures.size(); i++)
              serializer.Push([&calls, &futures, i] {
                calls.push_back(i);
                return std::move(futures[i].value);
              });
            CHECK(calls == std::vector<size_t>({0}));
            futures[0].consumer(EmptyValue());
            CHECK(calls == std::vector<size_t>({0, 1}));
            futures[2].consumer(EmptyValue());
            futures[3].consumer(EmptyValue());
            CHECK(calls == std::vector<size_t>({0, 1}));
            futures[1].consumer(EmptyValue());
            CHECK(calls == std::vector<size_t>({0, 1, 2, 3, 4}));
            futures[4].consumer(EmptyValue());
            CHECK(calls == std::vector<size_t>({0, 1, 2, 3, 4, 5}));
            serializer.Push([&calls] {
              calls.push_back(6);
              return futures::Past(EmptyValue());
            });
            CHECK(calls == std::vector<size_t>({0, 1, 2, 3, 4, 5}));
            futures[5].consumer(EmptyValue());
            CHECK(calls == std::vector<size_t>({0, 1, 2, 3, 4, 5, 6}));
          }},
     {.name = L"Reentrant", .callback = [] {
        Serializer serializer;
        std::vector<Future<EmptyValue>> futures(4);
        std::vector<size_t> calls;
        serializer.Push([&] {
          calls.push_back(0);
          serializer.Push([&] {
            calls.push_back(1);
            return std::move(futures[1].value);
          });
          return std::move(futures[0].value);
        });
        serializer.Push([&] {
          calls.push_back(2);
          serializer.Push([&] {
            calls.push_back(3);
            return std::move(futures[3].value);
          });
          return std::move(futures[2].value);
        });
        CHECK(calls == std::vector<size_t>({0}));
        futures[0].consumer(EmptyValue());
        CHECK(calls == std::vector<size_t>({0, 1}));
        futures[1].consumer(EmptyValue());
        CHECK(calls == std::vector<size_t>({0, 1, 2}));
        futures[2].consumer(EmptyValue());
        CHECK(calls == std::vector<size_t>({0, 1, 2, 3}));
        futures[3].consumer(EmptyValue());
      }}});
}
}  // namespace afc::futures

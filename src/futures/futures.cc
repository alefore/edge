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
class OnErrorTests : public tests::TestGroup<OnErrorTests> {
 public:
  OnErrorTests() : TestGroup<OnErrorTests>() {}
  std::wstring Name() const override { return L"OnErrorTests"; }
  std::vector<tests::Test> Tests() const override {
    return {{.name = L"WaitsForFuture",
             .callback =
                 [] {
                   Future<editor::ValueOrError<int>> internal;
                   bool executed = false;
                   auto external = OnError(
                       internal.value, [&](editor::ValueOrError<int> value) {
                         executed = true;
                         CHECK(value.error.has_value());
                         CHECK(value.error.value() == L"Foo");
                         return value;
                       });
                   CHECK(!executed);
                   internal.consumer(editor::ValueOrError<int>::Error(L"Foo"));
                   CHECK(executed);
                 }},
            {.name = L"OverridesReturnedValue",
             .callback =
                 [] {
                   std::optional<editor::ValueOrError<int>> value;
                   OnError(
                       futures::Past(editor::ValueOrError<int>::Error(L"Foo")),
                       [&](editor::ValueOrError<int>) {
                         return editor::ValueOrError<int>::Value(27);
                       })
                       .SetConsumer([&](editor::ValueOrError<int> result) {
                         value = result;
                       });
                   CHECK(value.value().value.has_value());  // Lovely...
                   CHECK_EQ(value.value().value.value(), 27);
                 }},
            {.name = L"SkippedOnSuccess", .callback = [] {
               OnError(futures::Past(editor::ValueOrError<int>::Value(12)),
                       [&](editor::ValueOrError<int> value) {
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

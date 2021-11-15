#include "src/async_processor.h"

#include <cctype>
#include <ostream>

#include "src/notification.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
std::unique_ptr<BackgroundCallbackRunner> NewBackgroundCallbackRunner(
    std::wstring name,
    BackgroundCallbackRunner::Options::QueueBehavior push_behavior) {
  BackgroundCallbackRunner::Options options;
  options.name = std::move(name);
  options.push_behavior = push_behavior;
  options.factory = [](BackgroundCallbackRunner::InputType input) {
    input();
    return 0;  // Ignored.
  };
  return std::make_unique<BackgroundCallbackRunner>(std::move(options));
}

const bool async_evaluator_tests_registration = tests::Register(
    L"AsyncEvaluatorTests",
    {{.name = L"Empty",
      .callback =
          [] {
            WorkQueue queue([] {});
            AsyncEvaluator(L"Test", &queue);
          }},
     {.name = L"EvaluatorDeleteWhileBusy",
      .callback =
          [] {
            std::optional<int> future_result;
            WorkQueue queue([] {});

            Notification started_running;
            Notification proceed;

            auto evaluator = std::make_unique<AsyncEvaluator>(L"Test", &queue);
            evaluator
                ->Run([&] {
                  started_running.Notify();
                  proceed.WaitForNotification();
                  sleep(1);
                  return 948;
                })
                .Transform([&](int result) {
                  future_result = result;
                  return EmptyValue();
                });

            started_running.WaitForNotification();
            LOG(INFO) << "Deleting.";
            proceed.Notify();
            evaluator = nullptr;

            CHECK(queue.NextExecution().has_value());
            CHECK(!future_result.has_value());
            queue.Execute();
            CHECK(!queue.NextExecution().has_value());
            CHECK(future_result.has_value());
            CHECK_EQ(future_result.value(), 948);
          }},
     // Tests that the WorkQueue instance can be deleted while calls to
     // `RunIgnoringResults` are ongoing.
     {.name = L"EvaluatorDeleteWhileBusyIgnoringResults", .callback = [] {
        auto queue = std::make_unique<WorkQueue>([] {});

        Notification started_running;
        Notification proceed;
        Notification completed;

        auto evaluator = std::make_unique<AsyncEvaluator>(L"Test", queue.get());
        evaluator->RunIgnoringResults([&] {
          started_running.Notify();
          proceed.WaitForNotification();
          sleep(1);
          completed.Notify();
        });

        started_running.WaitForNotification();
        LOG(INFO) << "Deleting.";
        queue = nullptr;
        proceed.Notify();
        evaluator = nullptr;
        CHECK(completed.HasBeenNotified());
      }}});
}  // namespace

AsyncEvaluator::AsyncEvaluator(
    std::wstring name, WorkQueue* work_queue,
    BackgroundCallbackRunner::Options::QueueBehavior push_behavior)
    : background_callback_runner_(
          NewBackgroundCallbackRunner(name, push_behavior)),
      work_queue_(work_queue) {}
}  // namespace afc::editor

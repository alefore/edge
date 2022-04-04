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
            auto queue = WorkQueue::New([] {});
            AsyncEvaluator(L"Test", queue);
          }},
     {.name = L"EvaluatorDeleteWhileBusy",
      .callback =
          [] {
            std::optional<int> future_result;
            auto queue = WorkQueue::New([] {});

            Notification started_running;
            Notification proceed;

            auto evaluator = std::make_unique<AsyncEvaluator>(L"Test", queue);
            evaluator
                ->Run([&] {
                  started_running.Notify();
                  proceed.WaitForNotification();
                  return 948;
                })
                .Transform([&](int result) {
                  future_result = result;
                  return EmptyValue();
                });

            started_running.WaitForNotification();
            LOG(INFO) << "Deleting.";
            evaluator = nullptr;
            proceed.Notify();

            CHECK(!future_result.has_value());
            while (!queue->NextExecution().has_value()) sleep(0.1);
            queue->Execute();
            CHECK(!queue->NextExecution().has_value());
            CHECK(future_result.has_value());
            CHECK_EQ(future_result.value(), 948);
          }},
     // Tests that the WorkQueue instance can be deleted while calls to
     // `RunIgnoringResults` are ongoing.
     {.name = L"EvaluatorDeleteWhileBusyIgnoringResults", .callback = [] {
        Notification started_running;
        Notification proceed;
        Notification completed;

        auto evaluator =
            std::make_unique<AsyncEvaluator>(L"Test", WorkQueue::New([] {}));
        evaluator->RunIgnoringResults([&] {
          started_running.Notify();
          proceed.WaitForNotification();
          sleep(0.1);
          completed.Notify();
        });

        started_running.WaitForNotification();
        proceed.Notify();
        evaluator = nullptr;
        CHECK(completed.HasBeenNotified());
      }}});
}  // namespace

AsyncEvaluator::AsyncEvaluator(
    std::wstring name, std::shared_ptr<WorkQueue> work_queue,
    BackgroundCallbackRunner::Options::QueueBehavior push_behavior)
    : background_callback_runner_(
          NewBackgroundCallbackRunner(name, push_behavior)),
      work_queue_(std::move(work_queue)) {}
}  // namespace afc::editor

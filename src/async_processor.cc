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
     {.name = L"EvaluatorDeleteWithMultipleRequests",
      .callback =
          [] {
            auto queue = WorkQueue::New([] {});
            auto evaluator = std::make_unique<AsyncEvaluator>(L"Test", queue);

            auto started_running = std::make_shared<Notification>();
            auto proceed = std::make_shared<Notification>();

            for (int i = 0; i < 10; i++)
              evaluator->Run([i, started_running, proceed] {
                LOG(INFO) << "Starting: " << i;
                started_running->Notify();
                proceed->WaitForNotification();
                return i;
              });

            std::optional<int> future_result;
            evaluator->Run([] { return 900; }).Transform([&](int result) {
              LOG(INFO) << "Received final result.";
              future_result = result;
              return EmptyValue();
            });

            started_running->WaitForNotification();
            evaluator = nullptr;
            LOG(INFO) << "Deleted.";
            proceed->Notify();

            CHECK(!future_result.has_value());
            while (future_result != 900) {
              queue->Execute();
              sleep(0.01);
            }
            CHECK(!queue->NextExecution().has_value());
          }},
     {.name = L"EvaluatorDeleteWhileBusy", .callback = [] {
        std::optional<int> future_result;
        auto queue = WorkQueue::New([] {});

        Notification started_running;
        Notification proceed;

        AsyncEvaluator(L"Test", queue)
            .Run([&] {
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
        proceed.Notify();

        CHECK(!future_result.has_value());
        while (!queue->NextExecution().has_value()) sleep(0.1);
        queue->Execute();
        CHECK(!queue->NextExecution().has_value());
        CHECK(future_result.has_value());
        CHECK_EQ(future_result.value(), 948);
      }}});
}  // namespace

AsyncEvaluator::AsyncEvaluator(
    std::wstring name, std::shared_ptr<WorkQueue> work_queue,
    BackgroundCallbackRunner::Options::QueueBehavior push_behavior)
    : background_callback_runner_(
          NewBackgroundCallbackRunner(name, push_behavior)),
      work_queue_(std::move(work_queue)) {}
}  // namespace afc::editor

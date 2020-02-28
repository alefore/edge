#include "src/async_processor.h"

#include <cctype>
#include <ostream>

namespace afc::editor {
namespace {
BackgroundCallbackRunner NewBackgroundCallbackRunner(
    std::wstring name,
    BackgroundCallbackRunner::Options::QueueBehavior push_behavior) {
  BackgroundCallbackRunner::Options options;
  options.name = std::move(name);
  options.push_behavior = push_behavior;
  options.factory = [](BackgroundCallbackRunner::InputType input) {
    input();
    return 0;  // Ignored.
  };
  return BackgroundCallbackRunner(std::move(options));
}
}  // namespace

AsyncEvaluator::AsyncEvaluator(
    std::wstring name, WorkQueue* work_queue,
    BackgroundCallbackRunner::Options::QueueBehavior push_behavior)
    : background_callback_runner_(
          NewBackgroundCallbackRunner(name, push_behavior)),
      work_queue_(work_queue) {}

}  // namespace afc::editor

#include "src/async_processor.h"

#include <cctype>
#include <ostream>

namespace afc::editor {

BackgroundCallbackRunner NewBackgroundCallbackRunner(std::wstring name) {
  BackgroundCallbackRunner::Options options;
  options.name = std::move(name);
  options.push_behavior =
      BackgroundCallbackRunner::Options::QueueBehavior::kWait;
  options.factory = [](BackgroundCallbackRunner::InputType input) {
    input();
    return 0;  // Ignored.
  };
  return BackgroundCallbackRunner(std::move(options));
}

}  // namespace afc::editor

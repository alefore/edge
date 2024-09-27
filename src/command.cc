#include "src/command.h"

#include <glog/logging.h>

using afc::language::lazy_string::LazyString;

namespace afc::editor {
Command::CursorMode Command::cursor_mode() const {
  return CursorMode::kDefault;
}

/* static */ const CommandCategory& CommandCategory::kBuffers() {
  static const CommandCategory* const output =
      new CommandCategory{LazyString{L"Buffers"}};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kEditor() {
  static const CommandCategory* const output =
      new CommandCategory{LazyString{L"Editor"}};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kEdit() {
  static const CommandCategory* const output =
      new CommandCategory{LazyString{L"Edit"}};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kExtensions() {
  static const CommandCategory* const output =
      new CommandCategory{LazyString{L"Extensions"}};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kNavigate() {
  static const CommandCategory* const output =
      new CommandCategory{LazyString{L"Navigate"}};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kPrompt() {
  static const CommandCategory* const output =
      new CommandCategory{LazyString{L"Prompt"}};
  return *output;
}

}  // namespace afc::editor

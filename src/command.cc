#include "src/command.h"

#include <glog/logging.h>

using afc::language::lazy_string::LazyString;

namespace afc::editor {
Command::CursorMode Command::cursor_mode() const {
  return CursorMode::kDefault;
}

/* static */ const CommandCategory& CommandCategory::kBuffers() {
  static const CommandCategory* const output =
      new CommandCategory{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Buffers")};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kCppFunctions() {
  static const CommandCategory* const output = new CommandCategory{
      NON_EMPTY_SINGLE_LINE_CONSTANT(L"C++ Functions (Extensions)")};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kEditor() {
  static const CommandCategory* const output =
      new CommandCategory{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Editor")};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kEdit() {
  static const CommandCategory* const output =
      new CommandCategory{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Edit")};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kExtensions() {
  static const CommandCategory* const output =
      new CommandCategory{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Extensions")};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kModifiers() {
  static const CommandCategory* const output =
      new CommandCategory{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Modifiers")};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kNavigate() {
  static const CommandCategory* const output =
      new CommandCategory{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Navigate")};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kPrompt() {
  static const CommandCategory* const output =
      new CommandCategory{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Prompt")};
  return *output;
}

/* static */ const CommandCategory& CommandCategory::kView() {
  static const CommandCategory* const output =
      new CommandCategory{NON_EMPTY_SINGLE_LINE_CONSTANT(L"View")};
  return *output;
}

}  // namespace afc::editor

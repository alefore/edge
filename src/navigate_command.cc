#include "src/navigate_command.h"

#include <glog/logging.h>

#include <map>
#include <memory>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/editor_mode.h"

namespace afc {
namespace editor {

using std::unique_ptr;

namespace {
class NavigateMode : public EditorMode {
 public:
  NavigateMode(Modifiers modifiers) : modifiers_(std::move(modifiers)) {}
  virtual ~NavigateMode() {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      LOG(INFO) << "NavigateMode gives up: No current buffer.";
      return;
    }

    switch (c) {
      case 'l':
        Navigate(buffer.get(), modifiers_.direction);
        break;

      case 'h':
        Navigate(buffer.get(), ReverseDirection(modifiers_.direction));
        break;

      default:
        buffer->ResetMode();
        editor_state->ProcessInput(c);
    }

    editor_state->ScheduleRedraw();
  }

 protected:
  // Returns the first position in the range.
  virtual size_t InitialStart(OpenBuffer* buffer) = 0;

  // Returns the last position in the range.
  virtual size_t InitialEnd(OpenBuffer* buffer) = 0;

  // Sets the current position.
  virtual void SetTarget(OpenBuffer* buffer, size_t target) = 0;

  // Returns the current position.
  virtual size_t GetCurrent(OpenBuffer* buffer) = 0;

 private:
  void Navigate(OpenBuffer* buffer, Direction direction) {
    size_t new_target;
    if (direction == FORWARDS) {
      if (!already_moved_) {
        already_moved_ = true;
        end_ = InitialEnd(buffer);
        LOG(INFO) << "Navigating forward; end: " << end_;
      }
      start_ = GetCurrent(buffer);
      new_target = (start_ + end_) / 2;
      if (new_target == start_ && new_target < InitialEnd(buffer)) {
        start_++;
        end_++;
        new_target++;
      }
    } else {
      if (!already_moved_) {
        already_moved_ = true;
        start_ = InitialStart(buffer);
        LOG(INFO) << "Navigating backward; start: " << start_;
      }
      end_ = GetCurrent(buffer);
      new_target = (start_ + end_) / 2;
      if (new_target == end_ && new_target > InitialStart(buffer)) {
        start_--;
        end_--;
        new_target--;
      }
    }
    SetTarget(buffer, new_target);
  }

  const Modifiers modifiers_;

  bool already_moved_ = false;
  size_t start_ = 0;
  size_t end_ = 0;
};

class NavigateModeChar : public NavigateMode {
 public:
  NavigateModeChar(Modifiers modifiers) : NavigateMode(modifiers) {}

 protected:
  size_t InitialStart(OpenBuffer*) override { return 0; }

  size_t InitialEnd(OpenBuffer* buffer) override {
    return buffer->current_line()->size();
  }

  void SetTarget(OpenBuffer* buffer, size_t target) override {
    LineColumn position = buffer->position();
    position.column = target;
    buffer->set_position(position);
  }

  size_t GetCurrent(OpenBuffer* buffer) override {
    return buffer->position().column;
  }
};

class NavigateModeSymbol : public NavigateMode {
 public:
  NavigateModeSymbol(Modifiers modifiers) : NavigateMode(modifiers) {}

 protected:
  size_t InitialStart(OpenBuffer* buffer) override {
    wstring contents = buffer->current_line()->ToString();
    size_t previous_space = contents.find_last_not_of(
        buffer->Read(buffer_variables::symbol_characters()),
        buffer->position().column);
    if (previous_space == wstring::npos) {
      return 0;
    }
    return previous_space + 1;
  }

  size_t InitialEnd(OpenBuffer* buffer) override {
    wstring contents = buffer->current_line()->ToString();
    size_t next_space = contents.find_first_not_of(
        buffer->Read(buffer_variables::symbol_characters()),
        buffer->position().column);
    if (next_space == wstring::npos) {
      return buffer->current_line()->size();
    }
    return next_space;
  }

  void SetTarget(OpenBuffer* buffer, size_t target) override {
    LineColumn position = buffer->position();
    position.column = target;
    buffer->set_position(position);
  }

  size_t GetCurrent(OpenBuffer* buffer) override {
    return buffer->position().column;
  }
};

class NavigateModeLine : public NavigateMode {
 public:
  NavigateModeLine(Modifiers modifiers) : NavigateMode(modifiers) {}

 protected:
  size_t InitialStart(OpenBuffer*) override { return 0; }

  size_t InitialEnd(OpenBuffer* buffer) override {
    return buffer->contents()->size();
  }

  void SetTarget(OpenBuffer* buffer, size_t target) override {
    LineColumn position = buffer->position();
    position.line = target;
    buffer->set_position(position);
  }

  size_t GetCurrent(OpenBuffer* buffer) override {
    return buffer->position().line;
  }
};

class NavigateCommand : public Command {
 public:
  NavigateCommand() {}

  wstring Description() const override { return L"activates navigate mode."; }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    auto structure = editor_state->modifiers().structure;
    // TODO: Move to Structure.
    if (structure == StructureChar()) {
      buffer->set_mode(
          std::make_unique<NavigateModeChar>(editor_state->modifiers()));
    } else if (structure == StructureSymbol()) {
      buffer->set_mode(
          std::make_unique<NavigateModeSymbol>(editor_state->modifiers()));
    } else if (structure == StructureLine()) {
      buffer->set_mode(
          std::make_unique<NavigateModeLine>(editor_state->modifiers()));
    } else {
      editor_state->SetStatus(L"Navigate not handled for current mode.");
      buffer->ResetMode();
    }
  }

 private:
  const map<wchar_t, Command*> commands_;
  const wstring mode_description_;
};

}  // namespace

std::unique_ptr<Command> NewNavigateCommand() {
  return std::make_unique<NavigateCommand>();
}

}  // namespace editor
}  // namespace afc

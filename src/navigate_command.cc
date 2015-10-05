#include "navigate_command.h"

#include <cassert>
#include <memory>
#include <map>

#include "char_buffer.h"
#include "direction.h"
#include "editor.h"
#include "editor_mode.h"

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
    if (buffer == editor_state->buffers()->end()) {
      LOG(INFO) << "NavigateMode gives up: No current buffer.";
      editor_state->ResetMode();
      editor_state->ProcessInput(c);
      return;
    }

    switch (c) {
      case 'l':
        Navigate(buffer->second.get(), modifiers_.direction);
        break;

      case 'h':
        Navigate(buffer->second.get(), ReverseDirection(modifiers_.direction));
        break;

      default:
        editor_state->ResetMode();
        editor_state->ProcessInput(c);
    }
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
  size_t InitialStart(OpenBuffer*) override {
    return 0;
  }

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

class NavigateModeWord : public NavigateMode {
 public:
  NavigateModeWord(Modifiers modifiers) : NavigateMode(modifiers) {}

 protected:
  size_t InitialStart(OpenBuffer* buffer) override {
    wstring contents = buffer->current_line()->ToString();
    size_t previous_space = contents.find_last_not_of(
        buffer->read_string_variable(OpenBuffer::variable_word_characters()),
        buffer->position().column);
    if (previous_space == wstring::npos) {
      return 0;
    }
    return previous_space + 1;
  }

  size_t InitialEnd(OpenBuffer* buffer) override {
    wstring contents = buffer->current_line()->ToString();
    size_t next_space = contents.find_first_not_of(
        buffer->read_string_variable(OpenBuffer::variable_word_characters()),
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
  size_t InitialStart(OpenBuffer*) override {
    return 0;
  }

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

  const wstring Description() {
    return L"activates navigate mode.";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto structure = editor_state->modifiers().structure;
    switch (structure) {
      case CHAR:
        editor_state->set_mode(std::unique_ptr<NavigateMode>(
            new NavigateModeChar(editor_state->modifiers())));
        break;

      case WORD:
        editor_state->set_mode(std::unique_ptr<NavigateMode>(
            new NavigateModeWord(editor_state->modifiers())));
        break;

      case LINE:
        editor_state->set_mode(std::unique_ptr<NavigateMode>(
            new NavigateModeLine(editor_state->modifiers())));
        break;

      default:
        editor_state->SetStatus(L"Navigate not handled for current mode.");
        editor_state->ResetMode();
    }
  }

 private:
  const map<wchar_t, Command*> commands_;
  const wstring mode_description_;
};

}  // namespace

std::unique_ptr<Command> NewNavigateCommand() {
  return std::unique_ptr<NavigateCommand>(new NavigateCommand());
}

}  // namespace editor
}  // namespace afc

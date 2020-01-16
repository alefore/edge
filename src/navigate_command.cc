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
struct SearchRange {
  size_t begin;
  size_t end;
};

size_t MidPoint(const SearchRange& r) { return (r.begin + r.end) / 2; }

struct NavigateOptions {
  // Returns the initial range containing a given position.
  std::function<SearchRange(const OpenBuffer*, LineColumn)> initial_range;

  // Makes a new position, adjusting an existing position.
  std::function<LineColumn(LineColumn, size_t)> write_index;

  std::function<size_t(LineColumn)> position_to_index;
};

class NavigateMode : public EditorMode {
 public:
  NavigateMode(NavigateOptions options, Modifiers modifiers)
      : options_(std::move(options)), modifiers_(std::move(modifiers)) {}
  virtual ~NavigateMode() = default;

  void ProcessInput(wint_t c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      LOG(INFO) << "NavigateMode gives up: No current buffer.";
      return;
    }

    switch (c) {
      case 'l':
      case 'h':
        Navigate(buffer.get(), c == 'l'
                                   ? modifiers_.direction
                                   : ReverseDirection(modifiers_.direction));
        break;

      default:
        buffer->ResetMode();
        editor_state->ProcessInput(c);
    }
  }

 private:
  void Navigate(OpenBuffer* buffer, Direction direction) {
    size_t index;
    if (direction == FORWARDS) {
      if (!initial_range_.has_value()) {
        initial_range_ = options_.initial_range(buffer, buffer->position());
        range_.end = initial_range_.value().end;
        LOG(INFO) << "Navigating forward; end: " << range_.end;
      }
      range_.begin = options_.position_to_index(buffer->position());
      index = MidPoint(range_);
      if (index == range_.begin && index < initial_range_.value().end) {
        range_.begin++;
        range_.end++;
        index++;
      }
    } else {
      if (!initial_range_.has_value()) {
        initial_range_ = options_.initial_range(buffer, buffer->position());
        range_.begin = initial_range_.value().begin;
        LOG(INFO) << "Navigating backward; start: " << range_.begin;
      }
      range_.end = options_.position_to_index(buffer->position());
      index = MidPoint(range_);
      if (index == range_.end && index > initial_range_.value().end) {
        range_.begin--;
        range_.end--;
        index--;
      }
    }
    buffer->set_position(options_.write_index(buffer->position(), index));
  }

  const NavigateOptions options_;
  const Modifiers modifiers_;

  // Starts empty and gets initialized when we first move.
  std::optional<SearchRange> initial_range_;
  SearchRange range_;
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
      NavigateOptions options;
      options.initial_range = [](const OpenBuffer* buffer,
                                 LineColumn position) {
        return SearchRange{0,
                           buffer->LineAt(position.line)->EndColumn().column};
      };
      options.write_index = [](LineColumn position, size_t target) {
        position.column = ColumnNumber(target);
        return position;
      };
      options.position_to_index = [](LineColumn position) {
        return position.column.column;
      };
      buffer->set_mode(std::make_unique<NavigateMode>(
          std::move(options), editor_state->modifiers()));
    } else if (structure == StructureSymbol()) {
      NavigateOptions options;
      options.initial_range = [](const OpenBuffer* buffer,
                                 LineColumn position) {
        auto contents = buffer->LineAt(position.line);
        auto contents_str = contents->ToString();
        SearchRange output;

        size_t previous_space = contents_str.find_last_not_of(
            buffer->Read(buffer_variables::symbol_characters),
            buffer->position().column.column);
        output.begin = previous_space == wstring::npos ? 0 : previous_space + 1;

        size_t next_space = contents_str.find_first_not_of(
            buffer->Read(buffer_variables::symbol_characters),
            buffer->position().column.column);
        output.end = next_space == wstring::npos ? contents->EndColumn().column
                                                 : next_space;

        return output;
      };

      options.write_index = [](LineColumn position, size_t target) {
        position.column = ColumnNumber(target);
        return position;
      };

      options.position_to_index = [](LineColumn position) {
        return position.column.column;
      };
      buffer->set_mode(std::make_unique<NavigateMode>(
          std::move(options), editor_state->modifiers()));
    } else if (structure == StructureLine()) {
      NavigateOptions options;
      options.initial_range = [](const OpenBuffer* buffer, LineColumn) {
        return SearchRange{
            0, static_cast<size_t>(buffer->contents()->size().line_delta)};
      };
      options.write_index = [](LineColumn position, size_t target) {
        position.line = LineNumber(target);
        return position;
      };
      options.position_to_index = [](LineColumn position) {
        return position.line.line;
      };
      buffer->set_mode(std::make_unique<NavigateMode>(
          std::move(options), editor_state->modifiers()));
    } else {
      buffer->status()->SetInformationText(
          L"Navigate not handled for current mode.");
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

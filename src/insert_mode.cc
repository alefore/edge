#include "insert_mode.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>

extern "C" {
#include <unistd.h>
}

#include <glog/logging.h>

#include "char_buffer.h"
#include "command_mode.h"
#include "editable_string.h"
#include "editor.h"
#include "editor_mode.h"
#include "file_link_mode.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "terminal.h"
#include "transformation_delete.h"
#include "transformation_move.h"
#include "tree.h"
#include "vm/public/value.h"
#include "wstring.h"

namespace {
using namespace afc::editor;

class NewLineTransformation : public Transformation {
  void Apply(EditorState* editor_state, OpenBuffer* buffer, Result* result)
      const override {
    const size_t column = result->cursor.column;
    auto line = buffer->LineAt(result->cursor.line);
    if (line == nullptr) {
      result->made_progress = false;
      return;
    }

    if (buffer->read_bool_variable(OpenBuffer::variable_atomic_lines())
        && column != 0
        && column != line->size()) {
      result->made_progress = false;
      return;
    }

    const wstring& line_prefix_characters(buffer->read_string_variable(
        OpenBuffer::variable_line_prefix_characters()));
    size_t prefix_end = 0;
    if (line != nullptr
        && !buffer->read_bool_variable(OpenBuffer::variable_paste_mode())) {
      while (prefix_end < column
             && (line_prefix_characters.find(line->get(prefix_end))
                 != line_prefix_characters.npos)) {
        prefix_end++;
      }
    }

    Line::Options continuation_options;
    continuation_options.contents = line->Substring(0, prefix_end);

    unique_ptr<TransformationStack> transformation(new TransformationStack);
    {
      shared_ptr<OpenBuffer> buffer_to_insert(
        new OpenBuffer(editor_state, L"- text inserted"));
      buffer_to_insert->AppendRawLine(
          editor_state, std::make_shared<Line>(continuation_options));
      transformation->PushBack(
          NewInsertBufferTransformation(buffer_to_insert, 1, END));
    }

    transformation->PushBack(NewDeleteSuffixSuperfluousCharacters());

    transformation->PushBack(NewGotoPositionTransformation(
        LineColumn(result->cursor.line + 1, prefix_end)));
    return transformation->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return unique_ptr<Transformation>(new NewLineTransformation());
  }
};

class InsertEmptyLineTransformation : public Transformation {
 public:
  InsertEmptyLineTransformation(Direction direction) : direction_(direction) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    if (direction_ == BACKWARDS) {
      result->cursor.line++;
    }
    result->cursor.column = 0;
    return ComposeTransformation(
        unique_ptr<Transformation>(new NewLineTransformation()),
        NewGotoPositionTransformation(result->cursor))
            ->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return unique_ptr<Transformation>(
        new InsertEmptyLineTransformation(direction_));
  }

 private:
  Direction direction_;
};


class AutocompleteMode : public EditorMode {
 public:
  using Iterator = Tree<std::shared_ptr<Line>>::const_iterator;

  struct Options {
    std::unique_ptr<EditorMode> delegate;

    std::shared_ptr<Line> prefix;

    // TODO: Make these shared_ptr weak_ptrs.
    std::shared_ptr<OpenBuffer> dictionary;
    std::shared_ptr<OpenBuffer> buffer;
    Iterator matches_start;
    size_t column_start;
    size_t column_end;
  };

  AutocompleteMode(Options options)
      : options_(std::move(options)),
        matches_current_(options_.matches_start),
        word_length_(options_.column_end - options_.column_start) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    if (c != '\t') {
      editor_state->set_mode(std::move(options_.delegate));
      editor_state->ProcessInput(c);
      return;
    }

    auto buffer_to_insert =
        std::make_shared<OpenBuffer>(editor_state, L"tmp buffer");
    buffer_to_insert->AppendToLastLine(
        editor_state,
        StringAppend((*matches_current_)->contents(), NewCopyString(L" ")));
    DLOG(INFO) << "Completion selected: " << buffer_to_insert->ToString();

    Modifiers modifiers;
    modifiers.repetitions = word_length_;
    options_.buffer->Apply(editor_state,
        ComposeTransformation(
            NewDeleteCharactersTransformation(modifiers, false),
            NewInsertBufferTransformation(buffer_to_insert, 1, END)),
        LineColumn(options_.buffer->position().line, options_.column_start));

    options_.buffer->set_modified(true);
    editor_state->ScheduleRedraw();

    LOG(INFO) << "Updating variables for next completion.";
    word_length_ = (*matches_current_)->size() + 1;
    ++matches_current_;
    if (matches_current_ == options_.dictionary->contents()->end()) {
      matches_current_ = options_.matches_start;
    }
  }

 private:
  Options options_;
  Iterator matches_current_;
  // The number of characters that need to be erased (starting at
  // options_.column_start) for the next insertion. Initially, this is computed
  // from options_.column_start and options_.column_end; however, after an
  // insertion, it gets updated with the length of the insertion.
  size_t word_length_;
};

class JumpTransformation : public Transformation {
 public:
  JumpTransformation(Direction direction) : direction_(direction) {}

  void Apply(EditorState* editor_state, OpenBuffer* buffer, Result* result)
      const override {
    CHECK(result);
    if (buffer->LineAt(result->cursor.line) == nullptr) {
      result->made_progress = false;
      return;
    }
    LineColumn position = result->cursor;
    switch (direction_) {
      case FORWARDS:
        position.column = buffer->current_line()->size();
        break;
      case BACKWARDS:
        position.column = 0;
        break;
    }
    NewGotoPositionTransformation(position)
        ->Apply(editor_state, buffer, result);
    // TODO: This probabily doesn't belong here.
    if (buffer->active_cursors()->size() > 1) {
      editor_state->ScheduleRedraw();
    }
    editor_state->ResetRepetitions();
    editor_state->ResetStructure();
    editor_state->ResetDirection();
  }

  unique_ptr<Transformation> Clone() {
    return std::unique_ptr<Transformation>(new JumpTransformation(direction_));
  }

 private:
  const Direction direction_;
};

class DefaultScrollBehavior : public ScrollBehavior {
 public:
  static shared_ptr<const ScrollBehavior> Get() {
    static const auto output = std::make_shared<DefaultScrollBehavior>();
    return output;
  }

  // Public for make_shared, but prefer DefaultScrollBehavior::Get.
  DefaultScrollBehavior() = default;

  void Up(EditorState*, OpenBuffer* buffer) const override {
    Modifiers modifiers;
    modifiers.direction = BACKWARDS;
    modifiers.structure = LINE;
    buffer->ApplyToCursors(NewMoveTransformation(modifiers));
  }

  void Down(EditorState*, OpenBuffer* buffer) const override {
    Modifiers modifiers;
    modifiers.structure = LINE;
    buffer->ApplyToCursors(NewMoveTransformation(modifiers));
  }

  void Left(EditorState*, OpenBuffer* buffer) const override {
    Modifiers modifiers;
    modifiers.direction = BACKWARDS;
    buffer->ApplyToCursors(NewMoveTransformation(modifiers));
  }

  void Right(EditorState*, OpenBuffer* buffer) const override {
    buffer->ApplyToCursors(NewMoveTransformation(Modifiers()));
  }

  void Begin(EditorState*, OpenBuffer* buffer) const override {
    buffer->ApplyToCursors(
        std::unique_ptr<Transformation>(new JumpTransformation(BACKWARDS)));
  }

  void End(EditorState*, OpenBuffer* buffer) const override {
    buffer->ApplyToCursors(
        std::unique_ptr<Transformation>(new JumpTransformation(FORWARDS)));
  }
};

void FindCompletion(EditorState* editor_state,
                    std::shared_ptr<OpenBuffer> buffer,
                    std::shared_ptr<OpenBuffer> dictionary) {
  if (buffer == nullptr || dictionary == nullptr) {
    LOG(INFO) << "Buffer or dictionary have expired, giving up.";
    return;
  }

  AutocompleteMode::Options options;

  auto line = buffer->current_line()->ToString();
  LOG(INFO) << "Extracting token from line: " << line;
  options.column_end = buffer->position().column;
  options.column_start = line.find_last_not_of(
      buffer->read_string_variable(OpenBuffer::variable_word_characters()),
      options.column_end);
  if (options.column_start == wstring::npos) {
    options.column_start = 0;
  } else {
    options.column_start++;
  }
  options.prefix = std::make_shared<Line>(Line::Options(NewCopyString(
      line.substr(options.column_start,
                  options.column_end - options.column_start))));

  options.delegate = editor_state->ResetMode();
  options.dictionary = dictionary;
  options.buffer = buffer;

  LOG(INFO) << "Find completion for \"" << options.prefix->ToString()
            << "\" among options: " << dictionary->contents()->size();
  options.matches_start = std::lower_bound(
      dictionary->contents()->begin(),
      dictionary->contents()->end(),
      options.prefix,
      [](const std::shared_ptr<Line>& a, const std::shared_ptr<Line>& b) {
        return a->ToString() < b->ToString();
      });

  editor_state->set_mode(unique_ptr<AutocompleteMode>(
      new AutocompleteMode(std::move(options))));
  editor_state->ProcessInput('\t');
}

bool StartCompletion(EditorState* editor_state,
                     std::shared_ptr<OpenBuffer> buffer) {
  OpenFileOptions options;
  options.path =
      buffer->read_string_variable(OpenBuffer::variable_dictionary());
  if (options.path.empty()) {
    LOG(INFO) << "Dictionary is not set.";
    return false;
  }
  options.editor_state = editor_state;
  options.make_current_buffer = false;
  auto file = OpenFile(options);
  file->second->set_bool_variable(
      OpenBuffer::variable_show_in_buffers_list(), false);
  LOG(INFO) << "Loaded dictionary.";
  std::weak_ptr<OpenBuffer> weak_dictionary = file->second;
  std::weak_ptr<OpenBuffer> weak_buffer = buffer;
  file->second->AddEndOfFileObserver(
      [editor_state, weak_buffer, weak_dictionary]() {
        FindCompletion(
            editor_state, weak_buffer.lock(), weak_dictionary.lock());
      });
  return true;
}

class InsertMode : public EditorMode {
 public:
  InsertMode(InsertModeOptions options)
      : options_(std::move(options)) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    switch (c) {
      case '\t':
        if (options_.start_completion()) {
          LOG(INFO) << "Completion has started, avoid inserting '\\t'.";
          return;
        }
        break;

      case Terminal::ESCAPE:
        options_.buffer->MaybeAdjustPositionCol();
        options_.buffer->ApplyToCursors(NewDeleteSuffixSuperfluousCharacters());
        options_.buffer->PopTransformationStack();
        for (size_t i = 1; i < editor_state->repetitions(); i++) {
          editor_state->current_buffer()
              ->second->RepeatLastTransformation();
        }
        editor_state->PushCurrentPosition();
        editor_state->ResetStatus();
        options_.escape_handler();
        editor_state->ResetMode();
        editor_state->ResetRepetitions();
        editor_state->ResetInsertionModifier();
        return;

      case Terminal::UP_ARROW:
        options_.scroll_behavior->Up(editor_state, options_.buffer.get());
        return;

      case Terminal::DOWN_ARROW:
        options_.scroll_behavior->Down(editor_state, options_.buffer.get());
        return;

      case Terminal::LEFT_ARROW:
        options_.scroll_behavior->Left(editor_state, options_.buffer.get());
        return;

      case Terminal::RIGHT_ARROW:
        options_.scroll_behavior->Right(editor_state, options_.buffer.get());
        return;

      case Terminal::CTRL_A:
        options_.scroll_behavior->Begin(editor_state, options_.buffer.get());
        return;

      case Terminal::CTRL_E:
        options_.scroll_behavior->End(editor_state, options_.buffer.get());
        return;

      case Terminal::BACKSPACE:
        {
          LOG(INFO) << "Handling backspace in insert mode.";
          options_.buffer->MaybeAdjustPositionCol();
          Modifiers modifiers;
          modifiers.direction = BACKWARDS;
          options_.buffer->ApplyToCursors(
              NewDeleteCharactersTransformation(modifiers, false));
          options_.buffer->set_modified(true);
          options_.modify_listener();
          editor_state->ScheduleRedraw();
        }
        return;

      case '\n':
        options_.new_line_handler();
        return;

      case Terminal::CTRL_U:
        {
          Modifiers modifiers;
          modifiers.structure = LINE;
          modifiers.structure_range =
              Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION;
          options_.buffer->ApplyToCursors(NewDeleteTransformation(
              modifiers, false));
          options_.modify_listener();
          editor_state->ScheduleRedraw();
          return;
        }

      case Terminal::CTRL_K:
        {
          Modifiers modifiers;
          modifiers.structure = LINE;
          modifiers.structure_range =
              Modifiers::FROM_CURRENT_POSITION_TO_END;
          options_.buffer->ApplyToCursors(NewDeleteTransformation(
              modifiers, false));
          options_.modify_listener();
          editor_state->ScheduleRedraw();
          return;
        }
    }

    {
      shared_ptr<OpenBuffer> insert(
          new OpenBuffer(editor_state, L"- text inserted"));
      insert->AppendToLastLine(editor_state,
          NewCopyString(options_.buffer->TransformKeyboardText(wstring(1, c))));
      options_.buffer->ApplyToCursors(
          NewInsertBufferTransformation(insert, 1, END));
    }

    options_.buffer->set_modified(true);
    options_.modify_listener();
    editor_state->ScheduleRedraw();
  }

 private:
  const InsertModeOptions options_;
};

class RawInputTypeMode : public EditorMode {
 public:
  RawInputTypeMode(shared_ptr<OpenBuffer> buffer)
      : buffer_(buffer),
        buffering_(false) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    bool old_literal = literal_;
    literal_ = false;
    switch (c) {
      case Terminal::CHAR_EOF:
        line_buffer_.push_back(4);
        write(buffer_->fd(), line_buffer_.c_str(), line_buffer_.size());
        line_buffer_ = "";
        break;

      case Terminal::CTRL_L:
        {
          string sequence(1, 0x0c);
          write(buffer_->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::CTRL_V:
        if (old_literal) {
          DLOG(INFO) << "Inserting literal CTRL_V";
          string sequence(1, 22);
          write(buffer_->fd(), sequence.c_str(), sequence.size());
        } else {
          DLOG(INFO) << "Set literal.";
          literal_ = true;
        }
        break;

      case Terminal::CTRL_U:
        if (!buffer_) {
          line_buffer_ = "";
        } else {
          string sequence(1, 21);
          write(buffer_->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::ESCAPE:
        if (old_literal) {
          DLOG(INFO) << "Inserting literal ESCAPE";
          string sequence(1, 27);
          write(buffer_->fd(), sequence.c_str(), sequence.size());
        } else {
          editor_state->ResetMode();
          editor_state->ResetStatus();
        }
        break;

      case Terminal::UP_ARROW:
        {
          string sequence = { 27, '[', 'A' };
          write(buffer_->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::DOWN_ARROW:
        {
          string sequence = { 27, '[', 'B' };
          write(buffer_->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::RIGHT_ARROW:
        {
          string sequence = { 27, '[', 'C' };
          write(buffer_->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::LEFT_ARROW:
        {
          string sequence = { 27, '[', 'D' };
          write(buffer_->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::BACKSPACE:
        if (buffering_) {
          if (line_buffer_.empty()) { return; }
          line_buffer_.resize(line_buffer_.size() - 1);
          auto last_line = *buffer_->contents()->rbegin();
          last_line.reset(
              new Line(last_line->Substring(0, last_line->size() - 1)));
        } else {
          string contents(1, 127);
          write(buffer_->fd(), contents.c_str(), contents.size());
        }
        break;

      case '\n':
        line_buffer_.push_back('\n');
        write(buffer_->fd(), line_buffer_.c_str(), line_buffer_.size());
        line_buffer_ = "";
        break;

      default:
        if (buffering_) {
          buffer_->AppendToLastLine(editor_state, NewCopyString(wstring(1, c)));
          line_buffer_.push_back(c);
          editor_state->ScheduleRedraw();
        } else {
          string contents(1, c);
          write(buffer_->fd(), contents.c_str(), contents.size());
        }
    };
  }

 private:
  // The buffer that we will be insertint into.
  const shared_ptr<OpenBuffer> buffer_;
  string line_buffer_;
  bool buffering_;
  bool literal_ = false;
};

void EnterInsertCharactersMode(InsertModeOptions options) {
  options.buffer->MaybeAdjustPositionCol();
  options.editor_state->SetStatus(L"type");
  options.editor_state->set_mode(
      unique_ptr<EditorMode>(new InsertMode(options)));
}

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

/* static */ shared_ptr<const ScrollBehavior> ScrollBehavior::Default() {
  return DefaultScrollBehavior::Get();
}

void EnterInsertMode(EditorState* editor_state) {
  InsertModeOptions options;
  options.editor_state = editor_state;
  EnterInsertMode(options);
}

void EnterInsertMode(InsertModeOptions options) {
  EditorState* editor_state = options.editor_state;
  CHECK(editor_state != nullptr);

  if (options.buffer == nullptr) {
    if (!editor_state->has_current_buffer()) {
      OpenAnonymousBuffer(editor_state);
    }
    options.buffer = editor_state->current_buffer()->second;
  }

  auto target_buffer = options.buffer->GetBufferFromCurrentLine();
  if (target_buffer != nullptr) {
    options.buffer = target_buffer;
  }

  if (!options.modify_listener) {
    options.modify_listener = []() { /* Nothing. */ };
  }

  if (options.scroll_behavior == nullptr) {
    options.scroll_behavior = DefaultScrollBehavior::Get();
  }

  if (!options.escape_handler) {
    options.escape_handler = []() { /* Nothing. */ };
  }

  if (!options.new_line_handler) {
    auto buffer = options.buffer;
    options.new_line_handler = [buffer, editor_state]() {
      buffer->ApplyToCursors(
          unique_ptr<Transformation>(new NewLineTransformation()));
      buffer->set_modified(true);
      editor_state->ScheduleRedraw();
    };
  }

  if (!options.start_completion) {
    auto buffer = options.buffer;
    options.start_completion = [editor_state, buffer]() {
      LOG(INFO) << "Start default completion.";
      return StartCompletion(editor_state, buffer);
    };
  }

  options.editor_state->ResetStatus();

  if (options.buffer->fd() != -1) {
    editor_state->SetStatus(L"type (raw)");
    editor_state->set_mode(
        unique_ptr<EditorMode>(new RawInputTypeMode(options.buffer)));
  } else if (editor_state->structure() == CHAR) {
    options.buffer->CheckPosition();
    options.buffer->PushTransformationStack();
    EnterInsertCharactersMode(options);
  } else if (editor_state->structure() == LINE) {
    options.buffer->CheckPosition();
    options.buffer->PushTransformationStack();
    options.buffer->ApplyToCursors(
        unique_ptr<Transformation>(
            new InsertEmptyLineTransformation(editor_state->direction())));
    EnterInsertCharactersMode(options);
    editor_state->ScheduleRedraw();
  }
  editor_state->ResetDirection();
  editor_state->ResetStructure();
}

}  // namespace editor
}  // namespace afc

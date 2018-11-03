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
#include "command.h"
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
    buffer->AdjustLineColumn(&result->cursor);
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

    auto continuation_line = std::make_shared<Line>(*line);
    continuation_line->DeleteCharacters(
        prefix_end, continuation_line->size() - prefix_end);

    unique_ptr<TransformationStack> transformation(new TransformationStack);
    {
      shared_ptr<OpenBuffer> buffer_to_insert(
        new OpenBuffer(editor_state, L"- text inserted"));
      buffer_to_insert->AppendRawLine(editor_state, continuation_line);
      transformation->PushBack(
          NewInsertBufferTransformation(buffer_to_insert, 1, END));
    }

    transformation->PushBack(NewGotoPositionTransformation(result->cursor));
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
    buffer->AdjustLineColumn(&result->cursor);
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

    std::shared_ptr<const Line> prefix;

    // TODO: Make these shared_ptr weak_ptrs.
    std::shared_ptr<OpenBuffer> dictionary;
    std::shared_ptr<OpenBuffer> buffer;

    // The position where the matches begin.
    size_t matches_start;
    size_t column_start;
    size_t column_end;
  };

  AutocompleteMode(Options options)
      : options_(std::move(options)),
        matches_current_(options_.matches_start),
        word_length_(options_.column_end - options_.column_start),
        original_text_(
            options_.buffer->LineAt(options_.buffer->position().line)
                ->Substring(options_.column_start, word_length_)) {}

  void DrawCurrentMatch(EditorState* editor_state) {
    wstring status;
    const size_t kPrefixLength = 3;
    size_t start = matches_current_ > options_.matches_start + kPrefixLength
                       ? matches_current_ - kPrefixLength
                       : options_.matches_start;
    for (size_t i = 0; i < 10 && start + i < options_.dictionary->lines_size();
         i++) {
      bool is_current = start + i == matches_current_;
      wstring number_prefix;
      if (start + i > matches_current_) {
        number_prefix = std::to_wstring(start + i - matches_current_) + L":";
      }
      status += wstring(status.empty() ? L"" : L" ")
          + wstring(is_current ? L"[" : L"")
          + number_prefix
          + options_.dictionary->LineAt(start + i)->ToString()
          + wstring(is_current ? L"]" : L"");
    }
    editor_state->SetStatus(status);

    ReplaceCurrentText(editor_state,
        options_.dictionary->LineAt(matches_current_)->contents());
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    switch (c) {
      case '\t':
      case 'j':
      case 'l':
      case Terminal::DOWN_ARROW:
      case Terminal::RIGHT_ARROW:
        matches_current_++;
        if (matches_current_ == options_.dictionary->lines_size()) {
          matches_current_ = options_.matches_start;
        }
        break;

      case 'k':
      case 'h':
      case Terminal::UP_ARROW:
      case Terminal::LEFT_ARROW:
        if (matches_current_ <= options_.matches_start) {
          matches_current_ = options_.dictionary->lines_size() - 1;
        } else {
          matches_current_--;
        }
        break;

      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        matches_current_ += c - '0';
        if (matches_current_ >= options_.dictionary->lines_size()) {
          matches_current_ = options_.matches_start;
        }
        break;

      case Terminal::ESCAPE:
        CHECK(original_text_ != nullptr);
        LOG(INFO) << "Inserting original text: " << original_text_->ToString();
        ReplaceCurrentText(editor_state, original_text_);
        // Pass through.

      default:
        editor_state->current_buffer()->second->set_mode(
            std::move(options_.delegate));
        editor_state->ResetStatus();
        if (c != '\n') {
          editor_state->ProcessInput(c);
        }
        return;
    }

    DrawCurrentMatch(editor_state);
    LOG(INFO) << "Updating variables for next completion.";
  }

 private:
  void ReplaceCurrentText(
      EditorState* editor_state, std::shared_ptr<LazyString> insert) {
    auto buffer_to_insert =
        std::make_shared<OpenBuffer>(editor_state, L"tmp buffer");
    buffer_to_insert->AppendToLastLine(editor_state, insert);
    DLOG(INFO) << "Completion selected: " << buffer_to_insert->ToString()
               << " (len: " << insert->size() << ", word_length: "
               << word_length_ << ").";
    DeleteOptions delete_options;
    delete_options.modifiers.repetitions = word_length_;
    delete_options.copy_to_paste_buffer = false;
    // TODO: Somewhat wrong. Should find the autocompletion for each position.
    // Also, should apply the deletions/insertions at the right positions.
    options_.buffer->ApplyToCursors(
        TransformationAtPosition(
            LineColumn(options_.buffer->position().line, options_.column_start),
            ComposeTransformation(
                NewDeleteCharactersTransformation(delete_options),
                NewInsertBufferTransformation(buffer_to_insert, 1, END))));

    editor_state->ScheduleRedraw();
    word_length_ = insert->size();
  }

  Options options_;
  // The position of the line with the current match.
  size_t matches_current_;

  // The number of characters that need to be erased (starting at
  // options_.column_start) for the next insertion. Initially, this is computed
  // from options_.column_start and options_.column_end; however, after an
  // insertion, it gets updated with the length of the insertion.
  size_t word_length_;

  // The original text (that the autocompletion is replacing).
  const std::shared_ptr<LazyString> original_text_;
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

  options.column_end = buffer->position().column;
  if (options.column_end == 0) {
    LOG(INFO) << "No completion at very beginning of line.";
    return;
  }

  auto line = buffer->current_line()->ToString();
  options.column_start = line.find_last_not_of(
      buffer->read_string_variable(OpenBuffer::variable_word_characters()),
      options.column_end - 1);
  if (options.column_start == wstring::npos) {
    options.column_start = 0;
  } else {
    options.column_start++;
  }
  LOG(INFO) << "Positions: start: " << options.column_start << ", end: "
            << options.column_end;
  options.prefix = std::make_shared<const Line>(Line::Options(NewCopyString(
      line.substr(options.column_start,
                  options.column_end - options.column_start))));

  options.delegate = buffer->ResetMode();
  options.dictionary = dictionary;
  options.buffer = buffer;

  LOG(INFO) << "Find completion for \"" << options.prefix->ToString()
            << "\" among options: " << dictionary->contents()->size();
  options.matches_start = dictionary->contents()->upper_bound(
      options.prefix,
      [](const shared_ptr<const Line>& a, const shared_ptr<const Line>& b) {
        return a->ToString() < b->ToString();
      });

  if (options.matches_start == dictionary->lines_size()) {
    options.matches_start = 0;
  }

  unique_ptr<AutocompleteMode> autocomplete_mode(
      new AutocompleteMode(std::move(options)));
  autocomplete_mode->DrawCurrentMatch(editor_state);
  editor_state->current_buffer()->second->set_mode(
      std::move(autocomplete_mode));
}

void StartCompletionFromDictionary(
    EditorState* editor_state, std::shared_ptr<OpenBuffer> buffer,
    wstring path) {
  OpenFileOptions options;
  options.path = path;
  DCHECK(!options.path.empty());
  options.editor_state = editor_state;
  options.make_current_buffer = false;
  auto file = OpenFile(options);
  file->second->set_bool_variable(
      OpenBuffer::variable_show_in_buffers_list(), false);
  LOG(INFO) << "Loading dictionary.";
  std::weak_ptr<OpenBuffer> weak_dictionary = file->second;
  std::weak_ptr<OpenBuffer> weak_buffer = buffer;
  file->second->AddEndOfFileObserver(
      [editor_state, weak_buffer, weak_dictionary]() {
        FindCompletion(
            editor_state, weak_buffer.lock(), weak_dictionary.lock());
      });
}

void RegisterLeaves(const OpenBuffer& buffer, const ParseTree& tree,
                   std::set<wstring>* words) {
  DCHECK(words != nullptr);
  if (tree.children.empty() && tree.range.begin.line == tree.range.end.line) {
    CHECK_LE(tree.range.begin.column, tree.range.end.column);
    auto line = buffer.LineAt(tree.range.begin.line);
    CHECK_LE(tree.range.end.column, line->size());
    auto word = line->Substring(
        tree.range.begin.column, tree.range.end.column
            - tree.range.begin.column)->ToString();
    DVLOG(5) << "Found leave: " << word;
    words->insert(word);
  }
  for (auto& child : tree.children) {
    RegisterLeaves(buffer, child, words);
  }
}

bool StartCompletion(EditorState* editor_state,
                     std::shared_ptr<OpenBuffer> buffer) {
  auto path = buffer->read_string_variable(OpenBuffer::variable_dictionary());
  if (!path.empty()) {
    StartCompletionFromDictionary(editor_state, buffer, path);
    return true;
  }

  std::set<wstring> words;
  auto root = buffer->parse_tree();
  RegisterLeaves(*buffer, *buffer->current_tree(root.get()), &words);
  LOG(INFO) << "Leaves found: " << words.size();

  std::wistringstream keywords(
      buffer->read_string_variable(OpenBuffer::variable_language_keywords()));
  words.insert(std::istream_iterator<wstring, wchar_t>(keywords),
               std::istream_iterator<wstring, wchar_t>());

  if (words.empty()) {
    return false;
  }

  auto dictionary = std::make_shared<OpenBuffer>(editor_state, L"Dictionary");
  for (auto& word : words) {
    dictionary->AppendLine(editor_state, NewCopyString(word));
  }

  FindCompletion(editor_state, buffer, dictionary);
  return true;
}

class FindCompletionCommand : public Command {
 public:
  const wstring Description() {
    return L"Autocompletes the current word.";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    StartCompletion(editor_state, buffer);
  }
};

class InsertMode : public EditorMode {
 public:
  InsertMode(InsertModeOptions options)
      : options_(std::move(options)) {
    CHECK(options_.escape_handler);
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    auto buffer = options_.buffer;
    CHECK(buffer != nullptr);
    switch (c) {
      case '\t':
        if (options_.start_completion()) {
          LOG(INFO) << "Completion has started, avoid inserting '\\t'.";
          return;
        }
        break;

      case Terminal::ESCAPE:
        buffer->MaybeAdjustPositionCol();
        buffer->ApplyToCursors(NewDeleteSuffixSuperfluousCharacters());
        buffer->PopTransformationStack();
        editor_state->set_repetitions(editor_state->repetitions() - 1);
        buffer->RepeatLastTransformation();
        buffer->PopTransformationStack();
        editor_state->PushCurrentPosition();
        editor_state->ResetStatus();
        CHECK(options_.escape_handler);
        options_.escape_handler();  // Probably deletes us.
        editor_state->ResetRepetitions();
        editor_state->ResetInsertionModifier();
        buffer->ResetMode();
        return;

      case Terminal::UP_ARROW:
        options_.scroll_behavior->Up(editor_state, buffer.get());
        return;

      case Terminal::DOWN_ARROW:
        options_.scroll_behavior->Down(editor_state, buffer.get());
        return;

      case Terminal::LEFT_ARROW:
        options_.scroll_behavior->Left(editor_state, buffer.get());
        return;

      case Terminal::RIGHT_ARROW:
        options_.scroll_behavior->Right(editor_state, buffer.get());
        return;

      case Terminal::CTRL_A:
        options_.scroll_behavior->Begin(editor_state, buffer.get());
        return;

      case Terminal::CTRL_E:
        options_.scroll_behavior->End(editor_state, buffer.get());
        return;

      case Terminal::CHAR_EOF:  // Ctrl_D
      case Terminal::DELETE:
      case Terminal::BACKSPACE:
        {
          LOG(INFO) << "Handling backspace in insert mode.";
          buffer->MaybeAdjustPositionCol();
          DeleteOptions delete_options;
          if (c == Terminal::BACKSPACE) {
            delete_options.modifiers.direction = BACKWARDS;
          }
          delete_options.copy_to_paste_buffer = false;
          buffer->ApplyToCursors(
              NewDeleteCharactersTransformation(delete_options));
          options_.modify_listener();
          editor_state->ScheduleRedraw();
        }
        return;

      case '\n':
        options_.new_line_handler();
        return;

      case Terminal::CTRL_U:
        {
          DeleteOptions delete_options;
          delete_options.modifiers.structure_range =
              Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION;
          delete_options.copy_to_paste_buffer = false;
          buffer->ApplyToCursors(
              NewDeleteLinesTransformation(delete_options));
          options_.modify_listener();
          editor_state->ScheduleRedraw();
          return;
        }

      case Terminal::CTRL_K:
        {
          DeleteOptions delete_options;
          delete_options.modifiers.structure_range =
              Modifiers::FROM_CURRENT_POSITION_TO_END;
          delete_options.copy_to_paste_buffer = false;
          buffer->ApplyToCursors(
              NewDeleteLinesTransformation(delete_options));
          options_.modify_listener();
          editor_state->ScheduleRedraw();
          return;
        }
    }

    {
      shared_ptr<OpenBuffer> insert(
          new OpenBuffer(editor_state, L"- text inserted"));
      insert->AppendToLastLine(editor_state,
          NewCopyString(buffer->TransformKeyboardText(wstring(1, c))));
      buffer->ApplyToCursors(
          NewInsertBufferTransformation(insert, 1, END));
    }

    options_.modify_listener();
    editor_state->ScheduleRedraw();
  }

 private:
  const InsertModeOptions options_;
};

class RawInputTypeMode : public EditorMode {
 public:
  RawInputTypeMode(shared_ptr<OpenBuffer> buffer)
      : buffer_(buffer) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    bool old_literal = literal_;
    literal_ = false;
    switch (c) {
      case Terminal::CHAR_EOF:
        line_buffer_.push_back(4);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_A:
        line_buffer_.push_back(1);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_E:
        line_buffer_.push_back(0x05);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_K:
        line_buffer_.push_back(0x0b);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_L:
        line_buffer_.push_back(0x0c);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_V:
        if (old_literal) {
          DLOG(INFO) << "Inserting literal CTRL_V";
          line_buffer_.push_back(22);
          WriteLineBuffer(editor_state);
        } else {
          DLOG(INFO) << "Set literal.";
          editor_state->SetStatus(L"<literal>");
          literal_ = true;
        }
        break;

      case Terminal::CTRL_U:
        if (!buffer_) {
          line_buffer_ = "";
        } else {
          line_buffer_.push_back(21);
          WriteLineBuffer(editor_state);
        }
        break;

      case Terminal::ESCAPE:
        if (old_literal) {
          editor_state->SetStatus(L"ESC");
          line_buffer_.push_back(27);
          WriteLineBuffer(editor_state);
        } else {
          buffer_->ResetMode();
          editor_state->ResetStatus();
        }
        break;

      case Terminal::UP_ARROW:
        line_buffer_.push_back(27);
        line_buffer_.push_back('[');
        line_buffer_.push_back('A');
        WriteLineBuffer(editor_state);
        break;

      case Terminal::DOWN_ARROW:
        line_buffer_.push_back(27);
        line_buffer_.push_back('[');
        line_buffer_.push_back('B');
        WriteLineBuffer(editor_state);
        break;

      case Terminal::RIGHT_ARROW:
        line_buffer_.push_back(27);
        line_buffer_.push_back('[');
        line_buffer_.push_back('C');
        WriteLineBuffer(editor_state);
        break;

      case Terminal::LEFT_ARROW:
        line_buffer_.push_back(27);
        line_buffer_.push_back('[');
        line_buffer_.push_back('D');
        WriteLineBuffer(editor_state);
        break;

      case Terminal::DELETE:
        line_buffer_.push_back(27);
        line_buffer_.push_back('[');
        line_buffer_.push_back(51);
        line_buffer_.push_back(126);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::BACKSPACE:
        {
          string contents(1, 127);
          write(buffer_->fd(), contents.c_str(), contents.size());
        }
        break;

      case '\n':
        line_buffer_.push_back('\n');
        WriteLineBuffer(editor_state);
        break;

      default:
        line_buffer_.push_back(c);
        WriteLineBuffer(editor_state);
    };
  }

 private:
  void WriteLineBuffer(EditorState* editor_state) {
    if (buffer_->fd() == -1) {
      editor_state->SetStatus(L"Warning: Process exited.");
    } else if (write(buffer_->fd(), line_buffer_.c_str(), line_buffer_.size())
                   == -1) {
      editor_state->SetStatus(
          L"Write failed: " + FromByteString(strerror(errno)));
    } else {
      editor_state->StartHandlingInterrupts();
    }
    line_buffer_ = "";
  }

  // The buffer that we will be insertint into.
  const shared_ptr<OpenBuffer> buffer_;
  string line_buffer_;
  bool literal_ = false;
};

void EnterInsertCharactersMode(InsertModeOptions options) {
  options.buffer->MaybeAdjustPositionCol();
  options.editor_state->SetStatus(L"type");
  options.editor_state->current_buffer()->second->set_mode(
      unique_ptr<EditorMode>(new InsertMode(options)));
  if (options.buffer->active_cursors()->size() > 1 &&
      options.buffer->read_bool_variable(
          OpenBuffer::variable_multiple_cursors())) {
    BeepFrequencies(options.editor_state->audio_player(), {659.25, 1046.50});
  }
}

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

std::unique_ptr<Command> NewFindCompletionCommand() {
  return std::unique_ptr<Command>(new FindCompletionCommand());
}

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
    editor_state->current_buffer()->second->set_mode(
        unique_ptr<EditorMode>(new RawInputTypeMode(options.buffer)));
  } else if (editor_state->structure() == CHAR) {
    options.buffer->CheckPosition();
    options.buffer->PushTransformationStack();
    options.buffer->PushTransformationStack();
    EnterInsertCharactersMode(options);
  } else if (editor_state->structure() == LINE) {
    options.buffer->CheckPosition();
    options.buffer->PushTransformationStack();
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

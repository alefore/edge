#include "src/insert_mode.h"

#include <algorithm>
#include <memory>
#include <vector>

extern "C" {
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/lazy_string_append.h"
#include "src/parse_tree.h"
#include "src/substring.h"
#include "src/terminal.h"
#include "src/transformation.h"
#include "src/transformation/goto_position.h"
#include "src/transformation_delete.h"
#include "src/transformation_move.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/wstring.h"

namespace afc::editor {
namespace {
class NewLineTransformation : public Transformation {
  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    result->buffer->AdjustLineColumn(&result->cursor);
    const ColumnNumber column = result->cursor.column;
    auto line = result->buffer->LineAt(result->cursor.line);
    if (line == nullptr) {
      result->made_progress = false;
      return;
    }

    if (result->buffer->Read(buffer_variables::atomic_lines) &&
        column != ColumnNumber(0) && column != line->EndColumn()) {
      result->made_progress = false;
      return;
    }

    const wstring& line_prefix_characters(
        result->buffer->Read(buffer_variables::line_prefix_characters));
    ColumnNumber prefix_end = ColumnNumber(0);
    if (line != nullptr &&
        !result->buffer->Read(buffer_variables::paste_mode)) {
      while (prefix_end < column &&
             (line_prefix_characters.find(line->get(prefix_end)) !=
              line_prefix_characters.npos)) {
        ++prefix_end;
      }
    }

    auto transformation = std::make_unique<TransformationStack>();
    {
      auto buffer_to_insert = std::make_shared<OpenBuffer>(
          result->buffer->editor(), L"- text inserted");
      buffer_to_insert->AppendRawLine(std::make_shared<Line>(
          Line::Options(*line).DeleteSuffix(prefix_end)));
      InsertOptions insert_options;
      insert_options.buffer_to_insert = buffer_to_insert;
      transformation->PushBack(
          NewInsertBufferTransformation(std::move(insert_options)));
    }

    transformation->PushBack(NewGotoPositionTransformation(result->cursor));
    transformation->PushBack(NewDeleteSuffixSuperfluousCharacters());

    transformation->PushBack(NewGotoPositionTransformation(
        LineColumn(result->cursor.line + LineNumberDelta(1), prefix_end)));
    return transformation->Apply(result);
  }

  unique_ptr<Transformation> Clone() const override {
    return std::make_unique<NewLineTransformation>();
  }
};

class InsertEmptyLineTransformation : public Transformation {
 public:
  InsertEmptyLineTransformation(Direction direction) : direction_(direction) {}

  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    if (direction_ == BACKWARDS) {
      result->cursor.line++;
    }
    result->cursor.column = ColumnNumber(0);
    result->buffer->AdjustLineColumn(&result->cursor);
    return ComposeTransformation(std::make_unique<NewLineTransformation>(),
                                 NewGotoPositionTransformation(result->cursor))
        ->Apply(result);
  }

  std::unique_ptr<Transformation> Clone() const override {
    return std::make_unique<InsertEmptyLineTransformation>(direction_);
  }

 private:
  Direction direction_;
};

class AutocompleteMode : public EditorMode {
 public:
  struct Options {
    std::shared_ptr<EditorMode> delegate;

    std::shared_ptr<const Line> prefix;

    // TODO: Make these shared_ptr weak_ptrs.
    std::shared_ptr<OpenBuffer> dictionary;
    std::shared_ptr<OpenBuffer> buffer;

    // The position where the matches begin.
    LineNumber line_start;
    ColumnNumber column_start;
    ColumnNumber column_end;
  };

  AutocompleteMode(Options options)
      : options_(std::move(options)),
        line_current_(options_.line_start),
        word_length_(options_.column_end - options_.column_start),
        original_text_(options_.buffer->LineAt(options_.buffer->position().line)
                           ->Substring(options_.column_start, word_length_)) {}

  void DrawCurrentMatch(EditorState* editor_state) {
    wstring status;
    const LineNumberDelta kPrefixLength = LineNumberDelta(3);
    LineNumber start = line_current_ > options_.line_start + kPrefixLength
                           ? line_current_ - kPrefixLength
                           : options_.line_start;
    for (size_t i = 0;
         i < 10 && start + LineNumberDelta(i) < options_.dictionary->EndLine();
         i++) {
      LineNumber current = start + LineNumberDelta(i);
      bool is_current = current == line_current_;
      wstring number_prefix;
      if (current > line_current_) {
        number_prefix =
            std::to_wstring((current - line_current_).line_delta) + L":";
      }
      status += wstring(status.empty() ? L"" : L" ") +
                wstring(is_current ? L"[" : L"") + number_prefix +
                options_.dictionary->LineAt(current)->ToString() +
                wstring(is_current ? L"]" : L"");
    }
    options_.buffer->status()->SetInformationText(status);

    ReplaceCurrentText(editor_state,
                       options_.dictionary->LineAt(line_current_)->contents());
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    switch (static_cast<int>(c)) {
      case '\t':
      case 'j':
      case 'l':
      case Terminal::DOWN_ARROW:
      case Terminal::RIGHT_ARROW:
        line_current_++;
        if (line_current_ > options_.dictionary->EndLine()) {
          line_current_ = options_.line_start;
        }
        break;

      case 'k':
      case 'h':
      case Terminal::UP_ARROW:
      case Terminal::LEFT_ARROW:
        if (line_current_ <= options_.line_start) {
          line_current_ = options_.dictionary->EndLine();
        } else {
          --line_current_;
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
        line_current_ += LineNumberDelta(c - '0');
        if (line_current_ > options_.dictionary->EndLine()) {
          line_current_ = options_.line_start;
        }
        break;

      case Terminal::ESCAPE:
        CHECK(original_text_ != nullptr);
        LOG(INFO) << "Inserting original text: " << original_text_->ToString();
        ReplaceCurrentText(editor_state, original_text_);
        // Fall through.
      default:
        editor_state->current_buffer()->set_mode(std::move(options_.delegate));
        editor_state->current_buffer()->status()->Reset();
        editor_state->status()->Reset();
        if (c != '\n') {
          editor_state->ProcessInput(c);
        }
        return;
    }

    DrawCurrentMatch(editor_state);
    LOG(INFO) << "Updating variables for next completion.";
  }

 private:
  void ReplaceCurrentText(EditorState* editor_state,
                          std::shared_ptr<LazyString> insert) {
    auto buffer_to_insert =
        std::make_shared<OpenBuffer>(editor_state, L"tmp buffer");
    buffer_to_insert->AppendToLastLine(insert);
    DLOG(INFO) << "Completion selected: " << buffer_to_insert->ToString()
               << " (len: " << insert->size()
               << ", word_length: " << word_length_ << ").";
    DeleteOptions delete_options;
    delete_options.modifiers.repetitions = word_length_.column_delta;
    delete_options.copy_to_paste_buffer = false;
    // TODO: Somewhat wrong. Should find the autocompletion for each position.
    // Also, should apply the deletions/insertions at the right positions.
    InsertOptions insert_options;
    insert_options.buffer_to_insert = buffer_to_insert;
    options_.buffer->ApplyToCursors(TransformationAtPosition(
        LineColumn(options_.buffer->position().line, options_.column_start),
        ComposeTransformation(
            NewDeleteTransformation(delete_options),
            NewInsertBufferTransformation(std::move(insert_options)))));

    word_length_ = ColumnNumberDelta(insert->size());
  }

  Options options_;
  // The position of the line with the current match.
  LineNumber line_current_;

  // The number of characters that need to be erased (starting at
  // options_.column_start) for the next insertion. Initially, this is computed
  // from options_.column_start and options_.column_end; however, after an
  // insertion, it gets updated with the length of the insertion.
  ColumnNumberDelta word_length_;

  // The original text (that the autocompletion is replacing).
  const std::shared_ptr<LazyString> original_text_;
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
  if (options.column_end.IsZero()) {
    LOG(INFO) << "No completion at very beginning of line.";
    return;
  }

  if (dictionary->contents()->size() <= LineNumberDelta(1)) {
    static std::vector<wstring> errors(
        {L"No completions are available.",
         L"The autocomplete dictionary is empty.",
         L"Maybe set the `dictionary` variable?",
         L"Sorry, I can't autocomplete with an empty dictionary."});
    buffer->status()->SetInformationText(errors[rand() % errors.size()]);
    return;
  }

  LOG(INFO) << "Dictionary size: " << dictionary->contents()->size();

  auto line = buffer->current_line();
  auto line_str = line->ToString();
  size_t index_before_symbol = line_str.find_last_not_of(
      buffer->Read(buffer_variables::symbol_characters),
      (options.column_end - ColumnNumberDelta(1)).column);
  if (index_before_symbol == wstring::npos) {
    options.column_start = ColumnNumber(0);
  } else {
    options.column_start = ColumnNumber(index_before_symbol + 1);
  }
  LOG(INFO) << "Positions: start: " << options.column_start
            << ", end: " << options.column_end;
  options.prefix = std::make_shared<const Line>(
      Line::Options(Substring(line->contents(), options.column_start,
                              options.column_end - options.column_start)));

  options.delegate = buffer->ResetMode();
  options.dictionary = dictionary;
  options.buffer = buffer;

  LOG(INFO) << "Find completion for \"" << options.prefix->ToString()
            << "\" among options: " << dictionary->contents()->size();
  options.line_start = dictionary->contents()->upper_bound(
      options.prefix,
      [](const shared_ptr<const Line>& a, const shared_ptr<const Line>& b) {
        return a->ToString() < b->ToString();
      });

  if (options.line_start == LineNumber(0) + dictionary->lines_size()) {
    options.line_start = LineNumber(0);
  }

  auto autocomplete_mode =
      std::make_unique<AutocompleteMode>(std::move(options));
  autocomplete_mode->DrawCurrentMatch(editor_state);
  editor_state->current_buffer()->set_mode(std::move(autocomplete_mode));
}

void StartCompletionFromDictionary(EditorState* editor_state,
                                   std::shared_ptr<OpenBuffer> buffer,
                                   wstring path) {
  OpenFileOptions options;
  options.path = path;
  DCHECK(!options.path.empty());
  options.editor_state = editor_state;
  options.insertion_type = BuffersList::AddBufferType::kIgnore;
  auto file = OpenFile(options);
  file->second->Set(buffer_variables::show_in_buffers_list, false);
  LOG(INFO) << "Loading dictionary.";
  std::weak_ptr<OpenBuffer> weak_dictionary = file->second;
  std::weak_ptr<OpenBuffer> weak_buffer = buffer;
  file->second->AddEndOfFileObserver([editor_state, weak_buffer,
                                      weak_dictionary]() {
    FindCompletion(editor_state, weak_buffer.lock(), weak_dictionary.lock());
  });
}

void RegisterLeaves(const OpenBuffer& buffer, const ParseTree& tree,
                    std::set<wstring>* words) {
  DCHECK(words != nullptr);
  if (tree.children().empty() &&
      tree.range().begin.line == tree.range().end.line) {
    CHECK_LE(tree.range().begin.column, tree.range().end.column);
    auto line = buffer.LineAt(tree.range().begin.line);
    CHECK_LE(tree.range().end.column, line->EndColumn());
    auto word =
        line->Substring(tree.range().begin.column,
                        tree.range().end.column - tree.range().begin.column)
            ->ToString();
    if (!word.empty()) {
      DVLOG(5) << "Found leave: " << word;
      words->insert(word);
    }
  }
  for (auto& child : tree.children()) {
    RegisterLeaves(buffer, child, words);
  }
}

bool StartCompletion(EditorState* editor_state,
                     std::shared_ptr<OpenBuffer> buffer) {
  auto path = buffer->Read(buffer_variables::dictionary);
  if (!path.empty()) {
    StartCompletionFromDictionary(editor_state, buffer, path);
    return true;
  }

  std::set<wstring> words;
  auto root = buffer->parse_tree();
  RegisterLeaves(*buffer, *buffer->current_tree(root.get()), &words);
  LOG(INFO) << "Leaves found: " << words.size();

  std::wistringstream keywords(
      buffer->Read(buffer_variables::language_keywords));
  words.insert(std::istream_iterator<wstring, wchar_t>(keywords),
               std::istream_iterator<wstring, wchar_t>());

  auto dictionary = std::make_shared<OpenBuffer>(editor_state, L"Dictionary");
  for (auto& word : words) {
    dictionary->AppendLine(NewLazyString(std::move(word)));
  }

  FindCompletion(editor_state, buffer, dictionary);
  return true;
}

class FindCompletionCommand : public Command {
 public:
  wstring Description() const override {
    return L"Autocompletes the current word.";
  }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    StartCompletion(editor_state, buffer);
  }
};

class InsertMode : public EditorMode {
 public:
  InsertMode(InsertModeOptions options) : options_(std::move(options)) {
    CHECK(options_.escape_handler);
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    auto buffer = options_.buffer;
    CHECK(buffer != nullptr);
    switch (static_cast<int>(c)) {
      case '\t':
        ResetScrollBehavior();
        if (options_.start_completion()) {
          LOG(INFO) << "Completion has started, avoid inserting '\\t'.";
          return;
        }
        break;

      case Terminal::ESCAPE:
        ResetScrollBehavior();
        buffer->MaybeAdjustPositionCol();
        buffer->ApplyToCursors(NewDeleteSuffixSuperfluousCharacters());
        buffer->PopTransformationStack();
        editor_state->set_repetitions(editor_state->repetitions() - 1);
        buffer->RepeatLastTransformation();
        buffer->PopTransformationStack();
        editor_state->PushCurrentPosition();
        buffer->status()->Reset();
        editor_state->status()->Reset();
        CHECK(options_.escape_handler);
        options_.escape_handler();  // Probably deletes us.
        editor_state->ResetRepetitions();
        editor_state->ResetInsertionModifier();
        editor_state->current_buffer()->ResetMode();
        editor_state->set_keyboard_redirect(nullptr);
        return;

      case Terminal::UP_ARROW:
        GetScrollBehavior()->Up(editor_state, buffer.get());
        return;

      case Terminal::DOWN_ARROW:
        GetScrollBehavior()->Down(editor_state, buffer.get());
        return;

      case Terminal::LEFT_ARROW:
        GetScrollBehavior()->Left(editor_state, buffer.get());
        return;

      case Terminal::RIGHT_ARROW:
        GetScrollBehavior()->Right(editor_state, buffer.get());
        return;

      case Terminal::CTRL_A:
        GetScrollBehavior()->Begin(editor_state, buffer.get());
        return;

      case Terminal::CTRL_E:
        GetScrollBehavior()->End(editor_state, buffer.get());
        return;

      case Terminal::CTRL_D:
      case Terminal::DELETE:
      case Terminal::BACKSPACE: {
        ResetScrollBehavior();
        LOG(INFO) << "Handling backspace in insert mode.";
        buffer->MaybeAdjustPositionCol();
        DeleteOptions delete_options;
        if (c == wint_t(Terminal::BACKSPACE)) {
          delete_options.modifiers.direction = BACKWARDS;
        }
        delete_options.copy_to_paste_buffer = false;
        buffer->ApplyToCursors(NewDeleteTransformation(delete_options));
        options_.modify_handler();
      }
        return;

      case '\n':
        ResetScrollBehavior();
        options_.new_line_handler();
        return;

      case Terminal::CTRL_U: {
        ResetScrollBehavior();
        // TODO: Find a way to set `copy_to_paste_buffer` in the transformation.
        Value* callback = buffer->environment()->Lookup(
            L"HandleKeyboardControlU", VMType::Function({VMType::Void()}));
        if (callback == nullptr) {
          LOG(INFO) << "Didn't find HandleKeyboardControlU function: "
                    << buffer->Read(buffer_variables::name);
          return;
        }
        std::shared_ptr<Expression> expression = vm::NewFunctionCall(
            vm::NewConstantExpression(std::make_unique<vm::Value>(*callback)),
            {});
        if (expression->Types().empty()) {
          buffer->status()->SetWarningText(
              L"Unable to compile (type mismatch).");
          return;
        }
        buffer->EvaluateExpression(
            expression.get(),
            [buffer, expression,
             callback = options_.modify_handler](Value::Ptr) { callback(); });
        return;
      }

      case Terminal::CTRL_K: {
        ResetScrollBehavior();
        DeleteOptions delete_options;
        delete_options.modifiers.structure = StructureLine();
        delete_options.modifiers.boundary_begin = Modifiers::CURRENT_POSITION;
        delete_options.modifiers.boundary_end = Modifiers::LIMIT_CURRENT;
        delete_options.copy_to_paste_buffer = false;
        buffer->ApplyToCursors(NewDeleteTransformation(delete_options));
        options_.modify_handler();
        return;
      }

      default:
        ResetScrollBehavior();
    }

    {
      auto buffer_to_insert =
          std::make_shared<OpenBuffer>(editor_state, L"- text inserted");
      buffer_to_insert->AppendToLastLine(
          NewLazyString(buffer->TransformKeyboardText(wstring(1, c))));

      InsertOptions insert_options;
      insert_options.modifiers.insertion = editor_state->modifiers().insertion;
      insert_options.buffer_to_insert = buffer_to_insert;
      buffer->ApplyToCursors(
          NewInsertBufferTransformation(std::move(insert_options)));
    }

    options_.modify_handler();
  }

 private:
  ScrollBehavior* GetScrollBehavior() {
    if (scroll_behavior_ == nullptr) {
      CHECK(options_.scroll_behavior != nullptr);
      scroll_behavior_ = options_.scroll_behavior->Build();
    }
    return scroll_behavior_.get();
  }

  void ResetScrollBehavior() { scroll_behavior_ = nullptr; }

  const InsertModeOptions options_;
  std::unique_ptr<ScrollBehavior> scroll_behavior_;
};

class RawInputTypeMode : public EditorMode {
 public:
  RawInputTypeMode(shared_ptr<OpenBuffer> buffer) : buffer_(buffer) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    bool old_literal = literal_;
    literal_ = false;
    switch (static_cast<int>(c)) {
      case Terminal::CTRL_A:
        line_buffer_.push_back(1);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_D:
        line_buffer_.push_back(4);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_E:
        line_buffer_.push_back(5);
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
          buffer_->status()->SetInformationText(L"<literal>");
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
          buffer_->status()->SetInformationText(L"ESC");
          line_buffer_.push_back(27);
          WriteLineBuffer(editor_state);
        } else {
          editor_state->current_buffer()->ResetMode();
          editor_state->set_keyboard_redirect(nullptr);
          buffer_->status()->Reset();
          editor_state->status()->Reset();
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

      case Terminal::BACKSPACE: {
        string contents(1, 127);
        if (buffer_->fd() != nullptr) {
          write(buffer_->fd()->fd(), contents.c_str(), contents.size());
        }
      } break;

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
    if (buffer_->fd() == nullptr) {
      buffer_->status()->SetWarningText(L"Warning: Process exited.");
    } else if (write(buffer_->fd()->fd(), line_buffer_.c_str(),
                     line_buffer_.size()) == -1) {
      buffer_->status()->SetWarningText(L"Write failed: " +
                                        FromByteString(strerror(errno)));
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
  if (options.buffer->Read(buffer_variables::extend_lines)) {
    options.buffer->MaybeExtendLine(options.buffer->position());
  } else {
    options.buffer->MaybeAdjustPositionCol();
  }
  options.buffer->status()->SetInformationText(L"🔡");

  auto handler = std::make_unique<InsertMode>(options);
  if (options.editor_state->current_buffer() == options.buffer) {
    options.editor_state->current_buffer()->set_mode(std::move(handler));
  } else {
    options.editor_state->set_keyboard_redirect(std::move(handler));
  }

  if (options.buffer->active_cursors()->size() > 1 &&
      options.buffer->Read(buffer_variables::multiple_cursors)) {
    BeepFrequencies(options.editor_state->audio_player(), {659.25, 1046.50});
  }
}
}  // namespace
using std::shared_ptr;
using std::unique_ptr;

void DefaultScrollBehavior::Up(EditorState*, OpenBuffer* buffer) {
  Modifiers modifiers;
  modifiers.direction = BACKWARDS;
  modifiers.structure = StructureLine();
  buffer->ApplyToCursors(NewMoveTransformation(modifiers));
}

void DefaultScrollBehavior::Down(EditorState*, OpenBuffer* buffer) {
  Modifiers modifiers;
  modifiers.structure = StructureLine();
  buffer->ApplyToCursors(NewMoveTransformation(modifiers));
}

void DefaultScrollBehavior::Left(EditorState*, OpenBuffer* buffer) {
  Modifiers modifiers;
  modifiers.direction = BACKWARDS;
  buffer->ApplyToCursors(NewMoveTransformation(modifiers));
}

void DefaultScrollBehavior::Right(EditorState*, OpenBuffer* buffer) {
  buffer->ApplyToCursors(NewMoveTransformation(Modifiers()));
}

void DefaultScrollBehavior::Begin(EditorState*, OpenBuffer* buffer) {
  buffer->ApplyToCursors(
      NewGotoPositionTransformation(std::nullopt, ColumnNumber(0)));
}

void DefaultScrollBehavior::End(EditorState*, OpenBuffer* buffer) {
  buffer->ApplyToCursors(NewGotoPositionTransformation(
      std::nullopt, std::numeric_limits<ColumnNumber>::max()));
}

std::unique_ptr<Command> NewFindCompletionCommand() {
  return std::make_unique<FindCompletionCommand>();
}

/* static */ std::unique_ptr<ScrollBehaviorFactory>
ScrollBehaviorFactory::Default() {
  class DefaultScrollBehaviorFactory : public ScrollBehaviorFactory {
    std::unique_ptr<ScrollBehavior> Build() override {
      return std::make_unique<DefaultScrollBehavior>();
    }
  };

  return std::make_unique<DefaultScrollBehaviorFactory>();
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
    options.buffer = editor_state->current_buffer();
  }

  auto target_buffer = options.buffer->GetBufferFromCurrentLine();
  if (target_buffer != nullptr) {
    options.buffer = target_buffer;
  }

  if (!options.modify_handler) {
    options.modify_handler = []() { /* Nothing. */ };
  }

  if (options.scroll_behavior == nullptr) {
    options.scroll_behavior = ScrollBehaviorFactory::Default();
  }

  if (!options.escape_handler) {
    options.escape_handler = []() { /* Nothing. */ };
  }

  if (!options.new_line_handler) {
    auto buffer = options.buffer;
    options.new_line_handler = [buffer, editor_state]() {
      buffer->ApplyToCursors(std::make_unique<NewLineTransformation>());
    };
  }

  if (!options.start_completion) {
    options.start_completion = [editor_state, buffer = options.buffer]() {
      LOG(INFO) << "Start default completion.";
      return StartCompletion(editor_state, buffer);
    };
  }

  options.editor_state->status()->Reset();
  options.buffer->status()->Reset();

  if (options.buffer->fd() != nullptr) {
    options.buffer->status()->SetInformationText(L"🔡 (raw)");
    editor_state->current_buffer()->set_mode(
        std::make_unique<RawInputTypeMode>(options.buffer));
  } else if (editor_state->structure() == StructureChar()) {
    options.buffer->CheckPosition();
    options.buffer->PushTransformationStack();
    options.buffer->PushTransformationStack();
    EnterInsertCharactersMode(options);
  } else if (editor_state->structure() == StructureLine()) {
    options.buffer->CheckPosition();
    options.buffer->PushTransformationStack();
    options.buffer->PushTransformationStack();
    options.buffer->ApplyToCursors(
        std::make_unique<InsertEmptyLineTransformation>(
            editor_state->direction()));
    EnterInsertCharactersMode(options);
  }
  editor_state->ResetDirection();
  editor_state->ResetStructure();
}

}  // namespace afc::editor

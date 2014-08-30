#include "buffer.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include "char_buffer.h"
#include "editor.h"
#include "file_link_mode.h"
#include "run_command_handler.h"
#include "lazy_string_append.h"
#include "server.h"
#include "substring.h"
#include "transformation.h"
#include "vm/vm.h"

namespace {

using namespace afc::editor;
using std::cerr;

void SaveDiff(EditorState* editor_state, OpenBuffer* buffer) {
  unique_ptr<OpenBuffer> original(new OpenBuffer(editor_state, "- original diff"));
  buffer->ReloadInto(editor_state, original.get());
  while (original->fd() != -1) {
    original->ReadData(editor_state);
  }

  char* path_old_diff = strdup("patch-old-diff-XXXXXX");
  int fd_old_diff = mkstemp(path_old_diff);
  char* path_new_diff = strdup("patch-new-diff-XXXXXX");
  int fd_new_diff = mkstemp(path_new_diff);

  SaveContentsToOpenFile(editor_state, original.get(), path_old_diff, fd_old_diff);
  SaveContentsToOpenFile(editor_state, buffer, path_new_diff, fd_new_diff);
  close(fd_old_diff);
  close(fd_new_diff);
  RunCommandHandler("./diff_writer.py " + string(path_old_diff) + " " + string(path_new_diff), editor_state);
  editor_state->SetStatus("Changing diff");
}

}  // namespace

namespace afc {
namespace editor {

using namespace afc::vm;
using std::to_string;

/* static */ const string OpenBuffer::kBuffersName = "- buffers";
/* static */ const string OpenBuffer::kPasteBuffer = "- paste buffer";

OpenBuffer::OpenBuffer(EditorState* editor_state, const string& name)
    : name_(name),
      fd_(-1),
      fd_is_terminal_(false),
      buffer_(nullptr),
      buffer_line_start_(0),
      buffer_length_(0),
      buffer_size_(0),
      child_pid_(-1),
      child_exit_status_(0),
      view_start_line_(0),
      view_start_column_(0),
      position_(0, 0),
      modified_(false),
      reading_from_parser_(false),
      bool_variables_(BoolStruct()->NewInstance()),
      string_variables_(StringStruct()->NewInstance()) {
  ClearContents();

  {
    unique_ptr<afc::vm::Value> open_buffer_function(new Value(VMType::FUNCTION));
    open_buffer_function->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    open_buffer_function->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    open_buffer_function->function1 =
        [editor_state](unique_ptr<Value> path_arg) {
          assert(path_arg->type == VMType::VM_STRING);
          string path = path_arg->str;
          editor_state->set_current_buffer(OpenFile(editor_state, path, path));
          return std::move(Value::Void());
        };
    evaluator_.Define("OpenBuffer", std::move(open_buffer_function));
  }

  {
    unique_ptr<afc::vm::Value> connect_to_function(new Value(VMType::FUNCTION));
    connect_to_function->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    connect_to_function->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    connect_to_function->function1 =
        [editor_state](unique_ptr<Value> path) {
          assert(path->type == VMType::VM_STRING);
          OpenServerBuffer(editor_state, path->str);
          return std::move(Value::Void());
        };
    evaluator_.Define("ConnectTo", std::move(connect_to_function));
  }

  {
    unique_ptr<afc::vm::Value> set_status_function(new Value(VMType::FUNCTION));
    set_status_function->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    set_status_function->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    set_status_function->function1 =
        [editor_state](unique_ptr<Value> message) {
          assert(message->type == VMType::VM_STRING);
          editor_state->SetStatus(message->str);
          return std::move(Value::Void());
        };
    evaluator_.Define("SetStatus", std::move(set_status_function));
  }

  {
    unique_ptr<afc::vm::Value> insert_text(new Value(VMType::FUNCTION));
    insert_text->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    insert_text->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    insert_text->function1 =
        [editor_state](unique_ptr<Value> text) {
          assert(text->type == VMType::VM_STRING);
          if (!editor_state->has_current_buffer()) { return Value::Void(); }
          shared_ptr<OpenBuffer> buffer =
              editor_state->current_buffer()->second;

          shared_ptr<OpenBuffer> buffer_to_insert(
              new OpenBuffer(editor_state, "tmp buffer"));

          // getline will silently eat the last (empty) line.
          std::istringstream text_stream(text->str + "\n");
          std::string line;
          while (std::getline(text_stream, line, '\n')) {
            buffer_to_insert->AppendLine(editor_state, NewCopyString(line));
          }

          // Skip the last (empty) line.
          buffer_to_insert->contents()->pop_back();
          unique_ptr<Transformation> transformation(
              NewInsertBufferTransformation(
                  buffer_to_insert, buffer->position(), 1));
          buffer->Apply(editor_state, *transformation);
          return Value::Void();
        };
    evaluator_.Define("InsertText", std::move(insert_text));
  }
}

void OpenBuffer::Close(EditorState* editor_state) {
  if (read_bool_variable(variable_save_on_close())) {
    Save(editor_state);
  }
}

void OpenBuffer::ClearContents() {
  contents_.clear();
  contents_.push_back(shared_ptr<Line>(new Line(EmptyString())));
}

void OpenBuffer::EndOfFile(EditorState* editor_state) {
  close(fd_);
  buffer_ = static_cast<char*>(realloc(buffer_, buffer_length_));
  buffer_size_ = buffer_length_;
  if (child_pid_ != -1) {
    if (waitpid(child_pid_, &child_exit_status_, 0) == -1) {
      editor_state->SetStatus("waitpid failed: " + string(strerror(errno)));
      return;
    }
  }
  fd_ = -1;
  child_pid_ = -1;
  if (read_bool_variable(variable_reload_after_exit())) {
    set_bool_variable(variable_reload_after_exit(),
        read_bool_variable(variable_default_reload_after_exit()));
    Reload(editor_state);
  }
  if (read_bool_variable(variable_close_after_clean_exit())
      && WIFEXITED(child_exit_status_)
      && WEXITSTATUS(child_exit_status_) == 0) {
    auto it = editor_state->buffers()->find(name_);
    if (it != editor_state->buffers()->end()) {
      editor_state->CloseBuffer(it);
    }
  }

  if (editor_state->has_current_buffer()
      && editor_state->current_buffer()->first == kBuffersName) {
    editor_state->current_buffer()->second->Reload(editor_state);
  }
}

void OpenBuffer::ReadData(EditorState* editor_state) {
  assert(fd_ > 0);
  assert(buffer_line_start_ <= buffer_length_);
  assert(buffer_length_ <= buffer_size_);
  if (buffer_length_ == buffer_size_) {
    buffer_size_ = buffer_size_ ? buffer_size_ * 2 : 64 * 1024;
    buffer_ = static_cast<char*>(realloc(buffer_, buffer_size_));
  }
  ssize_t characters_read = read(fd_, buffer_ + buffer_length_, buffer_size_ - buffer_length_);
  if (characters_read == -1) {
    if (errno == EAGAIN) {
      return;
    }
    return EndOfFile(editor_state);
  }
  assert(characters_read >= 0);
  if (characters_read == 0) {
    return EndOfFile(editor_state);
  }

  shared_ptr<LazyString> buffer_wrapper(
      NewMoveableCharBuffer(
          &buffer_, buffer_length_ + static_cast<size_t>(characters_read)));
  for (size_t i = buffer_length_;
       i < buffer_length_ + static_cast<size_t>(characters_read);
       i++) {
    if (buffer_[i] == '\n') {
      bool was_at_end = at_end();
      AppendLine(
          editor_state,
          Substring(buffer_wrapper, buffer_line_start_, i - buffer_line_start_));
      if (was_at_end) {
        set_position(end_position());
      }
      assert(position_.line < contents_.size());
      buffer_line_start_ = i + 1;
      if (editor_state->has_current_buffer()
          && editor_state->current_buffer()->second.get() == this
          && contents_.size() <= view_start_line_ + editor_state->visible_lines()) {
        editor_state->ScheduleRedraw();
      }
    }
  }
  buffer_length_ += static_cast<size_t>(characters_read);
  if (editor_state->has_current_buffer()
      && editor_state->current_buffer()->first == kBuffersName) {
    editor_state->current_buffer()->second->Reload(editor_state);
  }
}

void OpenBuffer::Reload(EditorState* editor_state) {
  if (child_pid_ != -1) {
    kill(-child_pid_, SIGTERM);
    reload_after_exit_ = true;
    return;
  }
  ReloadInto(editor_state, this);
  set_modified(false);
  CheckPosition();
}

void OpenBuffer::Save(EditorState* editor_state) {
  if (read_bool_variable(variable_diff())) {
    SaveDiff(editor_state, this);
    return;
  }
  editor_state->SetStatus("Buffer can't be saved.");
}

void OpenBuffer::AppendLazyString(
    EditorState* editor_state, shared_ptr<LazyString> input) {
  size_t size = input->size();
  size_t start = 0;
  for (size_t i = 0; i < size; i++) {
    if (input->get(i) == '\n') {
      AppendLine(editor_state, Substring(input, start, i - start));
      start = i + 1;
    }
  }
}

static void AddToParseTree(const shared_ptr<LazyString>& str_input) {
  string str = str_input->ToString();
}

void OpenBuffer::AppendLine(EditorState* editor_state, shared_ptr<LazyString> str) {
  assert(str != nullptr);
  if (reading_from_parser_) {
    switch (str->get(0)) {
      case 'E':
        return AppendRawLine(editor_state, Substring(str, 1));

      case 'T':
        AddToParseTree(str);
        return;
    }
    return;
  }

  if (contents_.empty()) {
    if (str->ToString() == "EDGE PARSER v1.0") {
      reading_from_parser_ = true;
      return;
    }
  }

  AppendRawLine(editor_state, str);
}

void OpenBuffer::AppendRawLine(
    EditorState* editor_state, shared_ptr<LazyString> str) {
  assert(!contents_.empty());
  (*contents_.rbegin()).reset(new Line(
      StringAppend((*contents_.rbegin())->contents, str)));
  contents_.push_back(shared_ptr<Line>(new Line(EmptyString())));
}

void OpenBuffer::Evaluate(
    EditorState* editor_state, const string& str) {
  evaluator_.AppendInput(str);
}

LineColumn OpenBuffer::InsertInCurrentPosition(
    const vector<shared_ptr<Line>>& insertion) {
  return InsertInPosition(insertion, position_);
}

LineColumn OpenBuffer::InsertInPosition(
    const vector<shared_ptr<Line>>& insertion, const LineColumn& position) {
  if (insertion.empty()) { return position; }
  auto head = Substring(contents_.at(position.line)->contents, 0, position.column);
  auto tail = Substring(contents_.at(position.line)->contents, position.column);
  contents_.insert(contents_.begin() + position.line + 1, insertion.begin() + 1, insertion.end());
  size_t line_end = position.line + insertion.size() - 1;
  if (insertion.size() == 1) {
    auto line_to_insert = insertion.at(0)->contents;
    contents_.at(position.line).reset(new Line(
        StringAppend(head, StringAppend(line_to_insert, tail))));
    return LineColumn(position.line, head->size() + line_to_insert->size());
  } else {
    contents_.at(position.line).reset(
        new Line(StringAppend(head, (*insertion.begin())->contents)));
    contents_.at(line_end).reset(
        new Line(StringAppend((*insertion.rbegin())->contents, tail)));
  }
  return LineColumn(line_end,
      (insertion.size() == 1 ? head->size() : 0)
      + (*insertion.rbegin())->contents->size());
}

void OpenBuffer::MaybeAdjustPositionCol() {
  if (contents_.empty()) { return; }
  size_t line_length = current_line()->contents->size();
  if (position_.column > line_length) {
    position_.column = line_length;
  }
}

void OpenBuffer::CheckPosition() {
  if (position_.line >= contents_.size()) {
    position_.line = contents_.size();
    if (position_.line > 0) {
      position_.line--;
    }
  }
}

bool OpenBuffer::BoundWordAt(
    const LineColumn& position_input, LineColumn* start, LineColumn* end) {
  const string& word_char = read_string_variable(variable_word_characters());
  LineColumn position(position_input);

  // Seek forwards until we're at a word character.
  while (at_end_of_line(position)
         || word_char.find(character_at(position)) == word_char.npos) {
    if (at_end(position)) {
      return false;
    } else if (at_end_of_line(position)) {
      position.column = 0;
      position.line++;
    } else {
      position.column ++;
    }
  }

  // Seek backwards until we're at the beginning of the word.
  while (!at_beginning_of_line(position)
         && word_char.find(character_at(LineColumn(position.line, position.column - 1))) != string::npos) {
    assert(position.column > 0);
    position.column--;
  }

  *start = position;

  // Seek forwards until the next space.
  while (!at_end_of_line(position)
         && word_char.find(character_at(position)) != string::npos) {
    position.column ++;
  }

  *end = position;
  return true;
}

string OpenBuffer::ToString() const {
  size_t size = 0;
  for (auto& it : contents_) {
    size += it->contents->size() + 1;
  }
  string output;
  output.reserve(size);
  for (auto& it : contents_) {
    output.append(it->contents->ToString() + "\n");
  }
  output = output.substr(0, output.size() - 1);
  return output;
}

void OpenBuffer::SetInputFile(
    int input_fd, bool fd_is_terminal, pid_t child_pid) {
  if (read_bool_variable(variable_clear_on_reload())) {
    ClearContents();
    buffer_ = nullptr;
    buffer_line_start_ = 0;
    buffer_length_ = 0;
    buffer_size_ = 0;
  }
  if (fd_ != -1) {
    close(fd_);
  }
  assert(child_pid_ == -1);
  fd_ = input_fd;
  fd_is_terminal_ = fd_is_terminal;
  child_pid_ = child_pid;
}

string OpenBuffer::FlagsString() const {
  string output;
  if (modified()) {
    output += "~";
  }
  if (fd() != -1) {
    output += "< l:" + to_string(contents_.size());
  }
  if (child_pid_ != -1) {
    output += " pid:" + to_string(child_pid_);
  } else if (child_exit_status_ != 0) {
    if (WIFEXITED(child_exit_status_)) {
      output += " exit:" + to_string(WEXITSTATUS(child_exit_status_));
    } else if (WIFSIGNALED(child_exit_status_)) {
      output += " signal:" + to_string(WTERMSIG(child_exit_status_));
    } else {
      output += " exit-status:" + to_string(child_exit_status_);
    }
  }
  return output;
}

/* static */ EdgeStruct<char>* OpenBuffer::BoolStruct() {
  static EdgeStruct<char>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<char>;
    // Trigger registration of all fields.
    OpenBuffer::variable_pts();
    OpenBuffer::variable_close_after_clean_exit();
    OpenBuffer::variable_reload_after_exit();
    OpenBuffer::variable_default_reload_after_exit();
    OpenBuffer::variable_reload_on_enter();
    OpenBuffer::variable_atomic_lines();
    OpenBuffer::variable_diff();
    OpenBuffer::variable_save_on_close();
    OpenBuffer::variable_clear_on_reload();
  }
  return output;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_pts() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "pts",
      "If a command is forked that writes to this buffer, should it be run "
      "with its own pseudoterminal?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_close_after_clean_exit() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "close_after_clean_exit",
      "If a command is forked that writes to this buffer, should the buffer be "
      "closed when the command exits with a successful status code?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_reload_after_exit() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "reload_after_exit",
      "If a forked command that writes to this buffer exits, should Edge "
      "reload the buffer?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_default_reload_after_exit() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "default_reload_after_exit",
      "If a forked command that writes to this buffer exits and "
      "reload_after_exit is set, what should Edge set reload_after_exit just "
      "after reloading the buffer?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_reload_on_enter() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "reload_on_enter",
      "Should this buffer be reloaded automatically when visited?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_atomic_lines() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "atomic_lines",
      "If true, lines can't be joined (e.g. you can't delete the last "
      "character in a line unless the line is empty).  This is used by certain "
      "buffers that represent lists of things (each represented as a line), "
      "for which this is a natural behavior.",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_diff() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "diff",
      "Does this buffer represent a diff?  If true, when it gets saved the "
      "original contents are reloaded into a separate buffer, an attempt is "
      "made to revert them and then an attempt is made to apply the new "
      "contents.",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_save_on_close() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "save_on_close",
      "Should this buffer be saved automatically when it's closed?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_clear_on_reload() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      "clear_on_reload",
      "Should any previous contents be discarded when this buffer is reloaded? "
      "If false, previous contents will be preserved and new contents will be "
      "appended at the end.",
      true);
  return variable;
}

/* static */ EdgeStruct<string>* OpenBuffer::StringStruct() {
  static EdgeStruct<string>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<string>;
    // Trigger registration of all fields.
    OpenBuffer::variable_word_characters();
    OpenBuffer::variable_path_characters();
    OpenBuffer::variable_path();
    OpenBuffer::variable_line_prefix_characters();
  }
  return output;
}

/* static */ EdgeVariable<string>* OpenBuffer::variable_word_characters() {
  static EdgeVariable<string>* variable = StringStruct()->AddVariable(
      "word_characters",
      "String with all the characters that should be considered part of a "
      "word.",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_");
  return variable;
}

/* static */ EdgeVariable<string>* OpenBuffer::variable_path_characters() {
  static EdgeVariable<string>* variable = StringStruct()->AddVariable(
      "path_characters",
      "String with all the characters that should be considered part of a "
      "path.",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.*:/");
  return variable;
}

/* static */ EdgeVariable<string>* OpenBuffer::variable_path() {
  static EdgeVariable<string>* variable = StringStruct()->AddVariable(
      "path",
      "String with the path of the current file.",
      "");
  return variable;
}

/* static */ EdgeVariable<string>*
OpenBuffer::variable_line_prefix_characters() {
  static EdgeVariable<string>* variable = StringStruct()->AddVariable(
      "line_prefix_characters",
      "String with all the characters that should be considered the prefix of "
      "the actual contents of a line.  When a new line is created, the prefix "
      "of the previous line (the sequence of all characters at the start of "
      "the previous line that are listed in line_prefix_characters) is copied "
      "to the new line.  The order of elements in line_prefix_characters has "
      "no effect.",
      " ");
  return variable;
}

bool OpenBuffer::read_bool_variable(const EdgeVariable<char>* variable) {
  return static_cast<bool>(bool_variables_.Get(variable));
}

void OpenBuffer::set_bool_variable(
    const EdgeVariable<char>* variable, bool value) {
  bool_variables_.Set(variable, static_cast<char>(value));
}

void OpenBuffer::toggle_bool_variable(const EdgeVariable<char>* variable) {
  set_bool_variable(variable, !read_bool_variable(variable));
}

const string& OpenBuffer::read_string_variable(const EdgeVariable<string>* variable) {
  return string_variables_.Get(variable);
}

void OpenBuffer::set_string_variable(
    const EdgeVariable<string>* variable, const string& value) {
  string_variables_.Set(variable, value);
}

void OpenBuffer::CopyVariablesFrom(const shared_ptr<const OpenBuffer>& src) {
  assert(src.get() != nullptr);
  bool_variables_.CopyFrom(src->bool_variables_);
  string_variables_.CopyFrom(src->string_variables_);
}

void OpenBuffer::Apply(
    EditorState* editor_state, const Transformation& transformation) {
  unique_ptr<Transformation> undo = transformation.Apply(editor_state, this);
  assert(undo != nullptr);
  undo_history_.push_back(std::move(undo));
}

void OpenBuffer::Undo(EditorState* editor_state) {
  if (undo_history_.empty()) { return; }
  undo_history_.back()->Apply(editor_state, this);
  undo_history_.pop_back();
}

}  // namespace editor
}  // namespace afc

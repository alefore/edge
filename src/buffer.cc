#include "buffer.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <sstream>
#include <stdexcept>
#include <string>

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "char_buffer.h"
#include "editor.h"
#include "file_link_mode.h"
#include "run_command_handler.h"
#include "lazy_string_append.h"
#include "server.h"
#include "substring.h"
#include "transformation.h"
#include "transformation_delete.h"
#include "vm/public/value.h"
#include "vm/public/vm.h"
#include "wstring.h"

namespace afc {
namespace editor {

namespace {

using std::unordered_set;

static void RegisterBufferFieldBool(afc::vm::ObjectType* object_type,
                                    const EdgeVariable<char>* variable) {
  using namespace afc::vm;
  assert(variable != nullptr);

  // Getter.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_BOOLEAN));
    callback->type.type_arguments.push_back(VMType::ObjectType(object_type));
    callback->callback =
        [variable](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          return Value::NewBool(buffer->read_bool_variable(variable));
        };
    object_type->AddField(variable->name(), std::move(callback));
  }

  // Setter.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType::ObjectType(object_type));
    callback->type.type_arguments.push_back(VMType(VMType::VM_BOOLEAN));
    callback->callback =
        [variable](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          assert(args[1]->type == VMType::VM_BOOLEAN);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          buffer->set_bool_variable(variable, args[1]->boolean);
          return std::move(Value::NewVoid());
        };
    object_type->AddField(L"set_" + variable->name(), std::move(callback));
  }
}

static void RegisterBufferFieldString(afc::vm::ObjectType* object_type,
                                      const EdgeVariable<wstring>* variable) {
  using namespace afc::vm;
  assert(variable != nullptr);

  // Getter.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType::ObjectType(object_type));
    callback->callback =
        [variable](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          return Value::NewString(buffer->read_string_variable(variable));
        };
    object_type->AddField(variable->name(), std::move(callback));
  }

  // Setter.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType::ObjectType(object_type));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback =
        [variable](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          assert(args[1]->type == VMType::VM_STRING);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          buffer->set_string_variable(variable, args[1]->str);
          return std::move(Value::NewVoid());
        };
    object_type->AddField(L"set_" + variable->name(), std::move(callback));
  }
}

static void RegisterBufferFieldInt(afc::vm::ObjectType* object_type,
                                   const EdgeVariable<int>* variable) {
  using namespace afc::vm;
  assert(variable != nullptr);

  // Getter.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType::ObjectType(object_type));
    callback->callback =
        [variable](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          return Value::NewInteger(buffer->read_int_variable(variable));
        };
    object_type->AddField(variable->name(), std::move(callback));
  }

  // Setter.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType::ObjectType(object_type));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback =
        [variable](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          assert(args[1]->type == VMType::VM_INTEGER);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          buffer->set_int_variable(variable, args[1]->integer);
          return std::move(Value::NewVoid());
        };
    object_type->AddField(L"set_" + variable->name(), std::move(callback));
  }
}

static void RegisterBufferFieldValue(
    afc::vm::ObjectType* object_type,
    const EdgeVariable<unique_ptr<Value>>* variable) {
  using namespace afc::vm;
  assert(variable != nullptr);

  // Getter.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(variable->type());
    callback->type.type_arguments.push_back(VMType::ObjectType(object_type));
    callback->callback =
        [variable](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          unique_ptr<Value> value = Value::NewVoid();
          *value = *buffer->read_value_variable(variable);
          return value;
        };
    object_type->AddField(variable->name(), std::move(callback));
  }

  // Setter.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType::ObjectType(object_type));
    callback->type.type_arguments.push_back(variable->type());
    callback->callback =
        [variable](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          assert(args[1]->type == variable->type());
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          unique_ptr<Value> value = Value::NewVoid();
          *value = *args[1];
          buffer->set_value_variable(variable, std::move(value));
          return std::move(Value::NewVoid());
        };
    object_type->AddField(L"set_" + variable->name(), std::move(callback));
  }
}

}  // namespace

using namespace afc::vm;
using std::to_wstring;

/* static */ const wstring OpenBuffer::kBuffersName = L"- buffers";
/* static */ const wstring OpenBuffer::kPasteBuffer = L"- paste buffer";

/* static */ void OpenBuffer::RegisterBufferType(
    EditorState* editor_state, afc::vm::Environment* environment) {
  unique_ptr<ObjectType> buffer(new ObjectType(L"Buffer"));

  {
    vector<wstring> variable_names;
    StringStruct()->RegisterVariableNames(&variable_names);
    for (const wstring& name : variable_names) {
      RegisterBufferFieldString(
          buffer.get(), StringStruct()->find_variable(name));
    }
  }

  {
    vector<wstring> variable_names;
    BoolStruct()->RegisterVariableNames(&variable_names);
    for (const wstring& name : variable_names) {
      RegisterBufferFieldBool(
          buffer.get(), BoolStruct()->find_variable(name));
    }
  }

  {
    vector<wstring> variable_names;
    IntStruct()->RegisterVariableNames(&variable_names);
    for (const wstring& name : variable_names) {
      RegisterBufferFieldInt(buffer.get(), IntStruct()->find_variable(name));
    }
  }

  {
    vector<wstring> variable_names;
    ValueStruct()->RegisterVariableNames(&variable_names);
    for (const wstring& name : variable_names) {
      RegisterBufferFieldValue(
          buffer.get(), ValueStruct()->find_variable(name));
    }
  }

  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));
    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          assert(args.size() == 1);
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          return Value::NewInteger(buffer->contents()->size());
        };
    buffer->AddField(L"line_count", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));
    callback->type.type_arguments.push_back(VMType::ObjectType(L"LineColumn"));
    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          assert(args.size() == 2);
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          buffer->set_position(
              *static_cast<LineColumn*>(args[1]->user_value.get()));
          return Value::NewVoid();
        };
    buffer->AddField(L"set_position", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType::ObjectType(L"LineColumn"));
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));
    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          assert(args.size() == 1);
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          return Value::NewObject(L"LineColumn", shared_ptr<LineColumn>(
              new LineColumn(buffer->position())));
        };
    buffer->AddField(L"position", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          assert(args.size() == 2);
          assert(args[0]->type == VMType::OBJECT_TYPE);
          assert(args[1]->type == VMType::VM_INTEGER);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          return Value::NewString(
              buffer->contents()->at(args[1]->integer)->ToString());
        };
    buffer->AddField(L"line", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));
    VMType function_argument(VMType::FUNCTION);
    function_argument.type_arguments.push_back(VMType(VMType::VM_STRING));
    function_argument.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(function_argument);
    callback->callback =
        [editor_state](vector<unique_ptr<Value>> args) {
          assert(args.size() == 2);
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          LineColumn old_position = buffer->position();
          buffer->set_position(LineColumn(0));
          unique_ptr<TransformationStack> transformation(
              new TransformationStack());
          while (buffer->position().line + 1 < buffer->contents()->size()) {
            wstring current_line = buffer->current_line()->ToString();
            vector<unique_ptr<Value>> line_args;
            line_args.push_back(Value::NewString(current_line));
            unique_ptr<Value> result = args[1]->callback(std::move(line_args));
            if (result->str != current_line) {
              transformation->PushBack(
                  NewDeleteLinesTransformation(Modifiers(), false));
              shared_ptr<OpenBuffer> buffer_to_insert(
                  new OpenBuffer(editor_state, L"tmp buffer"));
              buffer_to_insert->AppendLine(
                  editor_state, NewCopyString(result->str));
              transformation->PushBack(
                  NewInsertBufferTransformation(buffer_to_insert, 1, END));
            }
            buffer->set_position(LineColumn(buffer->position().line + 1));
          }
          buffer->Apply(editor_state, std::move(transformation));
          buffer->set_position(old_position);
          return Value::NewVoid();
        };
    buffer->AddField(L"Map", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));
    VMType function_argument(VMType::FUNCTION);
    function_argument.type_arguments.push_back(VMType(VMType::VM_BOOLEAN));
    function_argument.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(function_argument);
    callback->callback =
        [editor_state](vector<unique_ptr<Value>> args) {
          assert(args.size() == 2);
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          buffer->set_filter(std::move(args[1]));
          editor_state->ScheduleRedraw();
          return Value::NewVoid();
        };
    buffer->AddField(L"Filter", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    // The buffer to modify.
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));
    // The number of characters to delete.
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback =
        [editor_state](vector<unique_ptr<Value>> args) {
          CHECK(args.size() == 2);
          CHECK(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          CHECK(buffer != nullptr);

          Modifiers modifiers;
          modifiers.repetitions = args[1]->integer;
          buffer->Apply(editor_state,
              NewDeleteCharactersTransformation(modifiers, true));
          return Value::NewVoid();
        };
    buffer->AddField(L"DeleteCharacters", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    // The buffer to modify.
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));
    // The text to insert.
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback =
        [editor_state](vector<unique_ptr<Value>> args) {
          CHECK(args.size() == 2);
          CHECK(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          CHECK(buffer != nullptr);

          shared_ptr<OpenBuffer> buffer_to_insert(
              new OpenBuffer(editor_state, L"tmp buffer"));

          // getline will silently eat the last (empty) line.
          std::wistringstream text_stream(args[1]->str + L"\n");
          std::wstring line;
          while (std::getline(text_stream, line, wchar_t('\n'))) {
            buffer_to_insert->AppendLine(editor_state, NewCopyString(line));
          }

          buffer->Apply(editor_state,
              NewInsertBufferTransformation(buffer_to_insert, 1, END));

          return Value::NewVoid();
        };
    buffer->AddField(L"InsertText", std::move(callback));
  }

  environment->DefineType(L"Buffer", std::move(buffer));
}

OpenBuffer::OpenBuffer(EditorState* editor_state, const wstring& name)
    : name_(name),
      fd_(-1),
      fd_is_terminal_(false),
      child_pid_(-1),
      child_exit_status_(0),
      position_pts_(LineColumn(0, 0)),
      view_start_line_(0),
      view_start_column_(0),
      line_(this, 0),
      column_(0),
      modified_(false),
      reading_from_parser_(false),
      bool_variables_(BoolStruct()->NewInstance()),
      string_variables_(StringStruct()->NewInstance()),
      int_variables_(IntStruct()->NewInstance()),
      function_variables_(ValueStruct()->NewInstance()),
      environment_(editor_state->environment()),
      filter_version_(0),
      last_transformation_(NewNoopTransformation()) {
  environment_.Define(L"buffer", Value::NewObject(
      L"Buffer", shared_ptr<void>(this, [](void*){})));
  set_string_variable(variable_path(), L"");
  set_string_variable(variable_pts_path(), L"");
  set_string_variable(variable_command(), L"");
  set_bool_variable(variable_reload_after_exit(), false);
  ClearContents();
}

OpenBuffer::~OpenBuffer() {}

void OpenBuffer::Close(EditorState* editor_state) {
  LOG(INFO) << "Closing buffer: " << name_;
  if (read_bool_variable(variable_save_on_close())) {
    Save(editor_state);
  }
}

void OpenBuffer::ClearContents() {
  contents_.clear();
  position_pts_ = LineColumn();
}

void OpenBuffer::EndOfFile(EditorState* editor_state) {
  close(fd_);
  low_buffer_ = nullptr;
  low_buffer_length_ = 0;
  if (child_pid_ != -1) {
    if (waitpid(child_pid_, &child_exit_status_, 0) == -1) {
      editor_state->SetStatus(
          L"waitpid failed: " + FromByteString(strerror(errno)));
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
  static const size_t kLowBufferSize = 1024 * 60;
  if (low_buffer_ == nullptr) {
    CHECK_EQ(low_buffer_length_, 0);
    low_buffer_ = static_cast<char*>(malloc(kLowBufferSize));
  }
  ssize_t characters_read =
      read(fd_, low_buffer_ + low_buffer_length_,
           kLowBufferSize - low_buffer_length_);
  LOG(INFO) << "Read returns: " << characters_read;
  if (characters_read == -1) {
    if (errno == EAGAIN) {
      return;
    }
    return EndOfFile(editor_state);
  }
  CHECK_GE(characters_read, 0);
  CHECK_LE(characters_read, kLowBufferSize - low_buffer_length_);
  if (characters_read == 0) {
    return EndOfFile(editor_state);
  }
  low_buffer_length_ += characters_read;

  const char* low_buffer_tmp = low_buffer_;
  std::vector<wchar_t> buffer(
      1 + mbsnrtowcs(nullptr, &low_buffer_tmp, low_buffer_length_, 0, nullptr));

  low_buffer_tmp = low_buffer_;
  mbsnrtowcs(&buffer[0], &low_buffer_tmp, low_buffer_length_, buffer.size(),
             nullptr);

  shared_ptr<LazyString> buffer_wrapper(NewStringFromVector(std::move(buffer)));
  VLOG(5) << "Input: [" << buffer_wrapper->ToString() << "]";

  size_t processed = low_buffer_tmp == nullptr
      ? low_buffer_length_
      : low_buffer_tmp - low_buffer_;
  VLOG(5) << name_ << ": Characters consumed: " << processed;
  VLOG(5) << name_ << ": Characters produced: " << buffer_wrapper->size();
  CHECK_LE(processed, low_buffer_length_);
  memmove(low_buffer_, low_buffer_tmp, low_buffer_length_ - processed);
  low_buffer_length_ -= processed;
  if (low_buffer_length_ == 0) {
    LOG(INFO) << "Consumed all input.";
    free(low_buffer_);
    low_buffer_ = nullptr;
  }

  if (contents_.empty()) {
    contents_.emplace_back(new Line(Line::Options()));
    if (read_bool_variable(variable_follow_end_of_file())) {
      line_ = BufferLineIterator(this, contents_.size());
    }
  }

  if (read_bool_variable(variable_vm_exec())) {
    EvaluateString(editor_state, buffer_wrapper->ToString());
  }

  if (read_bool_variable(variable_pts())) {
    ProcessCommandInput(editor_state, buffer_wrapper);
    editor_state->ScheduleRedraw();
  } else {
    size_t line_start = 0;
    for (size_t i = 0; i < buffer_wrapper->size(); i++) {
      if (buffer_wrapper->get(i) == '\n') {
        auto line = Substring(buffer_wrapper, line_start, i - line_start);
        VLOG(6) << "Adding line of length: " << line->size();
        VLOG(7) << "Adding line: " << line->ToString();
        AppendToLastLine(editor_state, line);
        CHECK_LE(line_.line(), contents_.size());
        contents_.emplace_back(new Line(Line::Options()));
        if (read_bool_variable(variable_follow_end_of_file())) {
          line_ = BufferLineIterator(this, contents_.size());
        }
        line_start = i + 1;
        if (editor_state->has_current_buffer()
            && editor_state->current_buffer()->second.get() == this
            && contents_.size() <=
                   view_start_line_ + editor_state->visible_lines()) {
          editor_state->ScheduleRedraw();
        }
      }
    }
    if (line_start < buffer_wrapper->size()) {
      AppendToLastLine(
          editor_state,
          Substring(buffer_wrapper, line_start,
                    buffer_wrapper->size() - line_start));
    }
  }
  if (editor_state->has_current_buffer()
      && editor_state->current_buffer()->first == kBuffersName) {
    editor_state->current_buffer()->second->Reload(editor_state);
  }
  editor_state->ScheduleRedraw();
}

void OpenBuffer::Reload(EditorState* editor_state) {
  if (child_pid_ != -1) {
    kill(-child_pid_, SIGTERM);
    set_bool_variable(variable_reload_after_exit(), true);
    return;
  }
  for (const auto& dir : editor_state->edge_path()) {
    EvaluateFile(editor_state, dir + L"/hooks/buffer-reload.cc");
  }
  ReloadInto(editor_state, this);
  set_modified(false);
  CheckPosition();
  if (name_ != kBuffersName  // Endless recursion gets boring quickly.
      && editor_state->has_current_buffer()
      && editor_state->current_buffer()->first == kBuffersName) {
    LOG(INFO) << "Updating list of buffers: " << kBuffersName;
    editor_state->current_buffer()->second->Reload(editor_state);
  }
}

void OpenBuffer::Save(EditorState* editor_state) {
  LOG(INFO) << "Saving buffer: " << name_;
  editor_state->SetStatus(L"Buffer can't be saved.");
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
  AppendLine(editor_state, Substring(input, start, size - start));
}

static void AddToParseTree(const shared_ptr<LazyString>& str_input) {
  wstring str = str_input->ToString();
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
    if (str->ToString() == L"EDGE PARSER v1.0") {
      reading_from_parser_ = true;
      return;
    }
  }

  AppendRawLine(editor_state, str);
}

void OpenBuffer::AppendRawLine(EditorState*, shared_ptr<LazyString> str) {
  Line::Options options;
  options.contents = str;
  contents_.emplace_back(new Line(options));
  if (read_bool_variable(variable_follow_end_of_file())) {
    line_ = BufferLineIterator(this, contents_.size());
  }
}

void OpenBuffer::ProcessCommandInput(
    EditorState* editor_state, shared_ptr<LazyString> str) {
  assert(read_bool_variable(variable_pts()));
  if (position_pts_.line >= contents_.size()) {
    position_pts_.line = contents_.size() - 1;
  }
  CHECK_LT(position_pts_.line, contents_.size());
  auto current_line = contents_[position_pts_.line];

  std::unordered_set<Line::Modifier, hash<int>> modifiers;

  size_t read_index = 0;
  VLOG(5) << "Terminal input: " << str->ToString();
  while (read_index < str->size()) {
    int c = str->get(read_index);
    read_index++;
    if (c == '\b') {
      if (position_pts_.column > 0) {
        position_pts_.column--;
        if (read_bool_variable(variable_follow_end_of_file())) {
          column_ = position_pts_.column;
        }
      }
    } else if (c == '\a') {
      editor_state->SetStatus(L"beep!");
    } else if (c == '\r') {
      position_pts_.column = 0;
      if (read_bool_variable(variable_follow_end_of_file())) {
        column_ = position_pts_.column;
      }
    } else if (c == '\n') {
      position_pts_.line++;
      position_pts_.column = 0;
      if (position_pts_.line == contents_.size()) {
        contents_.emplace_back(new Line(Line::Options()));
      }
      CHECK_LT(position_pts_.line, contents_.size());
      current_line = contents_[position_pts_.line];
      if (read_bool_variable(variable_follow_end_of_file())) {
        line_ = BufferLineIterator(this, position_pts_.line);
        column_ = position_pts_.column;
      }
    } else if (c == 0x1b) {
      read_index = ProcessTerminalEscapeSequence(
          editor_state, str, read_index, &modifiers);
      CHECK_LT(position_pts_.line, contents_.size());
      current_line = contents_[position_pts_.line];
    } else if (isprint(c) || c == '\t') {
      current_line->SetCharacter(position_pts_.column, c, modifiers);
      position_pts_.column++;
      if (read_bool_variable(variable_follow_end_of_file())) {
        column_ = position_pts_.column;
      }
    } else {
      LOG(INFO) << "Unknown character: [" << c << "]\n";
    }
  }
}

size_t OpenBuffer::ProcessTerminalEscapeSequence(
    EditorState* editor_state, shared_ptr<LazyString> str, size_t read_index,
    std::unordered_set<Line::Modifier, hash<int>>* modifiers) {
  if (str->size() <= read_index) {
    LOG(INFO) << "Unhandled character sequence: "
              << Substring(str, read_index)->ToString() << ")\n";
    return read_index;
  }
  switch (str->get(read_index)) {
    case 'M':
      // cuu1: Up one line.
      if (position_pts_.line > 0) {
        position_pts_.line--;
        if (read_bool_variable(variable_follow_end_of_file())) {
          line_--;
          column_ = 0;
        }
        if (view_start_line_ > position_pts_.line) {
          view_start_line_ = position_pts_.line;
        }
      }
      return read_index + 1;
    case '[':
      break;
    default:
      LOG(INFO) << "Unhandled character sequence: "
                << Substring(str, read_index)->ToString();
  }
  read_index++;
  CHECK_LT(position_pts_.line, contents_.size());
  auto current_line = contents_[position_pts_.line];
  string sequence;
  while (read_index < str->size()) {
    int c = str->get(read_index);
    read_index++;
    switch (c) {
      case '@':
        // ich: insert character
        DLOG(INFO) << "Terminal: ich: Insert character.";
        current_line->InsertCharacterAtPosition(position_pts_.column);
        return read_index;

      case 'l':
        if (sequence == "?1") {
          sequence.push_back(c);
          continue;
        }
        if (sequence == "?1049") {
          // rmcup
        } else if (sequence == "?25") {
          LOG(INFO) << "Ignoring: Make cursor invisible";
        } else {
          LOG(INFO) << "Unhandled character sequence: " << sequence;
        }
        return read_index;

      case 'h':
        if (sequence == "?1") {
          sequence.push_back(c);
          continue;
        }
        if (sequence == "?1049") {
          // smcup
        } else if (sequence == "?25") {
          LOG(INFO) << "Ignoring: Make cursor visible";
        } else {
          LOG(INFO) << "Unhandled character sequence: " << sequence;
        }
        return read_index;

      case 'm':
        if (sequence == "") {
          modifiers->clear();
        } else if (sequence == "0") {
          modifiers->clear();
        } else if (sequence == "1") {
          modifiers->insert(Line::BOLD);
        } else if (sequence == "3") {
          // TODO(alejo): Support italic on.
        } else if (sequence == "4") {
          modifiers->insert(Line::UNDERLINE);
        } else if (sequence == "23") {
          // Fraktur off, italic off.  No need to do anything for now.
        } else if (sequence == "24") {
          modifiers->erase(Line::UNDERLINE);
        } else if (sequence == "31") {
          modifiers->clear();
          modifiers->insert(Line::RED);
        } else if (sequence == "32") {
          modifiers->clear();
          modifiers->insert(Line::GREEN);
        } else if (sequence == "36") {
          modifiers->clear();
          modifiers->insert(Line::CYAN);
        } else if (sequence == "1;30") {
          modifiers->clear();
          modifiers->insert(Line::BOLD);
          modifiers->insert(Line::BLACK);
        } else if (sequence == "1;31") {
          modifiers->clear();
          modifiers->insert(Line::BOLD);
          modifiers->insert(Line::RED);
        } else if (sequence == "1;36") {
          modifiers->clear();
          modifiers->insert(Line::BOLD);
          modifiers->insert(Line::CYAN);
        } else if (sequence == "0;36") {
          modifiers->clear();
          modifiers->insert(Line::CYAN);
        } else {
          LOG(INFO) << "Unhandled character sequence: (" << sequence;
        }
        return read_index;

      case '>':
        if (sequence == "?1l\E") {
          // rmkx: leave 'keyboard_transmit' mode
          // TODO(alejo): Handle it.
        } else {
          LOG(INFO) << "Unhandled character sequence: " << sequence;
        }
        return read_index;
        break;

      case '=':
        if (sequence == "?1h\E") {
          // smkx: enter 'keyboard_transmit' mode
          // TODO(alejo): Handle it.
        } else {
          LOG(INFO) << "Unhandled character sequence: " << sequence;
        }
        return read_index;
        break;

      case 'C':
        // cuf1: non-destructive space (move right one space)
        if (position_pts_.column >= current_line->size()) {
          return read_index;
        }
        position_pts_.column++;
        if (read_bool_variable(variable_follow_end_of_file())) {
          column_ = position_pts_.column;
        }
        return read_index;

      case 'H':
        // home: move cursor home.
        {
          size_t line_delta = 0, column_delta = 0;
          size_t pos = sequence.find(';');
          try {
            if (pos != wstring::npos) {
              line_delta = pos == 0 ? 0 : stoul(sequence.substr(0, pos)) - 1;
              column_delta = pos == sequence.size() - 1
                  ? 0 : stoul(sequence.substr(pos + 1)) - 1;
            } else if (!sequence.empty()) {
              line_delta = stoul(sequence);
            }
          } catch (const std::invalid_argument& ia) {
            editor_state->SetStatus(
                L"Unable to parse sequence from terminal in 'home' command: \""
                + FromByteString(sequence) + L"\"");
          }
          DLOG(INFO) << "Move cursor home: line: " << line_delta << ", column: "
                     << column_delta;
          position_pts_ =
              LineColumn(view_start_line_ + line_delta, column_delta);
          while (position_pts_.line >= contents_.size()) {
            contents_.emplace_back(new Line(Line::Options()));
          }
          if (read_bool_variable(variable_follow_end_of_file())) {
            line_ = BufferLineIterator(this, position_pts_.line);
            column_ = position_pts_.column;
          }
          view_start_column_ = column_delta;
        }
        return read_index;

      case 'J':
        // ed: clear to end of screen.
        contents_.erase(contents_.begin() + position_pts_.line + 1,
                        contents_.end());
        CHECK_LT(position_pts_.line, contents_.size());
        return read_index;

      case 'K':
        // el: clear to end of line.
        current_line->DeleteUntilEnd(position_pts_.column);
        return read_index;

      case 'M':
        // dl1: delete one line.
        contents_.erase(contents_.begin() + position_pts_.line);
        CHECK_LT(position_pts_.line, contents_.size());
        return read_index;

      case 'P':
        {
          current_line->DeleteCharacters(
              position_pts_.column,
              min(static_cast<size_t>(atoi(sequence.c_str())),
                  current_line->size()));
          return read_index;
        }
      default:
        sequence.push_back(c);
    }
  }
  LOG(INFO) << "Unhandled character sequence: " << sequence;
  return read_index;
}

void OpenBuffer::AppendToLastLine(
    EditorState* editor_state, shared_ptr<LazyString> str) {
  vector<unordered_set<Line::Modifier, hash<int>>> modifiers;
  AppendToLastLine(editor_state, str, modifiers);
}

void OpenBuffer::AppendToLastLine(
    EditorState*, shared_ptr<LazyString> str,
    const vector<unordered_set<Line::Modifier, hash<int>>>& modifiers) {
  if (contents_.empty()) {
    contents_.emplace_back(new Line(Line::Options()));
    if (read_bool_variable(variable_follow_end_of_file())) {
      line_ = BufferLineIterator(this, contents_.size());
    }
  }
  assert((*contents_.rbegin())->contents() != nullptr);
  Line::Options options;
  options.contents = StringAppend((*contents_.rbegin())->contents(), str);
  options.modifiers = (*contents_.rbegin())->modifiers();
  for (auto& it : modifiers) {
    options.modifiers.push_back(it);
  }
  contents_.rbegin()->reset(new Line(options));
}

void OpenBuffer::EvaluateString(EditorState* editor_state,
                                const wstring& code) {
  wstring error_description;
  unique_ptr<Expression> expression(
      CompileString(code, &environment_, &error_description));
  if (expression == nullptr) {
    editor_state->SetStatus(L"Compilation error: " + error_description);
    return;
  }
  Evaluate(expression.get(), &environment_);
}

void OpenBuffer::EvaluateFile(EditorState* editor_state, const wstring& path) {
  wstring error_description;
  unique_ptr<Expression> expression(
      CompileFile(ToByteString(path), &environment_, &error_description));
  if (expression == nullptr) {
    editor_state->SetStatus(L"Compilation error: " + error_description);
    return;
  }
  Evaluate(expression.get(), &environment_);
}

LineColumn OpenBuffer::InsertInCurrentPosition(
    const vector<shared_ptr<Line>>& insertion) {
  MaybeAdjustPositionCol();
  return InsertInPosition(insertion, position());
}

LineColumn OpenBuffer::InsertInPosition(
    const vector<shared_ptr<Line>>& insertion, const LineColumn& position) {
  if (insertion.empty()) { return position; }
  auto head = position.line >= contents_.size()
      ? EmptyString()
      : contents_.at(position.line)->Substring(0, position.column);
  auto tail = position.line >= contents_.size()
      ? EmptyString()
      : contents_.at(position.line)->Substring(position.column);
  contents_.insert(contents_.begin() + position.line, insertion.begin(), insertion.end() - 1);
  for (size_t i = 1; i < insertion.size() - 1; i++) {
    contents_.at(position.line + i)->set_modified(true);
  }
  if (insertion.size() == 1) {
    if (insertion.at(0)->size() == 0) { return position; }
    auto line_to_insert = insertion.at(0)->contents();
    Line::Options options;
    options.contents = StringAppend(head, StringAppend(line_to_insert, tail));
    if (position.line >= contents_.size()) {
      contents_.emplace_back(new Line(options));
    } else {
      contents_.at(position.line).reset(new Line(options));
    }
    contents_[position.line]->set_modified(true);
    return LineColumn(position.line, head->size() + line_to_insert->size());
  }
  size_t line_end = position.line + insertion.size() - 1;
  {
    Line::Options options;
    options.contents = StringAppend(head, (*insertion.begin())->contents());
    contents_.at(position.line).reset(new Line(options));
    if (contents_.at(position.line)->contents() != head) {
      contents_[position.line]->set_modified(true);
    }
  }
  {
    Line::Options options;
    options.contents = StringAppend((*insertion.rbegin())->contents(), tail);
    if (line_end >= contents_.size()) {
      contents_.emplace_back(new Line(options));
    } else {
      contents_.at(line_end).reset(new Line(options));
    }
  }
  if (head->size() > 0 || (*insertion.rbegin())->size() > 0) {
    contents_[line_end]->set_modified(true);
  }
  return LineColumn(line_end,
      (insertion.size() == 1 ? head->size() : 0)
      + (*insertion.rbegin())->size());
}

void OpenBuffer::MaybeAdjustPositionCol() {
  if (contents_.empty() || current_line() == nullptr) { return; }
  size_t line_length = current_line()->size();
  if (column_ > line_length) {
    column_ = line_length;
  }
}

void OpenBuffer::CheckPosition() {
  if (line_.line() > contents_.size()) {
    BufferLineIterator pos(this, contents_.size());
    line_ = pos;
  }
}

bool OpenBuffer::BoundWordAt(
    const LineColumn& position_input, LineColumn* start, LineColumn* end) {
  const wstring& word_char = read_string_variable(variable_word_characters());
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
         && word_char.find(character_at(LineColumn(position.line, position.column - 1))) != wstring::npos) {
    assert(position.column > 0);
    position.column--;
  }

  *start = position;

  // Seek forwards until the next space.
  while (!at_end_of_line(position)
         && word_char.find(character_at(position)) != wstring::npos) {
    position.column ++;
  }

  *end = position;
  return true;
}

wstring OpenBuffer::ToString() const {
  size_t size = 0;
  for (auto& it : contents_) {
    size += it->size() + 1;
  }
  wstring output;
  output.reserve(size);
  for (auto& it : contents_) {
    output.append(it->ToString() + L"\n");
  }
  output = output.substr(0, output.size() - 1);
  return output;
}

void OpenBuffer::PushSignal(EditorState* editor_state, int sig) {
  switch (sig) {
    case SIGINT:
      if (read_bool_variable(variable_pts())) {
        string sequence(1, 0x03);
        write(fd_, sequence.c_str(), sequence.size());
        editor_state->SetStatus(L"SIGINT");
      } else if (child_pid_ != -1) {
        editor_state->SetStatus(L"SIGINT >> pid:" + to_wstring(child_pid_));
        kill(child_pid_, sig);
      }
      break;

    case SIGTSTP:
      if (read_bool_variable(variable_pts())) {
        string sequence(1, 0x1a);
        write(fd_, sequence.c_str(), sequence.size());
      }
      // TODO(alejo): If not pts, we should pause ourselves.  This requires
      // calling the signal handler installed by ncurses so that we don't end up
      // with the terminal in a broken state.
      break;

    default:
      editor_state->SetStatus(L"Unexpected signal received: " + to_wstring(sig));
  }
}

void OpenBuffer::SetInputFile(
    int input_fd, bool fd_is_terminal, pid_t child_pid) {
  if (read_bool_variable(variable_clear_on_reload())) {
    ClearContents();
    low_buffer_ = nullptr;
    low_buffer_length_ = 0;
  }
  if (fd_ != -1) {
    close(fd_);
  }
  assert(child_pid_ == -1);
  fd_ = input_fd;
  fd_is_terminal_ = fd_is_terminal;
  child_pid_ = child_pid;
}

wstring OpenBuffer::FlagsString() const {
  wstring output;
  if (modified()) {
    output += L"~";
  }
  if (fd() != -1) {
    output += L"< l:" + to_wstring(contents_.size());
    if (read_bool_variable(variable_follow_end_of_file())) {
      output += L" follow";
    }
    wstring pts_path = read_string_variable(variable_pts_path());
    if (!pts_path.empty()) {
      output += L" " + pts_path;
    }
  }
  if (child_pid_ != -1) {
    output += L" pid:" + to_wstring(child_pid_);
  } else if (child_exit_status_ != 0) {
    if (WIFEXITED(child_exit_status_)) {
      output += L" exit:" + to_wstring(WEXITSTATUS(child_exit_status_));
    } else if (WIFSIGNALED(child_exit_status_)) {
      output += L" signal:" + to_wstring(WTERMSIG(child_exit_status_));
    } else {
      output += L" exit-status:" + to_wstring(child_exit_status_);
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
    OpenBuffer::variable_vm_exec();
    OpenBuffer::variable_close_after_clean_exit();
    OpenBuffer::variable_reload_after_exit();
    OpenBuffer::variable_default_reload_after_exit();
    OpenBuffer::variable_reload_on_enter();
    OpenBuffer::variable_atomic_lines();
    OpenBuffer::variable_save_on_close();
    OpenBuffer::variable_clear_on_reload();
    OpenBuffer::variable_paste_mode();
    OpenBuffer::variable_follow_end_of_file();
    OpenBuffer::variable_commands_background_mode();
    OpenBuffer::variable_reload_on_buffer_write();
  }
  return output;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_pts() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"pts",
      L"If a command is forked that writes to this buffer, should it be run "
      L"with its own pseudoterminal?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_vm_exec() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"vm_exec",
      L"If set, all input read into this buffer will be executed.",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_close_after_clean_exit() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"close_after_clean_exit",
      L"If a command is forked that writes to this buffer, should the buffer be "
      L"closed when the command exits with a successful status code?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_reload_after_exit() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"reload_after_exit",
      L"If a forked command that writes to this buffer exits, should Edge "
      L"reload the buffer?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_default_reload_after_exit() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"default_reload_after_exit",
      L"If a forked command that writes to this buffer exits and "
      L"reload_after_exit is set, what should Edge set reload_after_exit just "
      L"after reloading the buffer?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_reload_on_enter() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"reload_on_enter",
      L"Should this buffer be reloaded automatically when visited?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_atomic_lines() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"atomic_lines",
      L"If true, lines can't be joined (e.g. you can't delete the last "
      L"character in a line unless the line is empty).  This is used by certain "
      L"buffers that represent lists of things (each represented as a line), "
      L"for which this is a natural behavior.",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_save_on_close() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"save_on_close",
      L"Should this buffer be saved automatically when it's closed?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_clear_on_reload() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"clear_on_reload",
      L"Should any previous contents be discarded when this buffer is reloaded? "
      L"If false, previous contents will be preserved and new contents will be "
      L"appended at the end.",
      true);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_paste_mode() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"paste_mode",
      L"When paste_mode is enabled in a buffer, it will be displayed in a way "
      L"that makes it possible to select (with a mouse) parts of it (that are "
      L"currently shown).  It will also allow you to paste text directly into "
      L"the buffer.",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_follow_end_of_file() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"follow_end_of_file",
      L"Should the cursor stay at the end of the file?",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_commands_background_mode() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"commands_background_mode",
      L"Should new commands forked from this buffer be started in background "
      L"mode?  If false, we will switch to them automatically.",
      false);
  return variable;
}

/* static */ EdgeVariable<char>* OpenBuffer::variable_reload_on_buffer_write() {
  static EdgeVariable<char>* variable = BoolStruct()->AddVariable(
      L"reload_on_buffer_write",
      L"Should the current buffer (on which this variable is set) be reloaded "
      L"when any buffer is written?  This is useful mainly for command buffers "
      L"like 'make' or 'git diff'.",
      false);
  return variable;
}

/* static */ EdgeStruct<wstring>* OpenBuffer::StringStruct() {
  static EdgeStruct<wstring>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<wstring>;
    // Trigger registration of all fields.
    OpenBuffer::variable_word_characters();
    OpenBuffer::variable_path_characters();
    OpenBuffer::variable_path();
    OpenBuffer::variable_pts_path();
    OpenBuffer::variable_command();
    OpenBuffer::variable_editor_commands_path();
    OpenBuffer::variable_line_prefix_characters();
    OpenBuffer::variable_line_suffix_superfluous_characters();
  }
  return output;
}

/* static */ EdgeVariable<wstring>* OpenBuffer::variable_word_characters() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"word_characters",
      L"String with all the characters that should be considered part of a "
      L"word.",
      L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_");
  return variable;
}

/* static */ EdgeVariable<wstring>* OpenBuffer::variable_path_characters() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"path_characters",
      L"String with all the characters that should be considered part of a "
      L"path.",
      L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.*:/");
  return variable;
}

/* static */ EdgeVariable<wstring>* OpenBuffer::variable_path() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"path",
      L"String with the path of the current file.",
      L"",
      FilePredictor);
  return variable;
}

/* static */ EdgeVariable<wstring>* OpenBuffer::variable_pts_path() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"pts_path",
      L"String with the path of the terminal used by the current buffer (or "
      L"empty if the user is not using a terminal).",
      L"",
      FilePredictor);
  return variable;
}

/* static */ EdgeVariable<wstring>* OpenBuffer::variable_command() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"command",
      L"String with the current command (or empty).",
      L"",
      FilePredictor);
  return variable;
}

/* static */ EdgeVariable<wstring>* OpenBuffer::variable_editor_commands_path() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"editor_commands_path",
      L"String with the path to the initial directory for editor commands.",
      L"",
      FilePredictor);
  return variable;
}

/* static */ EdgeStruct<int>* OpenBuffer::IntStruct() {
  static EdgeStruct<int>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<int>;
    // Trigger registration of all fields.
    OpenBuffer::variable_line_width();
  }
  return output;
}

/* static */ EdgeVariable<int>* OpenBuffer::variable_line_width() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"line_width",
      L"Desired maximum width of a line.",
      80);
  return variable;
}

/* static */ EdgeStruct<unique_ptr<Value>>* OpenBuffer::ValueStruct() {
  static EdgeStruct<unique_ptr<Value>>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<unique_ptr<Value>>;
    // Trigger registration of all fields.
    // ... except there are no fields yet.
  }
  return output;
}

/* static */ EdgeVariable<wstring>*
OpenBuffer::variable_line_prefix_characters() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"line_prefix_characters",
      L"String with all the characters that should be considered the prefix of "
      L"the actual contents of a line.  When a new line is created, the prefix "
      L"of the previous line (the sequence of all characters at the start of "
      L"the previous line that are listed in line_prefix_characters) is copied "
      L"to the new line.  The order of characters in line_prefix_characters has "
      L"no effect.",
      L" ");
  return variable;
}

/* static */ EdgeVariable<wstring>*
OpenBuffer::variable_line_suffix_superfluous_characters() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"line_suffix_superfluous_characters",
      L"String with all the characters that should be removed from the suffix "
      L"of a line (after editing it).  The order of characters in "
      L"line_suffix_superfluous_characters has no effect.",
      L" ");
  return variable;
}

bool OpenBuffer::read_bool_variable(const EdgeVariable<char>* variable) const {
  return static_cast<bool>(bool_variables_.Get(variable));
}

void OpenBuffer::set_bool_variable(
    const EdgeVariable<char>* variable, bool value) {
  bool_variables_.Set(variable, static_cast<char>(value));
}

void OpenBuffer::toggle_bool_variable(const EdgeVariable<char>* variable) {
  set_bool_variable(variable, !read_bool_variable(variable));
}

const wstring& OpenBuffer::read_string_variable(
    const EdgeVariable<wstring>* variable) const {
  return string_variables_.Get(variable);
}

void OpenBuffer::set_string_variable(
    const EdgeVariable<wstring>* variable, const wstring& value) {
  string_variables_.Set(variable, value);
}

const int& OpenBuffer::read_int_variable(
    const EdgeVariable<int>* variable) const {
  return int_variables_.Get(variable);
}

void OpenBuffer::set_int_variable(
    const EdgeVariable<int>* variable, const int& value) {
  int_variables_.Set(variable, value);
}

const Value* OpenBuffer::read_value_variable(
    const EdgeVariable<unique_ptr<Value>>* variable) const {
  return function_variables_.Get(variable);
}

void OpenBuffer::set_value_variable(
    const EdgeVariable<unique_ptr<Value>>* variable, unique_ptr<Value> value) {
  function_variables_.Set(variable, std::move(value));
}

void OpenBuffer::CopyVariablesFrom(const shared_ptr<const OpenBuffer>& src) {
  assert(src.get() != nullptr);
  bool_variables_.CopyFrom(src->bool_variables_);
  string_variables_.CopyFrom(src->string_variables_);
}

void OpenBuffer::Apply(
    EditorState* editor_state, unique_ptr<Transformation> transformation) {
  if (!last_transformation_stack_.empty()) {
    CHECK(last_transformation_stack_.back() != nullptr);
    last_transformation_stack_.back()->PushBack(transformation->Clone());
  }
  transformations_past_.emplace_back(new Transformation::Result(editor_state));
  transformation->Apply(editor_state, this, transformations_past_.back().get());

  auto delete_buffer = transformations_past_.back()->delete_buffer;
  if (!delete_buffer->contents()->empty()) {
    auto insert_result = editor_state->buffers()->insert(
        make_pair(delete_buffer->name(), delete_buffer));
    if (!insert_result.second) {
      insert_result.first->second = delete_buffer;
    }
  }

  transformations_future_.clear();
  if (transformations_past_.back()->modified_buffer) {
    last_transformation_ = std::move(transformation);
  }
}

void OpenBuffer::RepeatLastTransformation(EditorState* editor_state) {
  transformations_past_.emplace_back(new Transformation::Result(editor_state));
  last_transformation_
      ->Apply(editor_state, this, transformations_past_.back().get());
  transformations_future_.clear();
}

void OpenBuffer::PushTransformationStack() {
  last_transformation_stack_.emplace_back(new TransformationStack);
}

void OpenBuffer::PopTransformationStack() {
  CHECK(!last_transformation_stack_.empty());
  last_transformation_ = std::move(last_transformation_stack_.back());
  last_transformation_stack_.pop_back();
  if (!last_transformation_stack_.empty()) {
    last_transformation_stack_.back()->PushBack(last_transformation_->Clone());
  }
}

void OpenBuffer::Undo(EditorState* editor_state) {
  list<unique_ptr<Transformation::Result>>* source;
  list<unique_ptr<Transformation::Result>>* target;
  if (editor_state->direction() == FORWARDS) {
    source = &transformations_past_;
    target = &transformations_future_;
  } else {
    source = &transformations_future_;
    target = &transformations_past_;
  }
  for (size_t i = 0; i < editor_state->repetitions(); i++) {
    bool modified_buffer = false;
    while (!modified_buffer && !source->empty()) {
      target->emplace_back(new Transformation::Result(editor_state));
      source->back()->undo->Apply(editor_state, this, target->back().get());
      source->pop_back();
      modified_buffer = target->back()->modified_buffer;
    }
    if (source->empty()) { return; }
  }
}

void OpenBuffer::set_filter(unique_ptr<Value> filter) {
  filter_ = std::move(filter);
  filter_version_++;
}

bool OpenBuffer::IsLineFiltered(size_t line_number) {
  assert(line_number <= contents_.size());
  if (line_number == contents_.size()) {
    return true;
  }
  auto line = contents_[line_number];
  if (line->filter_version() < filter_version_) {
    vector<unique_ptr<Value>> args;
    args.push_back(Value::NewString(line->ToString()));
    bool filtered = filter_->callback(std::move(args))->boolean;
    line->set_filtered(filtered, filter_version_);
  }
  return line->filtered();
}

}  // namespace editor
}  // namespace afc

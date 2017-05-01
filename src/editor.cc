#include <fstream>
#include <iostream>
#include <memory>
#include <list>
#include <string>
#include <sstream>
#include <stdexcept>

extern "C" {
#include <sys/types.h>
#include <pwd.h>
#include <signal.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "char_buffer.h"
#include "dirname.h"
#include "editor.h"
#include "file_link_mode.h"
#include "run_command_handler.h"
#include "server.h"
#include "substring.h"
#include "transformation_delete.h"
#include "vm/public/environment.h"
#include "vm/public/value.h"
#include "wstring.h"

namespace afc {
namespace editor {

namespace {

using std::make_pair;
using std::string;
using std::stringstream;
using std::to_string;
using std::vector;
using std::wstring;

static wstring GetHomeDirectory() {
  char* env = getenv("HOME");
  if (env != nullptr) { return FromByteString(env); }
  struct passwd* entry = getpwuid(getuid());
  if (entry != nullptr) { return FromByteString(entry->pw_dir); }
  return L"/";  // What else?
}

static vector<wstring> GetEdgeConfigPath(const wstring& home) {
  vector<wstring> output;
  output.push_back(home + L"/.edge");
  LOG(INFO) << "Pushing config path: " << output[0];
  char* env = getenv("EDGE_PATH");
  if (env != nullptr) {
    std::istringstream text_stream(string(env) + ";");
    std::string dir;
    // TODO: stat it and don't add it if it doesn't exist.
    while (std::getline(text_stream, dir, ';')) {
      output.push_back(FromByteString(dir));
    }
  }
  return output;
}

void RegisterBufferMethod(ObjectType* editor_type, const wstring& name,
                          void (OpenBuffer::*method)(void)) {
  unique_ptr<Value> callback(new Value(VMType::FUNCTION));

  // Returns nothing.
  callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

  callback->type.type_arguments.push_back(VMType::ObjectType(editor_type));
  callback->callback =
      [method](vector<unique_ptr<Value>> args) {
        CHECK_EQ(args.size(), 1);
        CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);

        auto editor = static_cast<EditorState*>(args[0]->user_value.get());
        CHECK(editor != nullptr);

        if (!editor->has_current_buffer()) { return Value::NewVoid(); }
        auto buffer = editor->current_buffer()->second;
        CHECK(buffer != nullptr);

        (*buffer.*method)();
        editor->ResetModifiers();
        editor->ScheduleRedraw();
        return Value::NewVoid();
      };
  editor_type->AddField(name, std::move(callback));
}

}  // namespace

Environment EditorState::BuildEditorEnvironment() {
  Environment environment(afc::vm::Environment::GetDefault());

  unique_ptr<ObjectType> editor_type(new ObjectType(L"Editor"));

  // Methods for Editor.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(editor_type.get()));
    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);

          auto editor = static_cast<EditorState*>(args[0]->user_value.get());
          CHECK(editor != nullptr);

          if (!editor->has_current_buffer()) { return Value::NewVoid(); }
          auto buffer = editor->current_buffer()->second;
          CHECK(buffer != nullptr);

          if (editor->structure() == LINE) {
            auto target_buffer = buffer->GetBufferFromCurrentLine();
            if (target_buffer != nullptr) {
              buffer = target_buffer;
            }
          }
          buffer->Reload(editor);
          editor->ResetModifiers();
          return Value::NewVoid();
        };
    editor_type->AddField(L"ReloadCurrentBuffer", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(
        VMType::ObjectType(editor_type.get()));
    callback->callback =
        [](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);

          auto editor = static_cast<EditorState*>(args[0]->user_value.get());
          CHECK(editor != nullptr);

          if (!editor->has_current_buffer()) { return Value::NewVoid(); }
          auto buffer = editor->current_buffer()->second;
          CHECK(buffer != nullptr);

          if (editor->structure() == LINE) {
            auto target_buffer = buffer->GetBufferFromCurrentLine();
            if (target_buffer != nullptr) {
              buffer = target_buffer;
            }
          }
          buffer->Save(editor);
          editor->ResetModifiers();
          return Value::NewVoid();
        };
    editor_type->AddField(L"SaveCurrentBuffer", std::move(callback));
  }
  {
    // A callback to return the current buffer. This is needed so that at a time
    // when there's no current buffer (i.e. EditorState is being created) we can
    // still compile code that will depend (at run time) on getting the current
    // buffer. Otherwise we could just use the "buffer" variable (that is
    // declared in the environment of each buffer).
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType::ObjectType(L"Buffer"));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 0);
          auto buffer = current_buffer()->second;
          CHECK(buffer != nullptr);
          if (structure() == LINE) {
            auto target_buffer = buffer->GetBufferFromCurrentLine();
            ResetStructure();
            if (target_buffer != nullptr) {
              buffer = target_buffer;
            }
          }
          return Value::NewObject(L"Buffer", buffer);
        };
    environment.Define(L"CurrentBuffer", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));

    // Returns nothing.
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));

    callback->type.type_arguments.push_back(VMType::Integer());
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1);
          CHECK_EQ(args[0]->type, VMType::VM_INTEGER);
          DCHECK(mode() != nullptr);
          mode()->ProcessInput(args[0]->integer, this);
          return Value::NewVoid();
        };
    environment.Define(L"ProcessInput", std::move(callback));
  }
  {
    unique_ptr<Value> connect_to_function(new Value(VMType::FUNCTION));
    connect_to_function->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    connect_to_function->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    connect_to_function->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          OpenServerBuffer(this, args[0]->str);
          return std::move(Value::NewVoid());
        };
    environment.Define(L"ConnectTo", std::move(connect_to_function));
  }
  {
    unique_ptr<Value> set_status_function(new Value(VMType::FUNCTION));
    set_status_function->type.type_arguments.push_back(
        VMType(VMType::VM_VOID));
    set_status_function->type.type_arguments.push_back(
        VMType(VMType::VM_STRING));
    set_status_function->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          SetStatus(args[0]->str);
          return std::move(Value::NewVoid());
        };
    environment.Define(L"SetStatus", std::move(set_status_function));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          CHECK(args.empty());
          ScheduleRedraw();
          return std::move(Value::NewVoid());
        };
    environment.Define(L"ScheduleRedraw", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType(VMType::VM_BOOLEAN));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1);
          CHECK_EQ(args[0]->type, VMType::VM_BOOLEAN);
          set_screen_needs_hard_redraw(args[0]->boolean);
          return std::move(Value::NewVoid());
        };
    environment.Define(L"set_screen_needs_hard_redraw", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType(VMType::VM_BOOLEAN));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 1);
          CHECK_EQ(args[0]->type, VMType::VM_BOOLEAN);
          terminate_ = args[0]->boolean;
          return std::move(Value::NewVoid());
        };
    environment.Define(L"set_terminate", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          if (!has_current_buffer()) { return Value::NewVoid(); }
          auto buffer = current_buffer()->second;
          assert(args[0]->type == VMType::VM_INTEGER);
          buffer->set_position(
              LineColumn(buffer->position().line, args[0]->integer));
          return Value::NewVoid();
        };
    environment.Define(L"SetPositionColumn", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback =
        [this](vector<unique_ptr<Value>>) {
          if (!has_current_buffer()) { return Value::NewVoid(); }
          auto buffer = current_buffer()->second;
          unique_ptr<Value> output(new Value(VMType::VM_STRING));
          output->str = buffer->current_line()->ToString();
          return output;
        };
    environment.Define(L"Line", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          ForkCommandOptions options;
          options.command = args[0]->str;
          options.enter = false;
          ForkCommand(this, options);
          return std::move(Value::NewVoid());
        };
    environment.Define(L"ForkCommand", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType::ObjectType(L"Buffer"));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          OpenFileOptions options;
          options.editor_state = this;
          options.path = args[0]->str;
          set_current_buffer(OpenFile(options));
          ResetMode();
          ScheduleRedraw();
          return Value::NewObject(L"Buffer", current_buffer()->second);
        };
    environment.Define(L"OpenFile", std::move(callback));
  }

  RegisterBufferMethod(editor_type.get(), L"ToggleActiveCursors",
                       &OpenBuffer::ToggleActiveCursors);
  RegisterBufferMethod(editor_type.get(), L"PushActiveCursors",
                       &OpenBuffer::PushActiveCursors);
  RegisterBufferMethod(editor_type.get(), L"PopActiveCursors",
                       &OpenBuffer::PopActiveCursors);
  RegisterBufferMethod(editor_type.get(), L"SetActiveCursorsToMarks",
                       &OpenBuffer::SetActiveCursorsToMarks);
  RegisterBufferMethod(editor_type.get(), L"CreateCursor",
                       &OpenBuffer::CreateCursor);
  RegisterBufferMethod(editor_type.get(), L"DestroyCursor",
                       &OpenBuffer::DestroyCursor);
  RegisterBufferMethod(editor_type.get(), L"DestroyOtherCursors",
                       &OpenBuffer::DestroyOtherCursors);
  RegisterBufferMethod(editor_type.get(), L"RepeatLastTransformation",
                       &OpenBuffer::RepeatLastTransformation);
  environment.DefineType(L"Editor", std::move(editor_type));

  environment.Define(L"editor", Value::NewObject(
      L"Editor", shared_ptr<void>(this, [](void*){})));

  OpenBuffer::RegisterBufferType(this, &environment);
  return environment;
}

EditorState::EditorState()
    : current_buffer_(buffers_.end()),
      home_directory_(GetHomeDirectory()),
      edge_path_(GetEdgeConfigPath(home_directory_)),
      environment_(BuildEditorEnvironment()),
      default_mode_supplier_(NewCommandModeSupplier(this)),
      mode_(default_mode_supplier_()),
      visible_lines_(1),
      status_prompt_(false),
      status_(L"") {
  unique_ptr<ObjectType> line_column(new ObjectType(L"LineColumn"));

  // Methods for LineColumn.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(
        VMType::ObjectType(line_column.get()));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args.size() == 2);
          assert(args[0]->type == VMType::VM_INTEGER);
          assert(args[1]->type == VMType::VM_INTEGER);
          return Value::NewObject(L"LineColumn", shared_ptr<LineColumn>(
              new LineColumn(args[0]->integer, args[1]->integer)));
        };
    environment_.Define(L"LineColumn", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(
        VMType::ObjectType(line_column.get()));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto line_column =
              static_cast<LineColumn*>(args[0]->user_value.get());
          assert(line_column != nullptr);
          unique_ptr<Value> output(new Value(VMType::VM_INTEGER));
          output->integer = line_column->line;
          return output;
        };
    line_column->AddField(L"line", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(
        VMType::ObjectType(line_column.get()));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto line_column =
              static_cast<LineColumn*>(args[0]->user_value.get());
          assert(line_column != nullptr);
          unique_ptr<Value> output(new Value(VMType::VM_INTEGER));
          output->integer = line_column->column;
          return output;
        };
    line_column->AddField(L"column", std::move(callback));
  }
  environment_.DefineType(L"LineColumn", std::move(line_column));

}

EditorState::~EditorState() {
  // TODO: Replace this with a custom deleter in the shared_ptr.  Simplify
  // CloseBuffer accordingly.
  LOG(INFO) << "Closing buffers.";
  for (auto& buffer : buffers_) {
    buffer.second->Close(this);
  }
}

bool EditorState::CloseBuffer(
    map<wstring, shared_ptr<OpenBuffer>>::iterator buffer) {
  if (!buffer->second->PrepareToClose(this)) {
    SetStatus(L"Dirty buffers (“Sad” to ignore): " + buffer->first);
    return false;
  }
  ScheduleRedraw();
  if (current_buffer_ == buffer) {
    if (buffers_.size() == 1) {
      current_buffer_ = buffers_.end();
    } else {
      current_buffer_ = buffer == buffers_.begin() ? buffers_.end() : buffer;
      current_buffer_--;
    }

    if (current_buffer_ != buffers_.end()) {
      current_buffer_->second->Visit(this);
    }
  }

  buffer->second->Close(this);
  buffers_.erase(buffer);
  CHECK(current_buffer_ != buffers_.end());
  return true;
}

bool EditorState::AttemptTermination(wstring* error_description) {
  LOG(INFO) << "Checking buffers for termination.";
  vector<wstring> buffers_with_problems;
  for (auto& it : buffers_) {
    if (!it.second->PrepareToClose(this)) {
      buffers_with_problems.push_back(it.first);
    }
  }
  if (buffers_with_problems.empty()) {
    LOG(INFO) << "Terminating.";
    terminate_ = true;
    return true;
  }

  wstring error = L"Dirty buffers (“Saq” to ignore):";
  for (auto name : buffers_with_problems) {
    error += L" " + name;
  }
  LOG(INFO) << error;
  if (error_description != nullptr) {
    *error_description = std::move(error);
  }
  return false;
}

void EditorState::MoveBufferForwards(size_t times) {
  if (current_buffer_ == buffers_.end()) {
    if (buffers_.empty()) { return; }
    current_buffer_ = buffers_.begin();
  }
  times = times % buffers_.size();
  for (size_t i = 0; i < times; i++) {
    current_buffer_++;
    if (current_buffer_ == buffers_.end()) {
      current_buffer_ = buffers_.begin();
    }
  }
  current_buffer_->second->Visit(this);
  PushCurrentPosition();
}

void EditorState::MoveBufferBackwards(size_t times) {
  if (current_buffer_ == buffers_.end()) {
    if (buffers_.empty()) { return; }
    current_buffer_ = buffers_.end();
    current_buffer_--;
  }
  times = times % buffers_.size();
  for (size_t i = 0; i < times; i++) {
    if (current_buffer_ == buffers_.begin()) {
      current_buffer_ = buffers_.end();
    }
    current_buffer_--;
  }
  current_buffer_->second->Visit(this);
  PushCurrentPosition();
}

void EditorState::ScheduleRedraw() {
  std::unique_lock<std::mutex> lock(mutex_);
  screen_state_.needs_redraw = true;
}

EditorState::ScreenState EditorState::FlushScreenState() {
  std::unique_lock<std::mutex> lock(mutex_);
  ScreenState output = screen_state_;
  screen_state_ = ScreenState();
  return output;
}

// We will store the positions in a special buffer.  They will be sorted from
// old (top) to new (bottom), one per line.  Each line will be of the form:
//
//   line column buffer
//
// The current line position is set to one line after the line to be returned
// by a pop.  To insert a new position, we insert it right at the current line.

static wstring kPositionsBufferName = L"- positions";

void EditorState::PushCurrentPosition() {
  if (current_buffer_ != buffers_.end()) {
    PushPosition(current_buffer_->second->position());
  }
}

void EditorState::PushPosition(LineColumn position) {
  if (!has_current_buffer()
      || !current_buffer_->second->read_bool_variable(
              OpenBuffer::variable_push_positions_to_history())) {
    return;
  }
  auto buffer_it = buffers_.find(kPositionsBufferName);
  if (buffer_it == buffers_.end()) {
    // Insert a new entry into the list of buffers.
    OpenFileOptions options;
    options.editor_state = this;
    options.name = kPositionsBufferName;
    options.path = PathJoin(*edge_path().begin(), L"positions");
    options.make_current_buffer = false;
    buffer_it = OpenFile(options);
    CHECK(buffer_it != buffers()->end());
    CHECK(buffer_it->second != nullptr);
    buffer_it->second->set_bool_variable(
        OpenBuffer::variable_save_on_close(), true);
    buffer_it->second->set_bool_variable(
        OpenBuffer::variable_show_in_buffers_list(), false);
  }
  CHECK(buffer_it->second != nullptr);
  CHECK_LE(buffer_it->second->position().line,
           buffer_it->second->contents()->size());
  buffer_it->second->InsertLine(
      buffer_it->second->current_position_line(),
      std::make_shared<Line>(
          position.ToString() + L" " + current_buffer_->first));
  CHECK_LE(buffer_it->second->position().line,
           buffer_it->second->contents()->size());
  if (buffer_it == current_buffer_) {
    ScheduleRedraw();
  }
}

static BufferPosition PositionFromLine(const wstring& line) {
  std::wstringstream line_stream(line);
  BufferPosition pos;
  line_stream >> pos.position.line >> pos.position.column;
  line_stream.get();
  getline(line_stream, pos.buffer_name);
  return pos;
}

void EditorState::SetStatus(const wstring& status) {
  LOG(INFO) << "SetStatus: " << status;
  status_ = status;
  is_status_warning_ = false;
  if (status_prompt_ || status.empty()) { return; }
  auto status_buffer_it = buffers_.insert(make_pair(L"- console", nullptr));
  if (status_buffer_it.second) {
    // Inserted the entry.
    status_buffer_it.first->second = shared_ptr<OpenBuffer>(
        new OpenBuffer(this, status_buffer_it.first->first));
    status_buffer_it.first->second->set_bool_variable(
        OpenBuffer::variable_allow_dirty_delete(), true);
    status_buffer_it.first->second->set_bool_variable(
        OpenBuffer::variable_show_in_buffers_list(), false);
  }
  status_buffer_it.first->second
      ->AppendLazyString(this, NewCopyString(status));
  if (current_buffer_ == status_buffer_it.first) {
    ScheduleRedraw();
  }
}

void EditorState::SetWarningStatus(const wstring& status) {
  SetStatus(status);
  is_status_warning_ = true;
}

bool EditorState::HasPositionsInStack() {
  auto it = buffers_.find(kPositionsBufferName);
  return it != buffers_.end() && it->second->contents()->size() > 1;
}

BufferPosition EditorState::ReadPositionsStack() {
  assert(HasPositionsInStack());
  auto buffer = buffers_.find(kPositionsBufferName)->second;
  return PositionFromLine(buffer->current_line()->ToString());
}

bool EditorState::MovePositionsStack(Direction direction) {
  // The directions here are somewhat counterintuitive: FORWARDS means the user
  // is actually going "back" in the history, which means we have to decrement
  // the line counter.
  assert(HasPositionsInStack());
  auto buffer = buffers_.find(kPositionsBufferName)->second;
  if (direction == BACKWARDS) {
    if (buffer->current_position_line() + 1 >= buffer->contents()->size()) {
      return false;
    }
    buffer->set_current_position_line(buffer->current_position_line() + 1);
    return true;
  }

  if (buffer->current_position_line() == 0) {
    return false;
  }
  buffer->set_current_position_line(buffer->current_position_line() - 1);
  return true;
}

void EditorState::ApplyToCurrentBuffer(
    unique_ptr<Transformation> transformation) {
  CHECK(transformation != nullptr);
  assert(has_current_buffer());
  current_buffer_->second->ApplyToCursors(std::move(transformation));
}

void EditorState::DefaultErrorHandler(const wstring& error_description) {
  SetStatus(L"Error: " + error_description);
}

wstring EditorState::expand_path(const wstring& path) {
  // TODO: Also support ~user/foo.
  if (path == L"~" || (path.size() > 2 && path.substr(0, 2) == L"~/")) {
    return home_directory() + path.substr(1);
  }
  return path;
}

void EditorState::ProcessSignals() {
  if (pending_signals_.empty()) { return; }
  vector<int> signals;
  signals.swap(pending_signals_);
  for (int signal : signals) {
    switch (signal) {
      case SIGINT:
      case SIGTSTP:
        if (!has_current_buffer()) {
          return;
        }
        auto buffer = current_buffer()->second;
        CHECK(buffer != nullptr);
        auto target_buffer = buffer->GetBufferFromCurrentLine();
        if (target_buffer != nullptr) {
          buffer = target_buffer;
        }
        buffer->PushSignal(this, signal);
    }
  }
}

bool EditorState::handling_stop_signals() const {
  if (!has_current_buffer()) {
    return false;
  }
  auto buffer = current_buffer()->second;
  CHECK(buffer != nullptr);
  auto target_buffer = buffer->GetBufferFromCurrentLine();
  if (target_buffer != nullptr) {
    buffer = target_buffer;
  }
  return buffer->read_bool_variable(OpenBuffer::variable_pts());
}

}  // namespace editor
}  // namespace afc

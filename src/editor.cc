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
#include "editor.h"
#include "file_link_mode.h"
#include "run_command_handler.h"
#include "server.h"
#include "substring.h"
#include "transformation_delete.h"
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

}  // namespace

EditorState::EditorState()
    : current_buffer_(buffers_.end()),
      terminate_(false),
      mode_(std::move(NewCommandMode())),
      visible_lines_(1),
      screen_needs_redraw_(false),
      screen_needs_hard_redraw_(false),
      status_prompt_(false),
      status_(L""),
      home_directory_(GetHomeDirectory()),
      edge_path_(GetEdgeConfigPath(home_directory_)),
      environment_(Environment::DefaultEnvironment()) {
  OpenBuffer::RegisterBufferType(this, &environment_);
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


  // Other functions.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType::ObjectType(L"Buffer"));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args.size() == 0);
          return Value::NewObject(L"Buffer", current_buffer()->second);
        };
    environment_.Define(L"CurrentBuffer", std::move(callback));
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
    environment_.Define(L"ConnectTo", std::move(connect_to_function));
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
    environment_.Define(L"SetStatus", std::move(set_status_function));
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
    environment_.Define(L"SetPositionColumn", std::move(callback));
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
    environment_.Define(L"Line", std::move(callback));
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
    environment_.Define(L"ForkCommand", std::move(callback));
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
    environment_.Define(L"OpenFile", std::move(callback));
  }
  environment_.DefineType(L"LineColumn", std::move(line_column));
}

EditorState::~EditorState() {
  // TODO: Replace this with a custom deleter in the shared_ptr.  Simplify
  // CloseBuffer accordingly.
  for (auto& buffer : buffers_) {
    buffer.second->Close(this);
  }
}

bool EditorState::CloseBuffer(
    map<wstring, shared_ptr<OpenBuffer>>::iterator buffer) {
  if (!buffer->second->PrepareToClose(this)) {
    SetStatus(L"Unable to close buffer: " + buffer->first);
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
      current_buffer_->second->Enter(this);
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

  wstring error = L"Unable to close buffers:";
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
  current_buffer_->second->Enter(this);
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
  current_buffer_->second->Enter(this);
  PushCurrentPosition();
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
  if (!has_current_buffer()) { return; }
  auto it = buffers_.find(kPositionsBufferName);
  if (it == buffers_.end()) {
    it = buffers_.insert(
        make_pair(kPositionsBufferName,
                  shared_ptr<OpenBuffer>(
                      new OpenBuffer(this, kPositionsBufferName))))
        .first;
  }
  assert(it->second != nullptr);
  assert(it->second->position().line <= it->second->contents()->size());
  shared_ptr<Line> line(new Line(Line::Options(
      NewCopyString(
          current_buffer_->second->position().ToString()
          + L" " + current_buffer_->first))));
  it->second->contents()->insert(
      it->second->contents()->begin() + it->second->current_position_line(),
      line);
  it->second->set_current_position_line(
      it->second->current_position_line() + 1);
  assert(it->second->position().line <= it->second->contents()->size());
  if (it == current_buffer_) {
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
  if (status_prompt_ || status.empty()) { return; }
  auto status_buffer_it = buffers_.insert(make_pair(L"- console", nullptr));
  if (status_buffer_it.second) {
    // Inserted the entry.
    status_buffer_it.first->second = shared_ptr<OpenBuffer>(
        new OpenBuffer(this, status_buffer_it.first->first));
  }
  status_buffer_it.first->second
      ->AppendLazyString(this, NewCopyString(status));
  if (current_buffer_ == status_buffer_it.first) {
    ScheduleRedraw();
  }
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
  current_buffer_->second->Apply(this, std::move(transformation));
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
        if (has_current_buffer()) {
          current_buffer()->second->PushSignal(this, signal);
        }
    }
  }
}

}  // namespace editor
}  // namespace afc

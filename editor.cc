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
#include <unistd.h>
}

#include "char_buffer.h"
#include "editor.h"
#include "file_link_mode.h"
#include "server.h"
#include "substring.h"

namespace {

using std::make_pair;
using std::string;
using std::stringstream;
using std::to_string;
using std::vector;

static string GetHomeDirectory() {
  char* env = getenv("HOME");
  if (env != nullptr) { return env; }
  struct passwd* entry = getpwuid(getuid());
  if (entry != nullptr) { return entry->pw_dir; }
  return "/";  // What else?
}

static vector<string> GetEdgeConfigPath(const string& home) {
  vector<string> output;
  char* env = getenv("EDGE_PATH");
  if (env != nullptr) {
    // TODO: Handle multiple directories separated with colons.
    // TODO: stat it and don't add it if it doesn't exist.
    output.push_back(env);
  }
  // TODO: Don't add it if it doesn't exist or it's already there.
  output.push_back(home + "/.edge");
  return output;
}

}  // namespace

namespace afc {
namespace editor {

EditorState::EditorState()
    : current_buffer_(buffers_.end()),
      terminate_(false),
      direction_(FORWARDS),
      default_direction_(FORWARDS),
      repetitions_(1),
      structure_(CHAR),
      sticky_structure_(false),
      mode_(std::move(NewCommandMode())),
      visible_lines_(1),
      screen_needs_redraw_(false),
      status_prompt_(false),
      status_(""),
      home_directory_(GetHomeDirectory()),
      edge_path_(GetEdgeConfigPath(home_directory_)),
      environment_(afc::vm::Environment::DefaultEnvironment()) {
  using namespace afc::vm;
  unique_ptr<ObjectType> line_column(new ObjectType("LineColumn"));
  unique_ptr<ObjectType> buffer(new ObjectType("Buffer"));

  // Methods for LineColumn.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType::ObjectType(line_column.get()));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args.size() == 2);
          assert(args[0]->type == VMType::VM_INTEGER);
          assert(args[1]->type == VMType::VM_INTEGER);
          return Value::NewObject("LineColumn", shared_ptr<LineColumn>(
              new LineColumn(args[0]->integer, args[1]->integer)));
        };
    environment_.Define("LineColumn", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType::ObjectType(line_column.get()));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto line_column = static_cast<LineColumn*>(args[0]->user_value.get());
          assert(line_column != nullptr);
          unique_ptr<Value> output(new Value(VMType::VM_INTEGER));
          output->integer = line_column->line;
          return output;
        };
    line_column->AddField("line", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType::ObjectType(line_column.get()));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto line_column = static_cast<LineColumn*>(args[0]->user_value.get());
          assert(line_column != nullptr);
          unique_ptr<Value> output(new Value(VMType::VM_INTEGER));
          output->integer = line_column->column;
          return output;
        };
    line_column->AddField("column", std::move(callback));
  }

  // Methods for Buffer
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          return Value::NewString(
              buffer->read_string_variable(OpenBuffer::variable_path()));
        };
    buffer->AddField("path", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::OBJECT_TYPE);
          assert(args[1]->type == VMType::VM_STRING);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          assert(buffer != nullptr);
          buffer->set_string_variable(
              OpenBuffer::variable_editor_commands_path(),
              args[1]->str);
          return std::move(Value::Void());
        };
    buffer->AddField("set_editor_commands_path", std::move(callback));
  }

  // Other functions.
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType::ObjectType("Buffer"));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args.size() == 0);
          return Value::NewObject("Buffer", current_buffer()->second);
        };
    environment_.Define("CurrentBuffer", std::move(callback));
  }
  {
    unique_ptr<Value> open_buffer_function(new Value(VMType::FUNCTION));
    open_buffer_function->type.type_arguments.push_back(VMType::ObjectType("Buffer"));
    open_buffer_function->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    open_buffer_function->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          string path = args[0]->str;
          set_current_buffer(OpenFile(this, path, path));
          return Value::NewObject("Buffer", current_buffer()->second);
        };
    environment_.Define("OpenBuffer", std::move(open_buffer_function));
  }

  {
    unique_ptr<Value> connect_to_function(new Value(VMType::FUNCTION));
    connect_to_function->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    connect_to_function->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    connect_to_function->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          OpenServerBuffer(this, args[0]->str);
          return std::move(Value::Void());
        };
    environment_.Define("ConnectTo", std::move(connect_to_function));
  }

  {
    unique_ptr<Value> set_status_function(new Value(VMType::FUNCTION));
    set_status_function->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    set_status_function->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    set_status_function->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          SetStatus(args[0]->str);
          return std::move(Value::Void());
        };
    environment_.Define("SetStatus", std::move(set_status_function));
  }

  {
    unique_ptr<Value> insert_text(new Value(VMType::FUNCTION));
    insert_text->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    insert_text->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    insert_text->callback =
        [this](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          if (!has_current_buffer()) { return Value::Void(); }
          auto buffer = current_buffer()->second;

          shared_ptr<OpenBuffer> buffer_to_insert(
              new OpenBuffer(this, "tmp buffer"));

          // getline will silently eat the last (empty) line.
          std::istringstream text_stream(args[0]->str + "\n");
          std::string line;
          while (std::getline(text_stream, line, '\n')) {
            buffer_to_insert->AppendLine(this, NewCopyString(line));
          }

          // Skip the last (empty) line.
          buffer_to_insert->contents()->pop_back();
          unique_ptr<Transformation> transformation(
              NewInsertBufferTransformation(
                  buffer_to_insert, buffer->position(), 1));
          buffer->Apply(this, *transformation);
          return Value::Void();
        };
    environment_.Define("InsertText", std::move(insert_text));
  }

  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType::ObjectType(line_column.get()));
    callback->type.type_arguments.push_back(VMType::ObjectType(line_column.get()));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          if (!has_current_buffer()) { return Value::Void(); }
          auto buffer = current_buffer()->second;
          LineColumn* start = static_cast<LineColumn*>(args[0]->user_value.get());
          LineColumn* end = static_cast<LineColumn*>(args[1]->user_value.get());
          unique_ptr<Transformation> transformation(
              NewDeleteTransformation(*start, *end));
          buffer->Apply(this, *transformation);
          return Value::Void();
        };
    environment_.Define("DeleteText", std::move(callback));
  }

  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          if (!has_current_buffer()) { return Value::Void(); }
          auto buffer = current_buffer()->second;
          assert(args[0]->type == VMType::VM_INTEGER);
          buffer->set_position(
              LineColumn(buffer->position().line, args[0]->integer));
          return Value::Void();
        };
    environment_.Define("SetPositionColumn", std::move(callback));
  }

  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
    callback->type.type_arguments.push_back(VMType::ObjectType(line_column.get()));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          if (!has_current_buffer()) { return Value::Void(); }
          auto buffer = current_buffer()->second;
          assert(args[0]->type == VMType::OBJECT_TYPE);
          assert(args[0]->user_value != nullptr);
          buffer->set_position(
              *static_cast<LineColumn*>(args[0]->user_value.get()));
          return Value::Void();
        };
    environment_.Define("SetPosition", std::move(callback));
  }

  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType::ObjectType(line_column.get()));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          if (!has_current_buffer()) { return Value::Void(); }
          auto buffer = current_buffer()->second;
          return Value::NewObject("LineColumn", shared_ptr<LineColumn>(
              new LineColumn(buffer->position())));
        };
    environment_.Define("Position", std::move(callback));
  }

  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback =
        [this](vector<unique_ptr<Value>> args) {
          if (!has_current_buffer()) { return Value::Void(); }
          auto buffer = current_buffer()->second;
          unique_ptr<Value> output(new Value(VMType::VM_STRING));
          output->str = buffer->current_line()->contents->ToString();
          return output;
        };
    environment_.Define("Line", std::move(callback));
  }

  environment_.DefineType("LineColumn", std::move(line_column));
  environment_.DefineType("Buffer", std::move(buffer));
}

EditorState::~EditorState() {
  // TODO: Replace this with a custom deleter in the shared_ptr.  Simplify
  // CloseBuffer accordingly.
  for (auto& buffer : buffers_) {
    buffer.second->Close(this);
  }
}

void EditorState::CloseBuffer(
    map<string, shared_ptr<OpenBuffer>>::iterator buffer) {
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
  assert(current_buffer_ != buffers_.end());
}

void EditorState::set_direction(Direction direction) {
  direction_ = direction;
}

void EditorState::set_default_direction(Direction direction) {
  default_direction_ = direction;
  ResetDirection();
}

void EditorState::set_structure(Structure structure) {
  structure_ = structure;
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

static const char* kPositionsBufferName = "- positions";

void EditorState::PushCurrentPosition() {
  if (!has_current_buffer()) { return; }
  auto it = buffers_.find(kPositionsBufferName);
  if (it == buffers_.end()) {
    it = buffers_.insert(
        make_pair(kPositionsBufferName,
                  new OpenBuffer(this, kPositionsBufferName)))
        .first;
  }
  assert(it->second != nullptr);
  assert(!it->second->contents()->empty());
  assert(it->second->position().line < it->second->contents()->size());
  shared_ptr<Line> line(new Line());
  line->contents = NewCopyString(
      current_buffer_->second->position().ToString()
      + " " + current_buffer_->first);
  it->second->contents()->insert(
      it->second->contents()->begin()
      + it->second->current_position_line(),
      line);
  it->second->set_current_position_line(
      it->second->current_position_line() + 1);
  assert(it->second->position().line < it->second->contents()->size());
  if (it == current_buffer_) {
    ScheduleRedraw();
  }
}

static BufferPosition PositionFromLine(const string& line) {
  stringstream line_stream(line);
  BufferPosition pos;
  line_stream >> pos.position.line >> pos.position.column;
  line_stream.get();
  getline(line_stream, pos.buffer);
  return pos;
}

bool EditorState::HasPositionsInStack() {
  auto it = buffers_.find(kPositionsBufferName);
  return it != buffers_.end() && it->second->contents()->size() > 1;
}

BufferPosition EditorState::ReadPositionsStack() {
  assert(HasPositionsInStack());
  auto buffer = buffers_.find(kPositionsBufferName)->second;
  return PositionFromLine(buffer->current_line()->contents->ToString());
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

void EditorState::ApplyToCurrentBuffer(const Transformation& transformation) {
  assert(has_current_buffer());
  current_buffer_->second->Apply(this, transformation);
}

void EditorState::Evaluate(const string& str) {
  using namespace afc::vm;
  Evaluator evaluator(unique_ptr<Environment>(new Environment(environment_)));
  evaluator.AppendInput(str);
}

void EditorState::EvaluateFile(const string& path,
                               afc::vm::Environment* environment) {
  using namespace afc::vm;
  Evaluator evaluator(unique_ptr<Environment>(new Environment(environment)));
  std::ifstream infile(path);
  std::string line;
  while (std::getline(infile, line)) {
    evaluator.AppendInput(line);
  }
}

string EditorState::expand_path(const string& path) {
  // TODO: Also support ~user/foo.
  if (path == "~" || (path.size() > 2 && path.substr(0, 2) == "~/")) {
    return home_directory() + path.substr(1);
  }
  return path;
}

}  // namespace editor
}  // namespace afc

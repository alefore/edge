#include "editor.h"

#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

extern "C" {
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "audio.h"
#include "char_buffer.h"
#include "dirname.h"
#include "file_link_mode.h"
#include "run_command_handler.h"
#include "server.h"
#include "src/buffer_variables.h"
#include "substring.h"
#include "transformation_delete.h"
#include "vm/public/callbacks.h"
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
  if (env != nullptr) {
    return FromByteString(env);
  }
  struct passwd* entry = getpwuid(getuid());
  if (entry != nullptr) {
    return FromByteString(entry->pw_dir);
  }
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
  auto callback = std::make_unique<Value>(VMType::FUNCTION);
  // Returns nothing.
  callback->type.type_arguments = {VMType(VMType::VM_VOID),
                                   VMType::ObjectType(editor_type)};
  callback->callback = [method](vector<unique_ptr<Value>> args,
                                Trampoline* trampoline) {
    CHECK_EQ(args.size(), size_t(1));
    CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);

    auto editor = static_cast<EditorState*>(args[0]->user_value.get());
    CHECK(editor != nullptr);

    if (editor->has_current_buffer()) {
      auto buffer = editor->current_buffer()->second;
      CHECK(buffer != nullptr);

      (*buffer.*method)();
      editor->ResetModifiers();
      editor->ScheduleRedraw();
    }
    trampoline->Return(Value::NewVoid());
  };
  editor_type->AddField(name, std::move(callback));
}
}  // namespace

void EditorState::NotifyInternalEvent() {
  VLOG(5) << "Internal event notification!";
  if (write(pipe_to_communicate_internal_events_.second, " ", 1) == -1) {
    SetStatus(L"Write to internal pipe failed!");
  }
}

Environment EditorState::BuildEditorEnvironment() {
  Environment environment(afc::vm::Environment::GetDefault());

  auto editor_type = std::make_unique<ObjectType>(L"Editor");

  // Methods for Editor.
  editor_type->AddField(
      L"ReloadCurrentBuffer",
      Value::NewFunction(
          {VMType(VMType::VM_VOID), VMType::ObjectType(editor_type.get())},
          [](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), size_t(1));
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);

            auto editor = static_cast<EditorState*>(args[0]->user_value.get());
            CHECK(editor != nullptr);

            if (!editor->has_current_buffer()) {
              return Value::NewVoid();
            }
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
          }));

  editor_type->AddField(
      L"SaveCurrentBuffer",
      Value::NewFunction(
          {VMType(VMType::VM_VOID), VMType::ObjectType(editor_type.get())},
          [](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), size_t(1));
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);

            auto editor = static_cast<EditorState*>(args[0]->user_value.get());
            CHECK(editor != nullptr);

            if (!editor->has_current_buffer()) {
              return Value::NewVoid();
            }
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
          }));

  // A callback to return the current buffer. This is needed so that at a time
  // when there's no current buffer (i.e. EditorState is being created) we can
  // still compile code that will depend (at run time) on getting the current
  // buffer. Otherwise we could just use the "buffer" variable (that is declared
  // in the environment of each buffer).
  environment.Define(
      L"CurrentBuffer",
      Value::NewFunction({VMType::ObjectType(L"Buffer")},
                         [this](vector<unique_ptr<Value>> args) {
                           CHECK_EQ(args.size(), size_t(0));
                           auto buffer = current_buffer()->second;
                           CHECK(buffer != nullptr);
                           if (structure() == LINE) {
                             auto target_buffer =
                                 buffer->GetBufferFromCurrentLine();
                             ResetStructure();
                             if (target_buffer != nullptr) {
                               buffer = target_buffer;
                             }
                           }
                           return Value::NewObject(L"Buffer", buffer);
                         }));

  environment.Define(L"ProcessInput", vm::NewCallback(std::function<void(int)>(
                                          [this](int c) { ProcessInput(c); })));

  environment.Define(
      L"ConnectTo",
      vm::NewCallback(std::function<void(wstring)>(
          [this](wstring target) { OpenServerBuffer(this, target); })));

  environment.Define(L"SetStatus", vm::NewCallback(std::function<void(wstring)>(
                                       [this](wstring s) { SetStatus(s); })));

  environment.Define(
      L"ScheduleRedraw",
      vm::NewCallback(std::function<void()>([this]() { ScheduleRedraw(); })));

  environment.Define(
      L"set_screen_needs_hard_redraw",
      vm::NewCallback(std::function<void(bool)>(
          [this](bool value) { set_screen_needs_hard_redraw(value); })));

  environment.Define(L"set_terminate",
                     vm::NewCallback(std::function<void(bool)>(
                         [this](bool value) { terminate_ = value; })));

  environment.Define(
      L"SetPositionColumn",
      vm::NewCallback(std::function<void(int)>([this](int value) {
        if (!has_current_buffer()) {
          return;
        }
        auto buffer = current_buffer()->second;
        buffer->set_position(LineColumn(buffer->position().line, value));
      })));

  environment.Define(
      L"Line",
      vm::NewCallback(std::function<wstring(void)>([this]() -> wstring {
        if (!has_current_buffer()) {
          return L"";
        }
        auto buffer = current_buffer()->second;
        return buffer->current_line()->ToString();
      })));

  environment.Define(
      L"ForkCommand",
      Value::NewFunction({VMType::ObjectType(L"Buffer"), VMType::VM_STRING,
                          VMType::VM_BOOLEAN},
                         [this](vector<Value::Ptr> args) {
                           CHECK_EQ(args.size(), 2u);
                           CHECK_EQ(args[0]->type, VMType::VM_STRING);
                           ForkCommandOptions options;
                           options.command = args[0]->str;
                           options.enter = args[1]->boolean;
                           return Value::NewObject(L"Buffer",
                                                   ForkCommand(this, options));
                         }));

  environment.Define(
      L"OpenFile",
      Value::NewFunction(
          {VMType::ObjectType(L"Buffer"), VMType(VMType::VM_STRING)},
          [this](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), size_t(1));
            CHECK_EQ(args[0]->type, VMType::VM_STRING);
            OpenFileOptions options;
            options.editor_state = this;
            options.path = args[0]->str;
            set_current_buffer(OpenFile(options));
            ScheduleRedraw();
            return Value::NewObject(L"Buffer", current_buffer()->second);
          }));

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

  environment.Define(
      L"editor",
      Value::NewObject(L"Editor", shared_ptr<void>(this, [](void*) {})));

  OpenBuffer::RegisterBufferType(this, &environment);
  return environment;
}

std::pair<int, int> BuildPipe() {
  int output[2];
  if (pipe2(output, O_NONBLOCK) == -1) {
    return {-1, -1};
  }
  return {output[0], output[1]};
}

EditorState::EditorState(AudioPlayer* audio_player)
    : current_buffer_(buffers_.end()),
      home_directory_(GetHomeDirectory()),
      edge_path_(GetEdgeConfigPath(home_directory_)),
      environment_(BuildEditorEnvironment()),
      default_commands_(NewCommandMode(this)),
      visible_lines_(1),
      status_prompt_(false),
      status_(L""),
      pipe_to_communicate_internal_events_(BuildPipe()),
      audio_player_(audio_player) {
  LineColumn::Register(&environment_);
  Range::Register(&environment_);
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
    SetWarningStatus(L"Dirty buffers (“Sad” to ignore): " + buffer->first);
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
  return true;
}

wstring GetBufferName(const wstring& prefix, size_t count) {
  return prefix + L" " + std::to_wstring(count);
}

wstring EditorState::GetUnusedBufferName(const wstring& prefix) {
  size_t count = 0;
  while (buffers()->find(GetBufferName(prefix, count)) != buffers()->end()) {
    count++;
  }
  return GetBufferName(prefix, count);
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

void EditorState::ProcessInput(int c) {
  EditorMode* handler = keyboard_redirect().get();
  if (handler != nullptr) {
    // Pass.
  } else if (has_current_buffer()) {
    handler = current_buffer()->second->mode();
  } else {
    handler = OpenAnonymousBuffer(this)->second->mode();
    CHECK(has_current_buffer());
  }
  handler->ProcessInput(c, this);
}

void EditorState::UpdateBuffers() {
  for (OpenBuffer* buffer : buffers_to_parse_) {
    buffer->ResetParseTree();
  }
  buffers_to_parse_.clear();
}

void EditorState::MoveBufferForwards(size_t times) {
  if (current_buffer_ == buffers_.end()) {
    if (buffers_.empty()) {
      return;
    }
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
    if (buffers_.empty()) {
      return;
    }
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
  if (!has_current_buffer() ||
      !current_buffer_->second->read_bool_variable(
          buffer_variables::push_positions_to_history())) {
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
    buffer_it->second->set_bool_variable(buffer_variables::save_on_close(),
                                         true);
    buffer_it->second->set_bool_variable(
        buffer_variables::show_in_buffers_list(), false);
  }
  CHECK(buffer_it->second != nullptr);
  CHECK_LE(buffer_it->second->position().line,
           buffer_it->second->contents()->size());
  buffer_it->second->InsertLine(
      buffer_it->second->current_position_line(),
      std::make_shared<Line>(position.ToString() + L" " +
                             current_buffer_->first));
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
  if (status_prompt_ || status.empty()) {
    return;
  }
  auto status_buffer_it = buffers_.insert(make_pair(L"- console", nullptr));
  if (status_buffer_it.second) {
    // Inserted the entry.
    status_buffer_it.first->second =
        std::make_shared<OpenBuffer>(this, status_buffer_it.first->first);
    status_buffer_it.first->second->set_bool_variable(
        buffer_variables::allow_dirty_delete(), true);
    status_buffer_it.first->second->set_bool_variable(
        buffer_variables::show_in_buffers_list(), false);
  }
  status_buffer_it.first->second->AppendLazyString(this, NewCopyString(status));
  if (current_buffer_ == status_buffer_it.first) {
    ScheduleRedraw();
  }
}

void EditorState::SetWarningStatus(const wstring& status) {
  SetStatus(status);
  is_status_warning_ = true;
  GenerateAlert(audio_player_);
}

bool EditorState::HasPositionsInStack() {
  auto it = buffers_.find(kPositionsBufferName);
  return it != buffers_.end() && it->second->contents()->size() > 1;
}

BufferPosition EditorState::ReadPositionsStack() {
  CHECK(HasPositionsInStack());
  auto buffer = buffers_.find(kPositionsBufferName)->second;
  return PositionFromLine(buffer->current_line()->ToString());
}

bool EditorState::MovePositionsStack(Direction direction) {
  // The directions here are somewhat counterintuitive: FORWARDS means the user
  // is actually going "back" in the history, which means we have to decrement
  // the line counter.
  CHECK(HasPositionsInStack());
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
  CHECK(has_current_buffer());
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
  if (pending_signals_.empty()) {
    return;
  }
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
  return buffer->read_bool_variable(buffer_variables::pts());
}

}  // namespace editor
}  // namespace afc

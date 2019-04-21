#include "src/editor.h"

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

#include "src/audio.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/dirname.h"
#include "src/file_link_mode.h"
#include "src/run_command_handler.h"
#include "src/server.h"
#include "src/shapes.h"
#include "src/substring.h"
#include "src/transformation_delete.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm_transformation.h"
#include "src/wstring.h"

namespace afc {
namespace vm {
template <>
struct VMTypeMapper<editor::EditorState*> {
  static editor::EditorState* get(Value* value) {
    return static_cast<editor::EditorState*>(value->user_value.get());
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::EditorState*>::vmtype =
    VMType::ObjectType(L"Editor");
}  // namespace vm
namespace editor {
namespace {

using std::make_pair;
using std::string;
using std::stringstream;
using std::to_string;
using std::vector;
using std::wstring;

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

    auto buffer = editor->current_buffer();
    if (buffer != nullptr) {
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

void EditorState::SchedulePendingWork(std::function<void()> callback) {
  pending_work_.push_back(callback);
}

EditorState::PendingWorkState EditorState::ExecutePendingWork() {
  VLOG(5) << "Executing pending work: " << pending_work_.size();
  std::vector<std::function<void()>> callbacks;
  callbacks.swap(pending_work_);
  for (auto& c : callbacks) {
    c();
  }
  return pending_work_.empty() ? PendingWorkState::kIdle
                               : PendingWorkState::kScheduled;
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

            auto buffer = editor->current_buffer();
            if (buffer == nullptr) {
              return Value::NewVoid();
            }

            if (editor->structure() == StructureLine()) {
              auto target_buffer = buffer->GetBufferFromCurrentLine();
              if (target_buffer != nullptr) {
                buffer = target_buffer;
              }
            }
            buffer->Reload();
            editor->ResetModifiers();
            return Value::NewVoid();
          }));

  editor_type->AddField(
      L"AddHorizontalSplit",
      vm::NewCallback(std::function<void(EditorState*)>(
          [](EditorState* editor) { editor->AddHorizontalSplit(); })));

  editor_type->AddField(L"SetHorizontalSplitsWithAllBuffers",
                        vm::NewCallback(std::function<void(EditorState*)>(
                            [](EditorState* editor) {
                              editor->SetHorizontalSplitsWithAllBuffers();
                            })));

  editor_type->AddField(L"SetActiveLeaf",
                        vm::NewCallback(std::function<void(EditorState*, int)>(
                            [](EditorState* editor, int delta) {
                              editor->SetActiveLeaf(delta);
                            })));

  editor_type->AddField(L"AdvanceActiveLeaf",
                        vm::NewCallback(std::function<void(EditorState*, int)>(
                            [](EditorState* editor, int delta) {
                              editor->AdvanceActiveLeaf(delta);
                            })));

  editor_type->AddField(
      L"ZoomToLeaf", vm::NewCallback(std::function<void(EditorState*)>(
                         [](EditorState* editor) { editor->ZoomToLeaf(); })));

  editor_type->AddField(
      L"RemoveActiveLeaf",
      vm::NewCallback(std::function<void(EditorState*)>(
          [](EditorState* editor) { editor->BufferTreeRemoveActiveLeaf(); })));

  editor_type->AddField(
      L"SaveCurrentBuffer",
      Value::NewFunction(
          {VMType(VMType::VM_VOID), VMType::ObjectType(editor_type.get())},
          [](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), size_t(1));
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);

            auto editor = static_cast<EditorState*>(args[0]->user_value.get());
            CHECK(editor != nullptr);

            auto buffer = editor->current_buffer();
            if (buffer == nullptr) {
              return Value::NewVoid();
            }

            if (editor->structure() == StructureLine()) {
              auto target_buffer = buffer->GetBufferFromCurrentLine();
              if (target_buffer != nullptr) {
                buffer = target_buffer;
              }
            }
            buffer->Save();
            editor->ResetModifiers();
            return Value::NewVoid();
          }));

  editor_type->AddField(
      L"home",
      vm::NewCallback(std::function<wstring(EditorState*)>(
          [](EditorState* editor) { return editor->home_directory(); })));

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
                           auto buffer = current_buffer();
                           CHECK(buffer != nullptr);
                           if (structure() == StructureLine()) {
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

  environment.Define(
      L"WaitForClose",
      Value::NewFunction(
          {VMType::Void(), VMType::ObjectType(L"SetString")},
          [this](vector<Value::Ptr> args, Trampoline* trampoline) {
            CHECK_EQ(args.size(), 1u);
            const auto& buffers_to_wait =
                *static_cast<std::set<wstring>*>(args[0]->user_value.get());

            auto pending = std::make_shared<int>(0);
            auto continuation = trampoline->Interrupt();
            for (const auto& buffer_name : buffers_to_wait) {
              auto buffer_it = buffers()->find(buffer_name);
              if (buffer_it == buffers()->end()) {
                continue;
              }
              (*pending)++;
              buffer_it->second->AddCloseObserver([pending, continuation]() {
                LOG(INFO) << "Buffer is closing, with: " << *pending;
                CHECK_GT(*pending, 0);
                if (--(*pending) == 0) {
                  LOG(INFO) << "Resuming!";
                  continuation(Value::NewVoid());
                }
              });
            }
            if (pending == 0) {
              trampoline->Return(Value::NewVoid());
            }
          }));

  environment.Define(
      L"SendExitTo",
      vm::NewCallback(std::function<void(wstring)>([](wstring args) {
        int fd = open(ToByteString(args).c_str(), O_WRONLY);
        string command = "Exit(0);\n";
        write(fd, command.c_str(), command.size());
        close(fd);
      })));

  environment.Define(L"Exit",
                     vm::NewCallback(std::function<void(int)>([](int status) {
                       LOG(INFO) << "Exit: " << status;
                       exit(status);
                     })));

  environment.Define(L"SetStatus", vm::NewCallback(std::function<void(wstring)>(
                                       [this](wstring s) { SetStatus(s); })));

  environment.Define(
      L"ScheduleRedraw",
      vm::NewCallback(std::function<void()>([this]() { ScheduleRedraw(); })));

  environment.Define(
      L"set_screen_needs_hard_redraw",
      vm::NewCallback(std::function<void(bool)>(
          [this](bool value) { set_screen_needs_hard_redraw(value); })));

  environment.Define(
      L"set_exit_value",
      vm::NewCallback(std::function<void(int)>(
          [this](int exit_value) { exit_value_ = exit_value; })));

  environment.Define(
      L"SetPositionColumn",
      vm::NewCallback(std::function<void(int)>([this](int value) {
        auto buffer = current_buffer();
        if (buffer == nullptr) {
          return;
        }
        buffer->set_position(LineColumn(buffer->position().line, value));
      })));

  environment.Define(
      L"Line",
      vm::NewCallback(std::function<wstring(void)>([this]() -> wstring {
        auto buffer = current_buffer();
        if (buffer == nullptr) {
          return L"";
        }
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

  environment.Define(L"repetitions",
                     vm::NewCallback(std::function<int()>([this]() {
                       return static_cast<int>(repetitions());
                     })));

  environment.Define(L"set_repetitions",
                     vm::NewCallback(std::function<void(int)>(
                         [this](int times) { set_repetitions(times); })));

  environment.Define(
      L"OpenFile",
      Value::NewFunction({VMType::ObjectType(L"Buffer"), VMType::VM_STRING,
                          VMType::VM_BOOLEAN},
                         [this](vector<unique_ptr<Value>> args) {
                           CHECK_EQ(args.size(), 2u);
                           CHECK(args[0]->IsString());
                           CHECK(args[1]->IsBool());
                           OpenFileOptions options;
                           options.editor_state = this;
                           options.path = args[0]->str;
                           options.make_current_buffer = args[1]->boolean;
                           return Value::NewObject(L"Buffer",
                                                   OpenFile(options)->second);
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

  InitShapes(&environment);
  RegisterTransformations(this, &environment);
  Modifiers::Register(&environment);
  return environment;
}

std::pair<int, int> BuildPipe() {
  int output[2];
  if (pipe2(output, O_NONBLOCK) == -1) {
    return {-1, -1};
  }
  return {output[0], output[1]};
}

EditorState::EditorState(command_line_arguments::Values args,
                         AudioPlayer* audio_player)
    : home_directory_(args.home_directory),
      edge_path_(args.config_paths),
      environment_(BuildEditorEnvironment()),
      default_commands_(NewCommandMode(this)),
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
    buffer.second->Close();
  }
}

void EditorState::CheckPosition() {
  auto buffer = FindActiveLeaf(&buffer_tree_)->leaf.lock();
  if (buffer != nullptr) {
    buffer->CheckPosition();
  }
}

bool EditorState::CloseBuffer(OpenBuffer* buffer) {
  CHECK(buffer != nullptr);
  if (!buffer->PrepareToClose()) {
    SetWarningStatus(L"🖝  Dirty buffers (“*ad” to ignore): " +
                     buffer->Read(buffer_variables::name()));
    return false;
  }
  ScheduleRedraw();

  if (current_buffer().get() == buffer) {
    RemoveActiveLeaf(&buffer_tree_);
    // TODO: Readjust: pick another one and make it active!
    // if (current_buffer_ != buffers_.end()) {
    //  current_buffer_->second->Visit();
    //}
  }

  buffer->Close();
  buffers_.erase(buffer->Read(buffer_variables::name()));

  return true;
}

void EditorState::set_current_buffer(shared_ptr<OpenBuffer> buffer) {
  FindActiveLeaf(&buffer_tree_)->leaf = buffer;
  if (buffer != nullptr) {
    buffer->Visit();
  }
}

void EditorState::AddHorizontalSplit() {
  if (buffer_tree_.type != BufferTree::Type::kHorizontal) {
    BufferTree old_tree = buffer_tree_;
    buffer_tree_.type = BufferTree::Type::kHorizontal;
    buffer_tree_.leaf.reset();
    buffer_tree_.children.clear();
    buffer_tree_.children.push_back(std::move(old_tree));
    buffer_tree_.active = 0;
  }

  buffer_tree_.active = buffer_tree_.children.size();
  buffer_tree_.children.push_back(BufferTree());
  set_screen_needs_hard_redraw(true);  // TODO: Why is this needed?
  ScheduleRedraw();
}

void EditorState::SetHorizontalSplitsWithAllBuffers() {
  auto active_buffer = current_buffer();
  buffer_tree_.type = BufferTree::Type::kHorizontal;
  buffer_tree_.leaf.reset();
  buffer_tree_.active = 0;
  buffer_tree_.children.clear();
  for (auto& buffer : buffers_) {
    if (!buffer.second->Read(buffer_variables::show_in_buffers_list())) {
      continue;
    }
    if (buffer.second == active_buffer) {
      buffer_tree_.active = buffer_tree_.children.size();
    }
    buffer_tree_.children.push_back(BufferTree());
    buffer_tree_.children.back().leaf = buffer.second;
  }
}

void EditorState::SetActiveLeaf(size_t position) {
  switch (buffer_tree_.type) {
    case BufferTree::Type::kLeaf:
      break;
    case BufferTree::Type::kHorizontal:
      CHECK(!buffer_tree_.children.empty());
      buffer_tree_.active = position % buffer_tree_.children.size();
      break;
  }
}

void EditorState::AdvanceActiveLeaf(int delta) {
  editor::AdvanceActiveLeaf(&buffer_tree_, delta);
  ScheduleRedraw();
}

void EditorState::ZoomToLeaf() {
  BufferTree tmp = *FindActiveLeaf(&buffer_tree_);
  buffer_tree_ = tmp;
  ScheduleRedraw();
}

void EditorState::BufferTreeRemoveActiveLeaf() {
  BufferTree::RemoveActiveLeaf(&buffer_tree_);
}

bool EditorState::has_current_buffer() const {
  return current_buffer() != nullptr;
}
shared_ptr<OpenBuffer> EditorState::current_buffer() {
  return FindActiveLeaf(&buffer_tree_)->leaf.lock();
}
const shared_ptr<OpenBuffer> EditorState::current_buffer() const {
  return FindActiveLeaf(&buffer_tree_)->leaf.lock();
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

bool EditorState::AttemptTermination(wstring* error_description,
                                     int exit_value) {
  LOG(INFO) << "Checking buffers for termination.";
  vector<wstring> buffers_with_problems;
  for (auto& it : buffers_) {
    if (!it.second->PrepareToClose()) {
      buffers_with_problems.push_back(it.first);
    }
  }
  if (buffers_with_problems.empty()) {
    LOG(INFO) << "Terminating.";
    exit_value_ = exit_value;
    return true;
  }

  wstring error = L"🖝  Dirty buffers (“*aq” to ignore):";
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
    handler = current_buffer()->mode();
  } else {
    handler = OpenAnonymousBuffer(this)->mode();
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
  auto it = buffers_.end();

  auto buffer = current_buffer();
  if (buffer != nullptr) {
    it = buffers_.find(buffer->Read(buffer_variables::name()));
  }

  if (it == buffers_.end()) {
    if (buffers_.empty()) {
      return;
    }
    it = buffers_.begin();
  }

  times = times % buffers_.size();
  for (size_t repetition = 0; repetition < times; repetition++) {
    ++it;
    if (it == buffers_.end()) {
      it = buffers_.begin();
    }
  }
  set_current_buffer(it->second);
  PushCurrentPosition();
}

void EditorState::MoveBufferBackwards(size_t times) {
  auto it = buffers_.end();

  auto buffer = current_buffer();
  if (buffer != nullptr) {
    it = buffers_.find(buffer->Read(buffer_variables::name()));
  }

  if (it == buffers_.end()) {
    if (buffers_.empty()) {
      return;
    }
    --it;
  }
  times = times % buffers_.size();
  for (size_t i = 0; i < times; i++) {
    if (it == buffers_.begin()) {
      it = buffers_.end();
    }
    --it;
  }
  set_current_buffer(it->second);
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
  auto buffer = current_buffer();
  if (buffer != nullptr) {
    PushPosition(buffer->position());
  }
}

void EditorState::PushPosition(LineColumn position) {
  auto buffer = current_buffer();
  if (buffer == nullptr ||
      !buffer->Read(buffer_variables::push_positions_to_history())) {
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
    buffer_it->second->Set(buffer_variables::save_on_close(), true);
    buffer_it->second->Set(buffer_variables::trigger_reload_on_buffer_write(),
                           false);
    buffer_it->second->Set(buffer_variables::show_in_buffers_list(), false);
  }
  CHECK(buffer_it->second != nullptr);
  buffer_it->second->CheckPosition();
  CHECK_LE(buffer_it->second->position().line,
           buffer_it->second->contents()->size());
  buffer_it->second->InsertLine(
      buffer_it->second->current_position_line(),
      std::make_shared<Line>(position.ToString() + L" " +
                             buffer->Read(buffer_variables::name())));
  CHECK_LE(buffer_it->second->position().line,
           buffer_it->second->contents()->size());
  if (buffer_it->second == buffer) {
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
    status_buffer_it.first->second->Set(buffer_variables::allow_dirty_delete(),
                                        true);
    status_buffer_it.first->second->Set(
        buffer_variables::show_in_buffers_list(), false);
  }
  status_buffer_it.first->second->AppendLazyString(NewLazyString(status));
  if (current_buffer() == status_buffer_it.first->second) {
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
  current_buffer()->ApplyToCursors(std::move(transformation));
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
        auto buffer = current_buffer();
        if (buffer == nullptr) {
          return;
        }
        auto target_buffer = buffer->GetBufferFromCurrentLine();
        if (target_buffer != nullptr) {
          buffer = target_buffer;
        }
        buffer->PushSignal(signal);
    }
  }
}

bool EditorState::handling_stop_signals() const {
  auto buffer = current_buffer();
  if (buffer == nullptr) {
    return false;
  }
  auto target_buffer = buffer->GetBufferFromCurrentLine();
  if (target_buffer != nullptr) {
    buffer = target_buffer;
  }
  return buffer->Read(buffer_variables::pts());
}

}  // namespace editor
}  // namespace afc

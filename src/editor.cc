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
#include "src/terminal.h"
#include "src/transformation/delete.h"
#include "src/transformation/stack.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm_transformation.h"
#include "src/widget_list.h"
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

template <typename MethodReturnType>
void RegisterBufferMethod(ObjectType* editor_type, const wstring& name,
                          MethodReturnType (OpenBuffer::*method)(void)) {
  auto callback = std::make_unique<Value>(VMType::FUNCTION);
  // Returns nothing.
  callback->type.type_arguments = {VMType(VMType::VM_VOID),
                                   VMType::ObjectType(editor_type)};
  callback->callback = [method](vector<unique_ptr<Value>> args, Trampoline*) {
    CHECK_EQ(args.size(), size_t(1));
    CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);

    auto editor = static_cast<EditorState*>(args[0]->user_value.get());
    CHECK(editor != nullptr);

    auto buffer = editor->current_buffer();
    if (buffer != nullptr) {
      (*buffer.*method)();
      editor->ResetModifiers();
    }
    return futures::ImmediateValue(EvaluationOutput::New(Value::NewVoid()));
  };
  editor_type->AddField(name, std::move(callback));
}
}  // namespace

void EditorState::NotifyInternalEvent() {
  VLOG(5) << "Internal event notification!";
  if (write(pipe_to_communicate_internal_events_.second, " ", 1) == -1) {
    status_.SetWarningText(L"Write to internal pipe failed: " +
                           FromByteString(strerror(errno)));
  }
}

// Executes pending work from all buffers.
void EditorState::ExecutePendingWork() {
  for (auto& buffer : buffers_) {
    buffer.second->ExecutePendingWork();
  }
  work_queue_.Execute();
}

WorkQueue::State EditorState::GetPendingWorkState() const {
  for (auto& buffer : buffers_) {
    if (buffer.second->GetPendingWorkState() == WorkQueue::State::kScheduled) {
      return WorkQueue::State::kScheduled;
    }
  }
  return work_queue_.state();
}

WorkQueue* EditorState::work_queue() const { return &work_queue_; }

std::shared_ptr<Environment> EditorState::BuildEditorEnvironment() {
  auto environment =
      std::make_shared<Environment>(afc::vm::Environment::GetDefault());

  environment->Define(L"terminal_control_u",
                      Value::NewString({Terminal::CTRL_U}));

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
      L"AddVerticalSplit",
      vm::NewCallback(std::function<void(EditorState*)>(
          [](EditorState* editor) { editor->AddVerticalSplit(); })));

  editor_type->AddField(
      L"AddHorizontalSplit",
      vm::NewCallback(std::function<void(EditorState*)>(
          [](EditorState* editor) { editor->AddHorizontalSplit(); })));

  editor_type->AddField(L"SetHorizontalSplitsWithAllBuffers",
                        vm::NewCallback(std::function<void(EditorState*)>(
                            [](EditorState* editor) {
                              editor->SetHorizontalSplitsWithAllBuffers();
                            })));

  editor_type->AddField(L"SetActiveBuffer",
                        vm::NewCallback(std::function<void(EditorState*, int)>(
                            [](EditorState* editor, int delta) {
                              editor->SetActiveBuffer(delta);
                            })));

  editor_type->AddField(L"AdvanceActiveBuffer",
                        vm::NewCallback(std::function<void(EditorState*, int)>(
                            [](EditorState* editor, int delta) {
                              editor->AdvanceActiveBuffer(delta);
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
  environment->Define(
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

  environment->Define(L"ProcessInput",
                      vm::NewCallback(std::function<void(int)>(
                          [this](int c) { ProcessInput(c); })));

  environment->Define(
      L"ConnectTo",
      vm::NewCallback(std::function<void(wstring)>(
          [this](wstring target) { OpenServerBuffer(this, target); })));

  environment->Define(
      L"WaitForClose",
      Value::NewFunction(
          {VMType::Void(), VMType::ObjectType(L"SetString")},
          [this](vector<Value::Ptr> args, Trampoline*) {
            CHECK_EQ(args.size(), 1u);
            const auto& buffers_to_wait =
                *static_cast<std::set<wstring>*>(args[0]->user_value.get());

            auto pending = std::make_shared<int>(0);
            futures::Future<EvaluationOutput> future;
            for (const auto& buffer_name : buffers_to_wait) {
              auto buffer_it = buffers()->find(buffer_name);
              if (buffer_it == buffers()->end()) {
                continue;
              }
              (*pending)++;
              buffer_it->second->AddCloseObserver(
                  [pending, consumer = future.consumer()]() {
                    LOG(INFO) << "Buffer is closing, with: " << *pending;
                    CHECK_GT(*pending, 0);
                    if (--(*pending) == 0) {
                      LOG(INFO) << "Resuming!";
                      consumer(EvaluationOutput::Return(Value::NewVoid()));
                    }
                  });
            }
            if (pending == 0) {
              future.consumer()(EvaluationOutput::Return(Value::NewVoid()));
            }
            return future.value();
          }));

  environment->Define(
      L"SendExitTo",
      vm::NewCallback(std::function<void(wstring)>([](wstring args) {
        int fd = open(ToByteString(args).c_str(), O_WRONLY);
        string command = "Exit(0);\n";
        write(fd, command.c_str(), command.size());
        close(fd);
      })));

  environment->Define(L"Exit",
                      vm::NewCallback(std::function<void(int)>([](int status) {
                        LOG(INFO) << "Exit: " << status;
                        exit(status);
                      })));

  environment->Define(
      L"SetStatus", vm::NewCallback(std::function<void(wstring)>(
                        [this](wstring s) { status_.SetInformationText(s); })));

  environment->Define(
      L"set_screen_needs_hard_redraw",
      vm::NewCallback(std::function<void(bool)>(
          [this](bool value) { set_screen_needs_hard_redraw(value); })));

  environment->Define(
      L"set_exit_value",
      vm::NewCallback(std::function<void(int)>(
          [this](int exit_value) { exit_value_ = exit_value; })));

  environment->Define(
      L"SetPositionColumn",
      vm::NewCallback(std::function<void(int)>([this](int value) {
        auto buffer = current_buffer();
        if (buffer == nullptr) {
          return;
        }
        buffer->set_position(
            LineColumn(buffer->position().line, ColumnNumber(value)));
      })));

  environment->Define(
      L"Line",
      vm::NewCallback(std::function<wstring(void)>([this]() -> wstring {
        auto buffer = current_buffer();
        if (buffer == nullptr) {
          return L"";
        }
        return buffer->current_line()->ToString();
      })));

  environment->Define(
      L"ForkCommand",
      vm::NewCallback(
          std::function<std::shared_ptr<OpenBuffer>(ForkCommandOptions*)>(
              [this](ForkCommandOptions* options) {
                return ForkCommand(this, *options);
              })));

  environment->Define(L"repetitions",
                      vm::NewCallback(std::function<int()>([this]() {
                        return static_cast<int>(repetitions());
                      })));

  environment->Define(L"set_repetitions",
                      vm::NewCallback(std::function<void(int)>(
                          [this](int times) { set_repetitions(times); })));

  environment->Define(
      L"OpenFile",
      Value::NewFunction(
          {VMType::ObjectType(L"Buffer"), VMType::VM_STRING,
           VMType::VM_BOOLEAN},
          [this](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), 2u);
            CHECK(args[0]->IsString());
            CHECK(args[1]->IsBool());
            OpenFileOptions options;
            options.editor_state = this;
            options.path = args[0]->str;
            options.insertion_type = args[1]->boolean
                                         ? BuffersList::AddBufferType::kVisit
                                         : BuffersList::AddBufferType::kIgnore;
            return Value::NewObject(L"Buffer", OpenFile(options)->second);
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
  environment->DefineType(L"Editor", std::move(editor_type));

  environment->Define(
      L"editor",
      Value::NewObject(L"Editor", shared_ptr<void>(this, [](void*) {})));

  OpenBuffer::RegisterBufferType(this, environment.get());

  InitShapes(environment.get());
  RegisterTransformations(this, environment.get());
  Modifiers::Register(environment.get());
  ForkCommandOptions::Register(environment.get());
  LineColumn::Register(environment.get());
  Range::Register(environment.get());
  return environment;
}

std::pair<int, int> BuildPipe() {
  int output[2];
  if (pipe2(output, O_NONBLOCK) == -1) {
    return {-1, -1};
  }
  return {output[0], output[1]};
}

EditorState::EditorState(CommandLineValues args, AudioPlayer* audio_player)
    : home_directory_(args.home_directory),
      edge_path_(args.config_paths),
      environment_(BuildEditorEnvironment()),
      default_commands_(NewCommandMode(this)),
      pipe_to_communicate_internal_events_(BuildPipe()),
      audio_player_(audio_player),
      buffer_tree_(std::make_unique<WidgetListHorizontal>(BufferWidget::New())),
      status_(GetConsole(), audio_player_),
      work_queue_([this] { NotifyInternalEvent(); }) {}

EditorState::~EditorState() {
  // TODO: Replace this with a custom deleter in the shared_ptr.  Simplify
  // CloseBuffer accordingly.
  LOG(INFO) << "Closing buffers.";
  for (auto& buffer : buffers_) {
    buffer.second->Close();
  }

  environment_->Clear();  // We may have loops. This helps break them.
}

void EditorState::CheckPosition() {
  auto buffer = buffer_tree_.GetActiveLeaf()->Lock();
  if (buffer != nullptr) {
    buffer->CheckPosition();
  }
}

void EditorState::CloseBuffer(OpenBuffer* buffer) {
  CHECK(buffer != nullptr);
  buffer->PrepareToClose(
      [this, buffer]() {
        buffer->Close();
        auto index = buffer_tree_.GetBufferIndex(buffer);
        buffer_tree_.RemoveBuffer(buffer);
        buffers_.erase(buffer->Read(buffer_variables::name));
        LOG(INFO) << "Adjusting widgets that may be displaying the buffer we "
                     "are deleting.";
        if (buffer_tree_.BuffersCount() == 0) return;
        auto replacement = buffer_tree_.GetBuffer(index.value_or(0) %
                                                  buffer_tree_.BuffersCount());
        buffer_tree_.ForEachBufferWidget([&](BufferWidget* widget) {
          auto widget_buffer = widget->Lock();
          if (widget_buffer == nullptr || widget_buffer.get() == buffer) {
            widget->SetBuffer(replacement);
          }
        });
      },
      [this, buffer](wstring error) {
        buffer->status()->SetWarningText(
            L"🖝  Unable to close (“*ad” to ignore): " + error + L": " +
            buffer->Read(buffer_variables::name));
      });
}

void EditorState::set_current_buffer(std::shared_ptr<OpenBuffer> buffer) {
  buffer_tree_.GetActiveLeaf()->SetBuffer(buffer);
  if (buffer != nullptr) {
    buffer->Visit();
  }
}

void EditorState::AddVerticalSplit() {
  auto casted_child = dynamic_cast<WidgetListVertical*>(buffer_tree_.Child());
  if (casted_child == nullptr) {
    buffer_tree_.WrapChild([this](std::unique_ptr<Widget> child) {
      return std::make_unique<WidgetListVertical>(std::move(child));
    });
    casted_child = dynamic_cast<WidgetListVertical*>(buffer_tree_.Child());
    CHECK(casted_child != nullptr);
  }
  casted_child->AddChild(BufferWidget::New(OpenAnonymousBuffer(this)));
}

void EditorState::AddHorizontalSplit() {
  auto casted_child = dynamic_cast<WidgetListHorizontal*>(buffer_tree_.Child());
  if (casted_child == nullptr) {
    buffer_tree_.WrapChild([this](std::unique_ptr<Widget> child) {
      return std::make_unique<WidgetListHorizontal>(std::move(child));
    });
    casted_child = dynamic_cast<WidgetListHorizontal*>(buffer_tree_.Child());
    CHECK(casted_child != nullptr);
  }
  casted_child->AddChild(BufferWidget::New(OpenAnonymousBuffer(this)));
}

void EditorState::SetHorizontalSplitsWithAllBuffers() {
  auto active_buffer = current_buffer();
  std::vector<std::unique_ptr<Widget>> buffers;
  size_t index_active = 0;
  for (auto& buffer : buffers_) {
    if (!buffer.second->Read(buffer_variables::show_in_buffers_list)) {
      continue;
    }
    if (buffer.second == active_buffer) {
      index_active = buffers.size();
    }
    buffers.push_back(BufferWidget::New(buffer.second));
  }
  CHECK(!buffers.empty());
  buffer_tree_.SetChild(
      std::make_unique<WidgetListHorizontal>(std::move(buffers), index_active));
}

void EditorState::SetActiveBuffer(size_t position) {
  buffer_tree_.GetActiveLeaf()->SetBuffer(
      buffer_tree_.GetBuffer(position % buffer_tree_.BuffersCount()));
}

void EditorState::AdvanceActiveLeaf(int delta) {
  size_t leaves = buffer_tree_.CountLeaves();
  LOG(INFO) << "AdvanceActiveLeaf with delta " << delta << " and leaves "
            << leaves;
  if (delta < 0) {
    delta = leaves - ((-delta) % leaves);
  } else {
    delta %= leaves;
  }
  VLOG(5) << "Delta adjusted to: " << delta;
  delta = buffer_tree_.AdvanceActiveLeafWithoutWrapping(delta);
  VLOG(6) << "After first advance, delta remaining: " << delta;
  if (delta > 0) {
    VLOG(7) << "Wrapping around end of tree";
    buffer_tree_.SetActiveLeavesAtStart();
    delta--;
  }
  delta = buffer_tree_.AdvanceActiveLeafWithoutWrapping(delta);
  VLOG(5) << "Done advance, with delta: " << delta;
}

void EditorState::AdvanceActiveBuffer(int delta) {
  delta += buffer_tree_.GetCurrentIndex();
  size_t total = buffer_tree_.BuffersCount();
  if (delta < 0) {
    delta = total - ((-delta) % total);
  } else {
    delta %= total;
  }
  buffer_tree_.GetActiveLeaf()->SetBuffer(
      buffer_tree_.GetBuffer(delta % total));
}

void EditorState::ZoomToLeaf() {
  buffer_tree_.SetChild(
      BufferWidget::New(buffer_tree_.GetActiveLeaf()->Lock()));
}

bool EditorState::has_current_buffer() const {
  return current_buffer() != nullptr;
}
shared_ptr<OpenBuffer> EditorState::current_buffer() {
  auto leaf = buffer_tree_.GetActiveLeaf();
  CHECK(leaf != nullptr);
  return leaf->Lock();
}
const shared_ptr<OpenBuffer> EditorState::current_buffer() const {
  return buffer_tree_.GetActiveLeaf()->Lock();
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

void EditorState::Terminate(TerminationType termination_type, int exit_value) {
  if (termination_type == TerminationType::kWhenClean) {
    LOG(INFO) << "Checking buffers for termination.";
    std::vector<wstring> buffers_with_problems;
    for (auto& it : buffers_) {
      if (it.second->IsUnableToPrepareToClose()) {
        buffers_with_problems.push_back(
            it.second->Read(buffer_variables::name));
      }
    }
    if (!buffers_with_problems.empty()) {
      wstring error = L"🖝  Dirty buffers (“*aq” to ignore):";
      for (auto name : buffers_with_problems) {
        error += L" " + name;
      }
      status_.SetWarningText(error);
      return;
    }
  }

  std::shared_ptr<int> pending_calls(
      new int(buffers_.size()),
      [this, exit_value, termination_type](int* value) {
        if (*value != 0) {
          LOG(INFO) << "Termination attempt didn't complete successfully. It "
                       "must mean that a new one has started.";
          return;
        }
        // Since `PrepareToClose is asynchronous, we must check that they are
        // all ready to be deleted.
        if (termination_type == TerminationType::kIgnoringErrors) {
          exit_value_ = exit_value;
          return;
        }
        LOG(INFO) << "Checking buffers state for termination.";
        std::vector<wstring> buffers_with_problems;
        for (auto& it : buffers_) {
          if (it.second->dirty() &&
              !it.second->Read(buffer_variables::allow_dirty_delete)) {
            buffers_with_problems.push_back(
                it.second->Read(buffer_variables::name));
          }
        }
        if (!buffers_with_problems.empty()) {
          wstring error = L"🖝  Dirty buffers (“*aq” to ignore):";
          for (auto name : buffers_with_problems) {
            error += L" " + name;
          }
          return status_.SetWarningText(error);
        }
        LOG(INFO) << "Terminating.";
        exit_value_ = exit_value;
      });

  for (auto& it : buffers_) {
    it.second->PrepareToClose([pending_calls]() { --*pending_calls; },
                              [pending_calls](wstring) { --*pending_calls; });
  }
}

void EditorState::ProcessInput(int c) {
  EditorMode* handler = keyboard_redirect().get();
  if (handler != nullptr) {
    // Pass.
  } else if (has_current_buffer()) {
    handler = current_buffer()->mode();
  } else {
    auto buffer = OpenAnonymousBuffer(this);
    if (!has_current_buffer()) {
      buffer_tree_.GetActiveLeaf()->SetBuffer(buffer);
    }
    handler = buffer->mode();
    CHECK(has_current_buffer());
  }
  handler->ProcessInput(c, this);
}

void EditorState::MoveBufferForwards(size_t times) {
  auto it = buffers_.end();

  auto buffer = current_buffer();
  if (buffer != nullptr) {
    it = buffers_.find(buffer->Read(buffer_variables::name));
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
    it = buffers_.find(buffer->Read(buffer_variables::name));
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
      !buffer->Read(buffer_variables::push_positions_to_history)) {
    return;
  }
  auto buffer_it = buffers_.find(kPositionsBufferName);
  if (buffer_it == buffers_.end()) {
    // Insert a new entry into the list of buffers.
    OpenFileOptions options;
    options.editor_state = this;
    options.name = kPositionsBufferName;
    if (!edge_path().empty()) {
      options.path = PathJoin(*edge_path().begin(), L"positions");
    }
    options.insertion_type = BuffersList::AddBufferType::kIgnore;
    buffer_it = OpenFile(options);
    CHECK(buffer_it != buffers()->end());
    CHECK(buffer_it->second != nullptr);
    buffer_it->second->Set(buffer_variables::save_on_close, true);
    buffer_it->second->Set(buffer_variables::trigger_reload_on_buffer_write,
                           false);
    buffer_it->second->Set(buffer_variables::show_in_buffers_list, false);
  }
  CHECK(buffer_it->second != nullptr);
  buffer_it->second->CheckPosition();
  CHECK_LE(buffer_it->second->position().line,
           LineNumber(0) + buffer_it->second->contents()->size());
  buffer_it->second->InsertLine(
      buffer_it->second->current_position_line(),
      std::make_shared<Line>(position.ToString() + L" " +
                             buffer->Read(buffer_variables::name)));
  CHECK_LE(buffer_it->second->position().line,
           LineNumber(0) + buffer_it->second->contents()->size());
}

static BufferPosition PositionFromLine(const wstring& line) {
  std::wstringstream line_stream(line);
  BufferPosition pos;
  line_stream >> pos.position.line.line >> pos.position.column.column;
  line_stream.get();
  getline(line_stream, pos.buffer_name);
  return pos;
}

std::shared_ptr<OpenBuffer> EditorState::GetConsole() {
  auto it = buffers_.insert(make_pair(L"- console", nullptr));
  if (it.second) {  // Inserted the entry.
    it.first->second = std::make_shared<OpenBuffer>(this, it.first->first);
    it.first->second->Set(buffer_variables::allow_dirty_delete, true);
    it.first->second->Set(buffer_variables::show_in_buffers_list, false);
  }
  return it.first->second;
}

bool EditorState::HasPositionsInStack() {
  auto it = buffers_.find(kPositionsBufferName);
  return it != buffers_.end() &&
         it->second->contents()->size() > LineNumberDelta(1);
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
    if (buffer->current_position_line() >= buffer->EndLine()) {
      return false;
    }
    buffer->set_current_position_line(buffer->current_position_line() +
                                      LineNumberDelta(1));
    return true;
  }

  if (buffer->current_position_line() == LineNumber(0)) {
    return false;
  }
  buffer->set_current_position_line(buffer->current_position_line() -
                                    LineNumberDelta(1));
  return true;
}

Status* EditorState::status() { return &status_; }
const Status* EditorState::status() const { return &status_; }

futures::DelayedValue<bool> EditorState::ApplyToCurrentBuffer(
    unique_ptr<Transformation> transformation) {
  CHECK(transformation != nullptr);
  CHECK(has_current_buffer());
  return current_buffer()->ApplyToCursors(std::move(transformation));
}

wstring EditorState::expand_path(const wstring& path) const {
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
  return buffer->Read(buffer_variables::pts);
}

}  // namespace editor
}  // namespace afc

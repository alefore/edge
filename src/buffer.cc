#include "buffer.h"

#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

extern "C" {
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "buffer_variables.h"
#include "char_buffer.h"
#include "command_with_modifiers.h"
#include "cpp_parse_tree.h"
#include "cursors_transformation.h"
#include "dirname.h"
#include "editor.h"
#include "file_link_mode.h"
#include "lazy_string_append.h"
#include "line_marks.h"
#include "map_mode.h"
#include "run_command_handler.h"
#include "server.h"
#include "src/parsers/diff.h"
#include "src/parsers/markdown.h"
#include "src/seek.h"
#include "substring.h"
#include "transformation.h"
#include "transformation_delete.h"
#include "vm/public/callbacks.h"
#include "vm/public/constant_expression.h"
#include "vm/public/function_call.h"
#include "vm/public/types.h"
#include "vm/public/value.h"
#include "vm/public/vm.h"
#include "wstring.h"

namespace afc {
namespace vm {
template <>
struct VMTypeMapper<editor::OpenBuffer*> {
  static editor::OpenBuffer* get(Value* value) {
    return static_cast<editor::OpenBuffer*>(value->user_value.get());
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::OpenBuffer*>::vmtype =
    VMType::ObjectType(L"Buffer");

}  // namespace vm
namespace editor {
namespace {

static const wchar_t* kOldCursors = L"old-cursors";

using std::unordered_set;

template <typename EdgeStruct, typename FieldValue>
void RegisterBufferFields(
    EditorState* editor_state, EdgeStruct* edge_struct,
    afc::vm::ObjectType* object_type,
    const FieldValue& (OpenBuffer::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (OpenBuffer::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  VMType buffer_type = VMType::ObjectType(object_type);

  vector<wstring> variable_names;
  edge_struct->RegisterVariableNames(&variable_names);
  for (const wstring& name : variable_names) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    object_type->AddField(
        variable->name(),
        vm::NewCallback(std::function<FieldValue(OpenBuffer*)>(
            [reader, variable](OpenBuffer* buffer) {
              DVLOG(4) << "Buffer field reader is returning.";
              return (buffer->*reader)(variable);
            })));

    // Setter.
    object_type->AddField(
        L"set_" + variable->name(),
        vm::NewCallback(std::function<void(OpenBuffer*, FieldValue)>(
            [editor_state, variable, setter](OpenBuffer* buffer,
                                             FieldValue value) {
              (buffer->*setter)(variable, value);
              editor_state->ScheduleRedraw();
            })));
  }
}
}  // namespace

using namespace afc::vm;
using std::to_wstring;

/* static */ const wstring OpenBuffer::kBuffersName = L"- buffers";
/* static */ const wstring OpenBuffer::kPasteBuffer = L"- paste buffer";

// TODO: Once we can capture std::unique_ptr, turn transformation into one.
void OpenBuffer::EvaluateMap(EditorState* editor, OpenBuffer* buffer,
                             size_t line, Value::Callback map_callback,
                             TransformationStack* transformation,
                             Trampoline* trampoline) {
  if (line + 1 >= buffer->contents()->size()) {
    buffer->Apply(editor, std::unique_ptr<Transformation>(transformation));
    trampoline->Return(Value::NewVoid());
    return;
  }
  wstring current_line = buffer->LineAt(line)->ToString();

  std::vector<std::unique_ptr<vm::Expression>> args_expr;
  args_expr.emplace_back(
      vm::NewConstantExpression(Value::NewString(std::move(current_line))));
  // TODO: Use unique_ptr and capture by value.
  std::shared_ptr<vm::Expression> map_line =
      vm::NewFunctionCall(vm::NewConstantExpression(Value::NewFunction(
                              {VMType::String()}, map_callback)),
                          std::move(args_expr));

  Evaluate(map_line.get(), trampoline->environment(),
           [editor, buffer, line, map_callback, transformation, trampoline,
            current_line, map_line](Value::Ptr value) {
             if (value->str != current_line) {
               DeleteOptions options;
               options.copy_to_paste_buffer = false;
               transformation->PushBack(NewDeleteLinesTransformation(options));
               auto buffer_to_insert =
                   std::make_shared<OpenBuffer>(editor, L"tmp buffer");
               buffer_to_insert->AppendLine(editor, NewCopyString(value->str));
               transformation->PushBack(
                   NewInsertBufferTransformation(buffer_to_insert, 1, END));
             }
             EvaluateMap(editor, buffer, line + 1, std::move(map_callback),
                         transformation, trampoline);
           });
}

/* static */ void OpenBuffer::RegisterBufferType(
    EditorState* editor_state, afc::vm::Environment* environment) {
  auto buffer = std::make_unique<ObjectType>(L"Buffer");

  RegisterBufferFields(editor_state, buffer_variables::BoolStruct(),
                       buffer.get(), &OpenBuffer::Read,
                       &OpenBuffer::set_bool_variable);
  RegisterBufferFields(editor_state, buffer_variables::StringStruct(),
                       buffer.get(), &OpenBuffer::Read,
                       &OpenBuffer::set_string_variable);
  RegisterBufferFields(editor_state, buffer_variables::IntStruct(),
                       buffer.get(), &OpenBuffer::Read,
                       &OpenBuffer::set_int_variable);
  RegisterBufferFields(editor_state, buffer_variables::DoubleStruct(),
                       buffer.get(), &OpenBuffer::Read,
                       &OpenBuffer::set_double_variable);

  buffer->AddField(
      L"line_count",
      vm::NewCallback(std::function<int(OpenBuffer*)>(
          [](OpenBuffer* buffer) { return int(buffer->contents()->size()); })));

  buffer->AddField(L"set_position",
                   vm::NewCallback(std::function<void(OpenBuffer*, LineColumn)>(
                       [](OpenBuffer* buffer, LineColumn position) {
                         buffer->set_position(position);
                       })));

  buffer->AddField(
      L"position",
      vm::NewCallback(std::function<LineColumn(OpenBuffer*)>(
          [](OpenBuffer* buffer) { return LineColumn(buffer->position()); })));

  buffer->AddField(
      L"line", vm::NewCallback(std::function<wstring(OpenBuffer*, int)>(
                   [](OpenBuffer* buffer, int line) {
                     size_t line_size_t = min(size_t(max(line, 0)),
                                              buffer->contents()->size() - 1);
                     return buffer->contents()->at(line_size_t)->ToString();
                   })));

  buffer->AddField(
      L"Map", Value::NewFunction(
                  {VMType::Void(), VMType::ObjectType(buffer.get()),
                   VMType::Function({VMType::String(), VMType::String()})},
                  [editor_state](vector<unique_ptr<Value>> args,
                                 Trampoline* evaluation) {
                    CHECK_EQ(args.size(), size_t(2));
                    CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
                    EvaluateMap(
                        editor_state,
                        static_cast<OpenBuffer*>(args[0]->user_value.get()), 0,
                        args[1]->callback,
                        std::make_unique<TransformationStack>().release(),
                        evaluation);
                  }));

  buffer->AddField(
      L"GetRegion",
      Value::NewFunction(
          {VMType::ObjectType(L"Range"), VMType::ObjectType(buffer.get()),
           VMType::String()},
          [editor_state](vector<Value::Ptr> args, Trampoline* trampoline) {
            CHECK_EQ(args.size(), 2u);
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            CHECK_EQ(args[1]->type, VMType::VM_STRING);
            // TODO: Don't ignore the buffer! Apply it to it!
            // auto buffer =
            // static_cast<OpenBuffer*>(args[0]->user_value.get());
            auto resume = trampoline->Interrupt();
            NewCommandWithModifiers(
                args[1]->str, L"Selects a region",
                [resume](EditorState*, OpenBuffer* buffer,
                         CommandApplyMode mode, Modifiers modifiers) {
                  // TODO: Apply this to all cursors. That's tricky, because we
                  // don't know what effect each transformation will have, and
                  // because we can't call `resume` more than once (it will
                  // likely free things when we call it).
                  if (mode == CommandApplyMode::FINAL) {
                    LOG(INFO) << "GetRegion: Resuming.";
                    LineColumn start, end;
                    buffer->FindPartialRange(modifiers, buffer->position(),
                                             &start, &end);
                    resume(Value::NewObject(
                        L"Range", std::make_shared<Range>(start, end)));
                  } else {
                    buffer->PushTransformationStack();
                    DeleteOptions options;
                    options.modifiers = modifiers;
                    options.copy_to_paste_buffer = false;
                    options.preview = true;
                    buffer->ApplyToCursors(
                        NewDeleteTransformation(options),
                        Modifiers::AFFECT_ONLY_CURRENT_CURSOR);
                    buffer->PopTransformationStack();
                  }
                })
                ->ProcessInput(L'\n', editor_state);
          }));

  buffer->AddField(
      L"AddKeyboardTextTransformer",
      Value::NewFunction(
          {VMType::Bool(), VMType::ObjectType(buffer.get()),
           VMType::Function({VMType::String(), VMType::String()})},
          [editor_state](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), size_t(2));
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
            CHECK(buffer != nullptr);
            return Value::NewBool(buffer->AddKeyboardTextTransformer(
                editor_state, std::move(args[1])));
          }));

  buffer->AddField(
      L"Filter", Value::NewFunction(
                     {VMType::Void(), VMType::ObjectType(buffer.get()),
                      VMType::Function({VMType::Bool(), VMType::String()})},
                     [editor_state](vector<unique_ptr<Value>> args) {
                       CHECK_EQ(args.size(), size_t(2));
                       CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
                       auto buffer =
                           static_cast<OpenBuffer*>(args[0]->user_value.get());
                       CHECK(buffer != nullptr);
                       buffer->set_filter(std::move(args[1]));
                       editor_state->ScheduleRedraw();
                       return Value::NewVoid();
                     }));

  buffer->AddField(
      L"DeleteCharacters",
      vm::NewCallback(std::function<void(OpenBuffer*, int)>(
          [editor_state](OpenBuffer* buffer, int count) {
            DeleteOptions options;
            options.modifiers.repetitions = count;
            buffer->ApplyToCursors(NewDeleteCharactersTransformation(options));
          })));

  buffer->AddField(L"Reload", vm::NewCallback(std::function<void(OpenBuffer*)>(
                                  [editor_state](OpenBuffer* buffer) {
                                    buffer->Reload(editor_state);
                                  })));

  buffer->AddField(
      L"InsertText",
      vm::NewCallback(std::function<void(OpenBuffer*, wstring)>(
          [editor_state](OpenBuffer* buffer, wstring text) {
            if (text.empty()) {
              return;  // Optimization.
            }
            if (buffer->fd() != -1) {
              auto str = ToByteString(text);
              LOG(INFO) << "Insert text: " << str.size();
              if (write(buffer->fd(), str.c_str(), str.size()) == -1) {
                editor_state->SetWarningStatus(L"Write failed: " +
                                               FromByteString(strerror(errno)));
              }
              return;
            }
            auto buffer_to_insert =
                std::make_shared<OpenBuffer>(editor_state, L"tmp buffer");

            // getline will silently eat the last (empty) line.
            std::wistringstream text_stream(text + L"\n");
            std::wstring line;
            bool insert_separator = false;
            while (std::getline(text_stream, line, wchar_t('\n'))) {
              if (insert_separator) {
                buffer_to_insert->AppendEmptyLine(editor_state);
              } else {
                insert_separator = true;
              }
              buffer_to_insert->AppendToLastLine(editor_state,
                                                 NewCopyString(line));
            }

            buffer->ApplyToCursors(
                NewInsertBufferTransformation(buffer_to_insert, 1, END));
          })));

  buffer->AddField(
      L"Save",
      vm::NewCallback(std::function<void(OpenBuffer*)>(
          [editor_state](OpenBuffer* buffer) { buffer->Save(editor_state); })));

  buffer->AddField(
      L"AddBinding",
      Value::NewFunction(
          {VMType::Void(), VMType::ObjectType(buffer.get()), VMType::String(),
           VMType::String(), VMType::Function({VMType::Void()})},
          [editor_state](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), 4u);
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            CHECK_EQ(args[1]->type, VMType::VM_STRING);
            CHECK_EQ(args[2]->type, VMType::VM_STRING);
            auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
            CHECK(buffer != nullptr);
            buffer->default_commands_->Add(args[1]->str, args[2]->str,
                                           std::move(args[3]),
                                           &buffer->environment_);
            return Value::NewVoid();
          }));

  buffer->AddField(
      L"AddBindingToFile",
      vm::NewCallback(std::function<void(OpenBuffer*, wstring, wstring)>(
          [editor_state](OpenBuffer* buffer, wstring keys, wstring path) {
            LOG(INFO) << "AddBindingToFile: " << keys << " -> " << path;
            buffer->default_commands_->Add(
                keys,
                [editor_state, buffer, path]() {
                  wstring resolved_path;
                  ResolvePathOptions options;
                  options.editor_state = editor_state;
                  options.path = path;
                  options.output_path = &resolved_path;
                  if (!ResolvePath(options)) {
                    editor_state->SetWarningStatus(L"Unable to resolve: " +
                                                   path);
                  } else {
                    buffer->EvaluateFile(editor_state, resolved_path);
                  }
                },
                L"Load file: " + path);
          })));

  buffer->AddField(L"EvaluateFile",
                   vm::NewCallback(std::function<void(OpenBuffer*, wstring)>(
                       [editor_state](OpenBuffer* buffer, wstring path) {
                         LOG(INFO) << "Evaluating file: " << path;
                         buffer->EvaluateFile(editor_state, path);
                       })));

  environment->DefineType(L"Buffer", std::move(buffer));
}

void OpenBuffer::BackgroundThread() {
  while (true) {
    std::unique_lock<std::mutex> lock(mutex_);
    background_condition_.wait(lock, [this]() {
      return background_thread_shutting_down_ || contents_to_parse_ != nullptr;
    });
    VLOG(5) << "Background thread is waking up.";
    if (background_thread_shutting_down_) {
      return;
    }
    std::unique_ptr<const BufferContents> contents =
        std::move(contents_to_parse_);
    CHECK(contents_to_parse_ == nullptr);
    auto parser = tree_parser_;
    size_t lines_for_zoomed_out_tree = lines_for_zoomed_out_tree_;
    lock.unlock();

    if (contents == nullptr) {
      continue;
    }
    auto parse_tree = std::make_shared<ParseTree>();
    if (!contents->empty()) {
      parse_tree->range.end.line = contents->size() - 1;
      parse_tree->range.end.column = contents->back()->size();
      parser->FindChildren(*contents, parse_tree.get());
    }
    auto simplified_parse_tree = std::make_shared<ParseTree>();
    SimplifyTree(*parse_tree, simplified_parse_tree.get());

    std::shared_ptr<const ParseTree> zoomed_out_tree;
    if (lines_for_zoomed_out_tree != 0) {
      zoomed_out_tree = std::make_shared<ParseTree>(ZoomOutTree(
          *simplified_parse_tree, contents->size(), lines_for_zoomed_out_tree));
    }

    std::unique_lock<std::mutex> final_lock(mutex_);
    parse_tree_ = parse_tree;
    simplified_parse_tree_ = simplified_parse_tree;
    zoomed_out_tree_ = zoomed_out_tree;
    editor_->ScheduleRedraw();
    final_lock.unlock();

    editor_->NotifyInternalEvent();
  }
}

OpenBuffer::OpenBuffer(EditorState* editor_state, const wstring& name)
    : editor_(editor_state),
      name_(name),
      child_pid_(-1),
      child_exit_status_(0),
      position_pts_(LineColumn(0, 0)),
      modified_(false),
      reading_from_parser_(false),
      bool_variables_(buffer_variables::BoolStruct()->NewInstance()),
      string_variables_(buffer_variables::StringStruct()->NewInstance()),
      int_variables_(buffer_variables::IntStruct()->NewInstance()),
      double_variables_(buffer_variables::DoubleStruct()->NewInstance()),
      environment_(editor_state->environment()),
      filter_version_(0),
      last_transformation_(NewNoopTransformation()),
      parse_tree_(std::make_shared<ParseTree>()),
      tree_parser_(NewNullTreeParser()),
      default_commands_(editor_->default_commands()->NewChild()),
      mode_(std::make_unique<MapMode>(default_commands_)) {
  contents_.AddUpdateListener(
      [this](const CursorsTracker::Transformation& transformation) {
        editor_->ScheduleParseTreeUpdate(this);
        modified_ = true;
        time(&last_action_);
        cursors_tracker_.AdjustCursors(transformation);
      });
  UpdateTreeParser();

  environment_.Define(
      L"buffer",
      Value::NewObject(L"Buffer", shared_ptr<void>(this, [](void*) {})));

  set_string_variable(buffer_variables::path(), L"");
  set_string_variable(buffer_variables::pts_path(), L"");
  set_string_variable(buffer_variables::command(), L"");
  set_bool_variable(buffer_variables::reload_after_exit(), false);
  if (name_ == kPasteBuffer) {
    set_bool_variable(buffer_variables::allow_dirty_delete(), true);
    set_bool_variable(buffer_variables::show_in_buffers_list(), false);
    set_bool_variable(buffer_variables::delete_into_paste_buffer(), false);
  }
  ClearContents(editor_state);
}

OpenBuffer::~OpenBuffer() {
  LOG(INFO) << "Buffer deleted: " << name_;
  editor_->UnscheduleParseTreeUpdate(this);
  DestroyThreadIf([]() { return true; });
}

bool OpenBuffer::PrepareToClose(EditorState* editor_state) {
  if (!PersistState() &&
      editor_state->modifiers().strength <= Modifiers::DEFAULT) {
    LOG(INFO) << "Unable to persist state: " << name_;
    return false;
  }
  if (!dirty()) {
    LOG(INFO) << name_ << ": clean, skipping.";
    return true;
  }
  if (Read(buffer_variables::save_on_close())) {
    LOG(INFO) << name_ << ": attempting to save buffer.";
    // TODO(alejo): Let Save give us status?
    Save(editor_state);
    if (!dirty()) {
      LOG(INFO) << name_ << ": successful save.";
      return true;
    }
  }
  if (Read(buffer_variables::allow_dirty_delete())) {
    LOG(INFO) << name_ << ": allows dirty delete, skipping.";
    return true;
  }
  if (editor_state->modifiers().strength > Modifiers::DEFAULT) {
    LOG(INFO) << name_ << ": Deleting due to modifiers.";
    return true;
  }
  return false;
}

void OpenBuffer::Close(EditorState* editor_state) {
  LOG(INFO) << "Closing buffer: " << name_;
  if (dirty() && Read(buffer_variables::save_on_close())) {
    LOG(INFO) << "Saving buffer: " << name_;
    Save(editor_state);
  }
}

void OpenBuffer::AddEndOfFileObserver(std::function<void()> observer) {
  if (fd_.fd == -1) {
    observer();
    return;
  }
  end_of_file_observers_.push_back(observer);
}

void OpenBuffer::Visit(EditorState* editor_state) {
  if (Read(buffer_variables::reload_on_enter())) {
    Reload(editor_state);
    CheckPosition();
  }
  time(&last_visit_);
  time(&last_action_);
}

time_t OpenBuffer::last_visit() const { return last_visit_; }
time_t OpenBuffer::last_action() const { return last_action_; }

bool OpenBuffer::PersistState() const { return true; }

void OpenBuffer::ClearContents(EditorState* editor_state) {
  VLOG(5) << "Clear contents of buffer: " << name_;
  editor_state->line_marks()->RemoveExpiredMarksFromSource(name_);
  editor_state->line_marks()->ExpireMarksFromSource(*this, name_);
  editor_state->ScheduleRedraw();
  EraseLines(0, contents_.size());
  position_pts_ = LineColumn();
  last_transformation_ = NewNoopTransformation();
  last_transformation_stack_.clear();
  transformations_past_.clear();
  transformations_future_.clear();
  AppendEmptyLine(editor_state);
}

void OpenBuffer::AppendEmptyLine(EditorState*) {
  contents_.push_back(std::make_shared<Line>());
  MaybeFollowToEndOfFile();
}

void OpenBuffer::EndOfFile(EditorState* editor_state) {
  time(&last_action_);
  CHECK_EQ(fd_.fd, -1);
  CHECK_EQ(fd_error_.fd, -1);
  if (child_pid_ != -1) {
    if (waitpid(child_pid_, &child_exit_status_, 0) == -1) {
      editor_state->SetStatus(L"waitpid failed: " +
                              FromByteString(strerror(errno)));
      return;
    }
  }

  // We can remove expired marks now. We know that the set of fresh marks is now
  // complete.
  editor_state->line_marks()->RemoveExpiredMarksFromSource(name_);
  editor_state->ScheduleRedraw();

  child_pid_ = -1;

  vector<std::function<void()>> observers;
  observers.swap(end_of_file_observers_);
  for (auto& observer : observers) {
    observer();
  }

  if (Read(buffer_variables::reload_after_exit())) {
    set_bool_variable(buffer_variables::reload_after_exit(),
                      Read(buffer_variables::default_reload_after_exit()));
    Reload(editor_state);
  }
  if (Read(buffer_variables::close_after_clean_exit()) &&
      WIFEXITED(child_exit_status_) && WEXITSTATUS(child_exit_status_) == 0) {
    auto it = editor_state->buffers()->find(name_);
    if (it != editor_state->buffers()->end()) {
      editor_state->CloseBuffer(it);
    }
  }

  if (editor_state->has_current_buffer() &&
      editor_state->current_buffer()->first == kBuffersName) {
    editor_state->current_buffer()->second->Reload(editor_state);
  }
}

void OpenBuffer::MaybeFollowToEndOfFile() {
  if (IsPastPosition(desired_position_)) {
    VLOG(5) << "desired_position_ is realized: " << desired_position_;
    set_position(desired_position_);
    desired_position_ = LineColumn::Max();
  }
  if (!Read(buffer_variables::follow_end_of_file())) {
    return;
  }
  if (Read(buffer_variables::pts())) {
    set_position(position_pts_);
  } else {
    set_position(LineColumn(contents_.size()));
  }
}

bool OpenBuffer::ShouldDisplayProgress() const {
  return fd_.fd != -1 || fd_error_.fd != -1;
}

void OpenBuffer::RegisterProgress() {
  struct timespec now;
  if (clock_gettime(0, &now) == -1) {
    return;
  }
  double ms_elapsed = (now.tv_sec - last_progress_update_.tv_sec) * 1000 +
                      (now.tv_nsec - last_progress_update_.tv_nsec) / 1000000;
  if (ms_elapsed < 200) {
    return;
  }
  last_progress_update_ = now;
  set_int_variable(buffer_variables::progress(),
                   Read(buffer_variables::progress()) + 1);
}

void OpenBuffer::ReadData(EditorState* editor_state) {
  fd_.ReadData(editor_state, this);
}

void OpenBuffer::ReadErrorData(EditorState* editor_state) {
  fd_error_.ReadData(editor_state, this);
}

vector<unordered_set<LineModifier, hash<int>>> ModifiersVector(
    const unordered_set<LineModifier, hash<int>>& input, size_t size) {
  return vector<unordered_set<LineModifier, hash<int>>>(size, input);
}

void OpenBuffer::Input::ReadData(EditorState* editor_state,
                                 OpenBuffer* target) {
  LOG(INFO) << "Reading input from " << fd << " for buffer " << target->name();
  static const size_t kLowBufferSize = 1024 * 60;
  if (low_buffer == nullptr) {
    CHECK_EQ(low_buffer_length, size_t(0));
    low_buffer.reset(new char[kLowBufferSize]);
  }
  ssize_t characters_read = read(fd, low_buffer.get() + low_buffer_length,
                                 kLowBufferSize - low_buffer_length);
  LOG(INFO) << "Read returns: " << characters_read;
  if (characters_read == -1) {
    if (errno == EAGAIN) {
      return;
    }
    target->RegisterProgress();
    Close();
    Reset();
    if (target->fd_.fd == -1 && target->fd_error_.fd == -1) {
      target->EndOfFile(editor_state);
    }
    return;
  }
  CHECK_GE(characters_read, 0);
  CHECK_LE(characters_read, ssize_t(kLowBufferSize - low_buffer_length));
  if (characters_read == 0) {
    Close();
    Reset();
    target->RegisterProgress();
    if (target->fd_.fd == -1 && target->fd_error_.fd == -1) {
      target->EndOfFile(editor_state);
    }
    return;
  }
  low_buffer_length += characters_read;

  const char* low_buffer_tmp = low_buffer.get();
  int output_characters =
      mbsnrtowcs(nullptr, &low_buffer_tmp, low_buffer_length, 0, nullptr);
  std::vector<wchar_t> buffer(output_characters == -1 ? low_buffer_length
                                                      : output_characters);

  low_buffer_tmp = low_buffer.get();
  if (output_characters == -1) {
    low_buffer_tmp = nullptr;
    for (size_t i = 0; i < low_buffer_length; i++) {
      buffer[i] = static_cast<wchar_t>(*(low_buffer.get() + i));
    }
  } else {
    mbsnrtowcs(&buffer[0], &low_buffer_tmp, low_buffer_length, buffer.size(),
               nullptr);
  }

  shared_ptr<LazyString> buffer_wrapper(NewStringFromVector(std::move(buffer)));
  VLOG(5) << "Input: [" << buffer_wrapper->ToString() << "]";

  size_t processed = low_buffer_tmp == nullptr
                         ? low_buffer_length
                         : low_buffer_tmp - low_buffer.get();
  VLOG(5) << target->name() << ": Characters consumed: " << processed;
  VLOG(5) << target->name()
          << ": Characters produced: " << buffer_wrapper->size();
  CHECK_LE(processed, low_buffer_length);
  memmove(low_buffer.get(), low_buffer_tmp, low_buffer_length - processed);
  low_buffer_length -= processed;
  if (low_buffer_length == 0) {
    LOG(INFO) << "Consumed all input.";
    low_buffer = nullptr;
  }

  if (target->Read(buffer_variables::vm_exec())) {
    LOG(INFO) << target->name()
              << ": Evaluating VM code: " << buffer_wrapper->ToString();
    target->EvaluateString(editor_state, buffer_wrapper->ToString(),
                           [](std::unique_ptr<Value>) {});
  }

  target->RegisterProgress();
  bool previous_modified = target->modified();
  if (target->Read(buffer_variables::pts())) {
    target->ProcessCommandInput(editor_state, buffer_wrapper);
    editor_state->ScheduleRedraw();
  } else {
    size_t line_start = 0;
    for (size_t i = 0; i < buffer_wrapper->size(); i++) {
      if (buffer_wrapper->get(i) == '\n') {
        auto line = Substring(buffer_wrapper, line_start, i - line_start);
        VLOG(8) << "Adding line from " << line_start << " to " << i;
        target->AppendToLastLine(editor_state, line,
                                 ModifiersVector(modifiers, line->size()));
        target->StartNewLine(editor_state);
        target->MaybeFollowToEndOfFile();
        line_start = i + 1;
        if (editor_state->has_current_buffer() &&
            editor_state->current_buffer()->second.get() == target &&
            target->contents()->size() <=
                target->Read(buffer_variables::view_start_line()) +
                    editor_state->visible_lines()) {
          editor_state->ScheduleRedraw();
        }
      }
    }
    if (line_start < buffer_wrapper->size()) {
      VLOG(8) << "Adding last line from " << line_start << " to "
              << buffer_wrapper->size();
      auto line = Substring(buffer_wrapper, line_start,
                            buffer_wrapper->size() - line_start);
      target->AppendToLastLine(editor_state, line,
                               ModifiersVector(modifiers, line->size()));
    }
  }
  if (!previous_modified) {
    target->ClearModified();  // These changes don't count.
  }
  if (editor_state->has_current_buffer() &&
      editor_state->current_buffer()->first == kBuffersName) {
    editor_state->current_buffer()->second->Reload(editor_state);
  }
  editor_state->ScheduleRedraw();
}

void OpenBuffer::UpdateTreeParser() {
  auto parser = Read(buffer_variables::tree_parser());
  std::wistringstream typos_stream(Read(buffer_variables::typos()));
  std::unordered_set<wstring> typos_set{
      std::istream_iterator<std::wstring, wchar_t>(typos_stream),
      std::istream_iterator<std::wstring, wchar_t>()};

  std::unique_lock<std::mutex> lock(mutex_);
  if (parser == L"text") {
    tree_parser_ = NewLineTreeParser(
        NewWordsTreeParser(Read(buffer_variables::word_characters()), typos_set,
                           NewNullTreeParser()));
  } else if (parser == L"cpp") {
    std::wistringstream keywords(Read(buffer_variables::language_keywords()));
    tree_parser_ =
        NewCppTreeParser(std::unordered_set<wstring>(
                             std::istream_iterator<wstring, wchar_t>(keywords),
                             std::istream_iterator<wstring, wchar_t>()),
                         std::move(typos_set));
  } else if (parser == L"diff") {
    tree_parser_ = parsers::NewDiffTreeParser();
  } else if (parser == L"md") {
    tree_parser_ = parsers::NewMarkdownTreeParser();
  } else {
    tree_parser_ = NewNullTreeParser();
  }
  editor_->ScheduleParseTreeUpdate(this);
  lock.unlock();

  DestroyThreadIf([this]() { return TreeParser::IsNull(tree_parser_.get()); });
}

void OpenBuffer::ResetParseTree() {
  VLOG(5) << "Resetting parse tree.";
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (TreeParser::IsNull(tree_parser_.get())) {
      return;
    }
    contents_to_parse_ = contents_.copy();
  }

  {
    std::unique_lock<std::mutex> lock(thread_creation_mutex_);
    if (!background_thread_.joinable()) {
      std::unique_lock<std::mutex> lock(mutex_);
      background_thread_shutting_down_ = false;
      background_thread_ = std::thread([this]() { BackgroundThread(); });
    }
  }

  background_condition_.notify_one();
}

void OpenBuffer::DestroyThreadIf(std::function<bool()> predicate) {
  std::unique_lock<std::mutex> thread_creation_lock(thread_creation_mutex_);
  if (!background_thread_.joinable()) {
    return;
  }

  std::unique_lock<std::mutex> lock(mutex_);
  if (!predicate()) {
    return;
  }
  background_thread_shutting_down_ = true;
  lock.unlock();

  background_condition_.notify_one();
  background_thread_.join();
}

void OpenBuffer::StartNewLine(EditorState* editor_state) {
  if (!contents_.empty()) {
    DVLOG(5) << "Line is completed: " << contents_.back()->ToString();

    if (Read(buffer_variables::contains_line_marks())) {
      wstring path;
      LineColumn position;
      wstring pattern;
      ResolvePathOptions options;
      options.editor_state = editor_state;
      options.path = contents_.back()->ToString();
      options.output_path = &path;
      options.output_position = &position;
      options.output_pattern = &pattern;
      if (ResolvePath(options)) {
        LineMarks::Mark mark;
        mark.source = name_;
        mark.source_line = contents_.size() - 1;
        mark.target_buffer = path;
        mark.target = position;
        LOG(INFO) << "Found a mark: " << mark;
        editor_state->line_marks()->AddMark(mark);
      }
    }
  }
  contents_.push_back(std::make_shared<Line>());
}

void OpenBuffer::Reload(EditorState* editor_state) {
  if (child_pid_ != -1) {
    LOG(INFO) << "Sending SIGTERM.";
    kill(-child_pid_, SIGTERM);
    set_bool_variable(buffer_variables::reload_after_exit(), true);
    return;
  }
  for (const auto& dir : editor_state->edge_path()) {
    EvaluateFile(editor_state, PathJoin(dir, L"hooks/buffer-reload.cc"));
  }
  auto buffer_path = Read(buffer_variables::path());
  for (const auto& dir : editor_state->edge_path()) {
    auto state_path = PathJoin(PathJoin(dir, L"state"),
                               PathJoin(buffer_path, L".edge_state"));
    struct stat stat_buffer;
    if (stat(ToByteString(state_path).c_str(), &stat_buffer) == -1) {
      continue;
    }
    EvaluateFile(editor_state, state_path);
  }
  if (desired_position_ == LineColumn::Max()) {
    VLOG(5) << "Setting desired_position_ to current position: " << position();
    desired_position_ = position();
  }
  ClearModified();
  LOG(INFO) << "Starting reload: " << name_;
  ReloadInto(editor_state, this);
  CheckPosition();
}

void OpenBuffer::Save(EditorState* editor_state) {
  LOG(INFO) << "Saving buffer: " << name_;
  editor_state->SetStatus(L"Buffer can't be saved.");
}

void OpenBuffer::AppendLazyString(EditorState* editor_state,
                                  shared_ptr<LazyString> input) {
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

void OpenBuffer::SortContents(size_t first, size_t last,
                              std::function<bool(const shared_ptr<const Line>&,
                                                 const shared_ptr<const Line>&)>
                                  compare) {
  CHECK(first <= last);
  contents_.sort(first, last, compare);
}

void OpenBuffer::EraseLines(size_t first, size_t last) {
  contents_.EraseLines(first, last);
  CHECK_LE(position().line, lines_size());
}

void OpenBuffer::InsertLine(size_t line_position, shared_ptr<Line> line) {
  contents_.insert_line(line_position, line);
}

void OpenBuffer::AppendLine(EditorState* editor_state,
                            shared_ptr<LazyString> str) {
  CHECK(str != nullptr);
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

void OpenBuffer::AppendRawLine(EditorState* editor,
                               shared_ptr<LazyString> str) {
  AppendRawLine(editor, std::make_shared<Line>(Line::Options(str)));
}

void OpenBuffer::AppendRawLine(EditorState*, shared_ptr<Line> line) {
  contents_.push_back(line);
  MaybeFollowToEndOfFile();
}

void OpenBuffer::ProcessCommandInput(EditorState* editor_state,
                                     shared_ptr<LazyString> str) {
  CHECK(Read(buffer_variables::pts()));
  if (position_pts_.line >= contents_.size()) {
    position_pts_.line = contents_.size() - 1;
  }
  CHECK_LT(position_pts_.line, contents_.size());
  auto current_line = contents_.at(position_pts_.line);
  std::unordered_set<LineModifier, hash<int>> modifiers;

  size_t read_index = 0;
  VLOG(5) << "Terminal input: " << str->ToString();
  while (read_index < str->size()) {
    int c = str->get(read_index);
    read_index++;
    if (c == '\b') {
      if (position_pts_.column > 0) {
        position_pts_.column--;
        MaybeFollowToEndOfFile();
      }
    } else if (c == '\a') {
      auto status = editor_state->status();
      if (!all_of(status.begin(), status.end(), [](const wchar_t& c) {
            return c == L'♪' || c == L'♫' || c == L'…' || c == L' ' ||
                   c == L'𝄞';
          })) {
        status = L"𝄞";
      } else if (status.size() >= 40) {
        status = L"…" + status.substr(status.size() - 40, status.size());
      }
      editor_state->SetStatus(status + L" " +
                              (status.back() == L'♪' ? L"♫" : L"♪"));
      BeepFrequencies(editor_state->audio_player(), {783.99, 523.25, 659.25});
    } else if (c == '\r') {
      position_pts_.column = 0;
      MaybeFollowToEndOfFile();
    } else if (c == '\n') {
      position_pts_.line++;
      position_pts_.column = 0;
      if (position_pts_.line == contents_.size()) {
        contents_.push_back(std::make_shared<Line>());
      }
      CHECK_LT(position_pts_.line, contents_.size());
      current_line = contents_.at(position_pts_.line);
      MaybeFollowToEndOfFile();
    } else if (c == 0x1b) {
      read_index = ProcessTerminalEscapeSequence(editor_state, str, read_index,
                                                 &modifiers);
      CHECK_LT(position_pts_.line, contents_.size());
      current_line = contents_.at(position_pts_.line);
    } else if (isprint(c) || c == '\t') {
      contents_.SetCharacter(position_pts_.line, position_pts_.column, c,
                             modifiers);
      current_line = LineAt(position_pts_.line);
      position_pts_.column++;
      MaybeFollowToEndOfFile();
    } else {
      LOG(INFO) << "Unknown character: [" << c << "]\n";
    }
  }
}

size_t OpenBuffer::ProcessTerminalEscapeSequence(
    EditorState* editor_state, shared_ptr<LazyString> str, size_t read_index,
    std::unordered_set<LineModifier, hash<int>>* modifiers) {
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
        position_pts_.column = 0;
        MaybeFollowToEndOfFile();
        if (static_cast<size_t>(Read(buffer_variables::view_start_line())) >
            position_pts_.line) {
          set_int_variable(buffer_variables::view_start_line(),
                           position_pts_.line);
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
  auto current_line = contents_.at(position_pts_.line);
  string sequence;
  while (read_index < str->size()) {
    int c = str->get(read_index);
    read_index++;
    switch (c) {
      case '@': {
        // ich: insert character
        DLOG(INFO) << "Terminal: ich: Insert character.";
        contents_.InsertCharacter(position_pts_.line, position_pts_.column);
        return read_index;
      }

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
          modifiers->insert(LineModifier::BOLD);
        } else if (sequence == "3") {
          // TODO(alejo): Support italic on.
        } else if (sequence == "4") {
          modifiers->insert(LineModifier::UNDERLINE);
        } else if (sequence == "23") {
          // Fraktur off, italic off.  No need to do anything for now.
        } else if (sequence == "24") {
          modifiers->erase(LineModifier::UNDERLINE);
        } else if (sequence == "31") {
          modifiers->clear();
          modifiers->insert(LineModifier::RED);
        } else if (sequence == "32") {
          modifiers->clear();
          modifiers->insert(LineModifier::GREEN);
        } else if (sequence == "36") {
          modifiers->clear();
          modifiers->insert(LineModifier::CYAN);
        } else if (sequence == "1;30") {
          modifiers->clear();
          modifiers->insert(LineModifier::BOLD);
          modifiers->insert(LineModifier::BLACK);
        } else if (sequence == "1;31") {
          modifiers->clear();
          modifiers->insert(LineModifier::BOLD);
          modifiers->insert(LineModifier::RED);
        } else if (sequence == "1;36") {
          modifiers->clear();
          modifiers->insert(LineModifier::BOLD);
          modifiers->insert(LineModifier::CYAN);
        } else if (sequence == "0;36") {
          modifiers->clear();
          modifiers->insert(LineModifier::CYAN);
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
        MaybeFollowToEndOfFile();
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
                                 ? 0
                                 : stoul(sequence.substr(pos + 1)) - 1;
            } else if (!sequence.empty()) {
              line_delta = stoul(sequence);
            }
          } catch (const std::invalid_argument& ia) {
            editor_state->SetStatus(
                L"Unable to parse sequence from terminal in 'home' command: "
                L"\"" +
                FromByteString(sequence) + L"\"");
          }
          DLOG(INFO) << "Move cursor home: line: " << line_delta
                     << ", column: " << column_delta;
          position_pts_ =
              LineColumn(Read(buffer_variables::view_start_line()) + line_delta,
                         column_delta);
          while (position_pts_.line >= contents_.size()) {
            contents_.push_back(std::make_shared<Line>());
          }
          MaybeFollowToEndOfFile();
          set_int_variable(buffer_variables::view_start_column(), column_delta);
        }
        return read_index;

      case 'J':
        // ed: clear to end of screen.
        EraseLines(position_pts_.line + 1, contents_.size());
        CHECK_LT(position_pts_.line, contents_.size());
        return read_index;

      case 'K': {
        // el: clear to end of line.
        contents_.DeleteCharactersFromLine(position_pts_.line,
                                           position_pts_.column);
        return read_index;
      }

      case 'M':
        // dl1: delete one line.
        {
          EraseLines(position_pts_.line, position_pts_.line + 1);
          CHECK_LT(position_pts_.line, contents_.size());
        }
        return read_index;

      case 'P': {
        contents_.DeleteCharactersFromLine(
            position_pts_.line, position_pts_.column,
            min(static_cast<size_t>(atoi(sequence.c_str())),
                current_line->size()));
        current_line = LineAt(position_pts_.line);
        return read_index;
      }
      default:
        sequence.push_back(c);
    }
  }
  LOG(INFO) << "Unhandled character sequence: " << sequence;
  return read_index;
}

void OpenBuffer::AppendToLastLine(EditorState* editor_state,
                                  shared_ptr<LazyString> str) {
  vector<unordered_set<LineModifier, hash<int>>> modifiers(str->size());
  AppendToLastLine(editor_state, str, modifiers);
}

void OpenBuffer::AppendToLastLine(
    EditorState*, shared_ptr<LazyString> str,
    const vector<unordered_set<LineModifier, hash<int>>>& modifiers) {
  CHECK_EQ(str->size(), modifiers.size());
  Line::Options options;
  options.contents = str;
  options.modifiers = modifiers;
  contents_.AppendToLine(contents_.size(), Line(options));
  MaybeFollowToEndOfFile();
}

unique_ptr<Expression> OpenBuffer::CompileString(EditorState*,
                                                 const wstring& code,
                                                 wstring* error_description) {
  return afc::vm::CompileString(code, &environment_, error_description);
}

void OpenBuffer::EvaluateExpression(
    EditorState*, Expression* expr,
    std::function<void(std::unique_ptr<Value>)> consumer) {
  Evaluate(expr, &environment_, consumer);
}

bool OpenBuffer::EvaluateString(
    EditorState* editor_state, const wstring& code,
    std::function<void(std::unique_ptr<Value>)> consumer) {
  wstring error_description;
  LOG(INFO) << "Compiling code.";
  // TODO: Use unique_ptr and capture by value.
  std::shared_ptr<Expression> expression =
      CompileString(editor_state, code, &error_description);
  if (expression == nullptr) {
    editor_state->SetWarningStatus(L"Compilation error: " + error_description);
    return false;
  }
  LOG(INFO) << "Code compiled, evaluating.";
  EvaluateExpression(
      editor_state, expression.get(),
      [expression, consumer](Value::Ptr value) { consumer(std::move(value)); });
  return true;
}

bool OpenBuffer::EvaluateFile(EditorState* editor_state, const wstring& path) {
  wstring error_description;
  // TODO: Use unique_ptr and capture by value.
  std::shared_ptr<Expression> expression =
      CompileFile(ToByteString(path), &environment_, &error_description);
  if (expression == nullptr) {
    editor_state->SetStatus(path + L": error: " + error_description);
    return false;
  }
  Evaluate(expression.get(), &environment_,
           [expression](std::unique_ptr<Value>) {});
  return true;
}

void OpenBuffer::DeleteRange(const Range& range) {
  if (range.begin.line == range.end.line) {
    contents_.DeleteCharactersFromLine(range.begin.line, range.begin.column,
                                       range.end.column - range.begin.column);
  } else {
    contents_.DeleteCharactersFromLine(range.begin.line, range.begin.column);
    contents_.DeleteCharactersFromLine(range.end.line, 0, range.end.column);
    // Lines in the middle.
    EraseLines(range.begin.line + 1, range.end.line);
    contents_.FoldNextLine(range.begin.line);
  }
}

LineColumn OpenBuffer::InsertInPosition(const OpenBuffer& buffer,
                                        const LineColumn& input_position,
                                        const LineModifierSet* modifiers) {
  auto blocker = cursors_tracker_.DelayTransformations();
  if (buffer.empty()) {
    return input_position;
  }
  LineColumn position = input_position;
  if (empty()) {
    contents_.push_back(std::make_shared<Line>());
  }
  if (position.line >= contents_.size()) {
    position.line = contents_.size() - 1;
    position.column = contents_.at(position.line)->size();
  }
  if (position.column > contents_.at(position.line)->size()) {
    position.column = contents_.at(position.line)->size();
  }
  contents_.SplitLine(position);
  contents_.insert(position.line + 1, buffer.contents_, modifiers);
  contents_.FoldNextLine(position.line);

  size_t last_line = position.line + buffer.contents_.size() - 1;
  size_t column = LineAt(last_line)->size();

  contents_.FoldNextLine(last_line);
  return LineColumn(last_line, column);
}

void OpenBuffer::AdjustLineColumn(LineColumn* output) const {
  CHECK(!contents_.empty());
  output->line = min(output->line, contents_.size() - 1);
  CHECK(LineAt(output->line) != nullptr);
  output->column = min(LineAt(output->line)->size(), output->column);
}

void OpenBuffer::MaybeAdjustPositionCol() {
  if (contents_.empty() || current_line() == nullptr) {
    return;
  }
  size_t line_length = current_line()->size();
  if (position().column > line_length) {
    set_current_position_col(line_length);
  }
}

void OpenBuffer::MaybeExtendLine(LineColumn position) {
  CHECK_LT(position.line, contents_.size());
  auto line = std::make_shared<Line>(*LineAt(position.line));
  if (line->size() > position.column + 1) {
    return;
  }
  wstring padding(position.column - line->size() + 1, L' ');
  line->Append(Line(padding));
  contents_.set_line(position.line, line);
}

void OpenBuffer::CheckPosition() {
  if (position().line >= contents_.size()) {
    set_position(LineColumn(contents_.size()));
  }
}

CursorsSet* OpenBuffer::FindCursors(const wstring& name) {
  return cursors_tracker_.FindOrCreateCursors(name);
}

CursorsSet* OpenBuffer::active_cursors() {
  return FindCursors(editor_->modifiers().active_cursors);
}

void OpenBuffer::set_active_cursors(const vector<LineColumn>& positions) {
  if (positions.empty()) {
    return;
  }
  auto cursors = active_cursors();
  FindCursors(kOldCursors)->swap(*cursors);
  cursors->clear();
  cursors->insert(positions.begin(), positions.end());

  // We find the first position (rather than just take cursors->begin()) so that
  // we start at the first requested position.
  cursors_tracker_.SetCurrentCursor(cursors, positions.front());
  CHECK_LE(position().line, lines_size());

  editor_->ScheduleRedraw();
}

void OpenBuffer::ToggleActiveCursors() {
  LineColumn desired_position = position();

  auto cursors = active_cursors();
  FindCursors(kOldCursors)->swap(*cursors);

  // TODO: Maybe it'd be best to pick the nearest after the cursor?
  // TODO: This should probably be merged somewhat with set_active_cursors?
  for (auto it = cursors->begin(); it != cursors->end(); ++it) {
    if (desired_position == *it) {
      LOG(INFO) << "Desired position " << desired_position << " prevails.";
      cursors_tracker_.SetCurrentCursor(cursors, desired_position);
      CHECK_LE(position().line, lines_size());
      return;
    }
  }

  cursors_tracker_.SetCurrentCursor(cursors, *cursors->begin());
  LOG(INFO) << "Picked up the first cursor: " << position();
  CHECK_LE(position().line, contents_.size());

  editor_->ScheduleRedraw();
}

void OpenBuffer::PushActiveCursors() {
  auto stack_size = cursors_tracker_.Push();
  editor_->SetStatus(L"cursors stack (" + to_wstring(stack_size) + L"): +");
}

void OpenBuffer::PopActiveCursors() {
  auto stack_size = cursors_tracker_.Pop();
  if (stack_size == 0) {
    editor_->SetWarningStatus(L"cursors stack: -: Stack is empty!");
    return;
  }
  editor_->SetStatus(L"cursors stack (" + to_wstring(stack_size - 1) + L"): -");
}

void OpenBuffer::SetActiveCursorsToMarks() {
  const auto& marks = *GetLineMarks(*editor_);
  if (marks.empty()) {
    editor_->SetWarningStatus(L"Buffer has no marks!");
    return;
  }

  std::vector<LineColumn> cursors;
  for (auto& it : marks) {
    cursors.push_back(it.second.target);
  }
  set_active_cursors(cursors);
}

void OpenBuffer::set_current_cursor(CursorsSet::value_type new_value) {
  auto cursors = active_cursors();
  // Need to do find + erase because cursors is a multiset; we only want to
  // erase one cursor, rather than all cursors with the current value.
  auto it = cursors->find(position());
  if (it != cursors->end()) {
    cursors->erase(it);
  }
  cursors->insert(new_value);
  cursors_tracker_.SetCurrentCursor(cursors, new_value);
  CHECK_LE(position().line, contents_.size());
}

void OpenBuffer::CreateCursor() {
  switch (editor_->modifiers().structure) {
    case WORD:
    case LINE: {
      auto structure = editor_->modifiers().structure;
      LineColumn first, last;
      Modifiers tmp_modifiers = editor_->modifiers();
      tmp_modifiers.structure = CURSOR;
      if (!FindPartialRange(tmp_modifiers, position(), &first, &last)) {
        return;
      }
      if (first == last) {
        return;
      }
      editor_->set_direction(FORWARDS);
      LOG(INFO) << "Range for cursors: [" << first << ", " << last << ")";
      while (first < last) {
        auto tmp_first = first;
        SeekToStructure(structure, FORWARDS, &tmp_first);
        if (tmp_first > first && tmp_first < last) {
          VLOG(5) << "Creating cursor at: " << tmp_first;
          active_cursors()->insert(tmp_first);
        }
        if (!SeekToLimit(structure, FORWARDS, &tmp_first)) {
          break;
        }
        first = tmp_first;
      }
    } break;
    default:
      CHECK_LE(position().line, contents_.size());
      active_cursors()->insert(position());
  }
  editor_->SetStatus(L"Cursor created.");
  editor_->ScheduleRedraw();
}

CursorsSet::iterator OpenBuffer::FindPreviousCursor(LineColumn position) {
  LOG(INFO) << "Visiting previous cursor: " << editor_->modifiers();
  editor_->set_direction(ReverseDirection(editor_->modifiers().direction));
  return FindNextCursor(position);
}

CursorsSet::iterator OpenBuffer::FindNextCursor(LineColumn position) {
  LOG(INFO) << "Visiting next cursor: " << editor_->modifiers();
  auto direction = editor_->modifiers().direction;
  auto cursors = active_cursors();
  if (cursors->empty()) {
    return cursors->end();
  }

  size_t index = 0;
  auto output = cursors->begin();
  while (output != cursors->end() &&
         (*output < position || (direction == FORWARDS && *output == position &&
                                 std::next(output) != cursors->end() &&
                                 *std::next(output) == position))) {
    ++output;
    ++index;
  }

  size_t repetitions = editor_->modifiers().repetitions % cursors->size();
  size_t final_position;  // From cursors->begin().
  if (direction == FORWARDS) {
    final_position = (index + repetitions) % cursors->size();
  } else if (index >= repetitions) {
    final_position = index - repetitions;
  } else {
    final_position = cursors->size() - (repetitions - index);
  }
  output = cursors->begin();
  std::advance(output, final_position);
  return output;
}

void OpenBuffer::DestroyCursor() {
  auto cursors = active_cursors();
  if (cursors->size() <= 1) {
    return;
  }
  size_t repetitions =
      min(editor_->modifiers().repetitions, cursors->size() - 1);
  for (size_t i = 0; i < repetitions; i++) {
    cursors_tracker_.DeleteCurrentCursor(cursors);
  }
  CHECK_LE(position().line, contents_.size());
  editor_->ScheduleRedraw();
}

void OpenBuffer::DestroyOtherCursors() {
  CheckPosition();
  auto position = this->position();
  CHECK_LE(position, contents_.size());
  auto cursors = active_cursors();
  cursors->clear();
  cursors->insert(position);
  cursors_tracker_.SetCurrentCursor(cursors, position);
  set_bool_variable(buffer_variables::multiple_cursors(), false);
  editor_->ScheduleRedraw();
}

void OpenBuffer::SeekToStructure(Structure structure, Direction direction,
                                 LineColumn* position) {
  switch (structure) {
    case BUFFER:
    case CURSOR:
    case CHAR:
    case TREE:
      break;

    case PARAGRAPH:
      Seek(contents_, position)
          .WithDirection(direction)
          .UntilNextLineIsNotSubsetOf(
              Read(buffer_variables::line_prefix_characters()));
      break;

    case LINE:
      Seek(contents_, position)
          .WithDirection(direction)
          .WrappingLines()
          .UntilCurrentCharNotIn(L"\n");
      break;

    case WORD:
      Seek(contents_, position)
          .WithDirection(direction)
          .WrappingLines()
          .UntilCurrentCharIn(Read(buffer_variables::word_characters()));
  }
}

bool OpenBuffer::SeekToLimit(Structure structure, Direction direction,
                             LineColumn* position) {
  if (empty()) {
    *position = LineColumn();
  } else {
    position->line = min(lines_size() - 1, position->line);
    if (position->column >= LineAt(position->line)->size()) {
      if (Read(buffer_variables::extend_lines())) {
        MaybeExtendLine(*position);
      } else {
        position->column = LineAt(position->line)->size();
      }
    }
  }
  switch (structure) {
    case BUFFER:
      if (empty() || direction == BACKWARDS) {
        *position = LineColumn();
      } else {
        position->line = lines_size() - 1;
        position->column = LineAt(position->line)->size();
      }
      return false;
      break;

    case CHAR:
      return Seek(contents_, position)
                 .WrappingLines()
                 .WithDirection(direction)
                 .Once() == Seek::DONE;
      break;

    case TREE: {
      auto root = parse_tree();
      if (root == nullptr) {
        return false;
      }
      auto route = MapRoute(*root, FindRouteToPosition(*root, *position));
      if (tree_depth() <= 0 || route.size() <= tree_depth() - 1) {
        return false;
      }
      bool has_boundary = false;
      LineColumn boundary;
      for (const auto& candidate : route[tree_depth() - 1]->children) {
        if (direction == FORWARDS) {
          if (candidate.range.begin > *position &&
              (!has_boundary || candidate.range.begin < boundary)) {
            boundary = candidate.range.begin;
            has_boundary = true;
          }
        } else {
          if (candidate.range.end < *position &&
              (!has_boundary || candidate.range.end > boundary)) {
            boundary = candidate.range.end;
            has_boundary = true;
          }
        }
      }
      if (!has_boundary) {
        return false;
      }
      if (direction == BACKWARDS) {
        Seek(contents_, &boundary).WithDirection(direction).Once();
      }
      *position = boundary;
    } break;

    case PARAGRAPH:
      return Seek(contents_, position)
                 .WithDirection(direction)
                 .WrappingLines()
                 .UntilNextLineIsSubsetOf(Read(
                     buffer_variables::line_prefix_characters())) == Seek::DONE;
      break;

    case LINE:
      if (direction == BACKWARDS) {
        position->column = 0;
        return Seek(contents_, position).WrappingLines().Backwards().Once() ==
               Seek::DONE;
      }
      position->column = LineAt(position->line)->size();
      return true;
      break;

    case WORD: {
      return Seek(contents_, position)
                 .WithDirection(direction)
                 .WrappingLines()
                 .UntilCurrentCharNotIn(
                     Read(buffer_variables::word_characters())) == Seek::DONE;
    } break;

    case CURSOR: {
      bool has_boundary = false;
      LineColumn boundary;
      auto cursors = cursors_tracker_.FindCursors(L"");
      CHECK(cursors != nullptr);
      for (const auto& candidate : *cursors) {
        if (direction == FORWARDS ? (candidate > *position &&
                                     (!has_boundary || candidate < boundary))
                                  : (candidate < *position &&
                                     (!has_boundary || candidate > boundary))) {
          boundary = candidate;
          has_boundary = true;
        }
      }

      if (!has_boundary) {
        return false;
      }
      if (direction == BACKWARDS) {
        Seek(contents_, &boundary).WithDirection(direction).Once();
      }
      *position = boundary;
    }
  }
  return false;
}

bool OpenBuffer::FindPartialRange(const Modifiers& modifiers,
                                  const LineColumn& initial_position,
                                  LineColumn* start, LineColumn* end) {
  const auto forward = modifiers.direction;
  const auto backward = ReverseDirection(forward);

  LineColumn position = initial_position;
  if (!empty()) {
    position.line = min(lines_size() - 1, position.line);
    if (position.column > LineAt(position.line)->size()) {
      if (Read(buffer_variables::extend_lines())) {
        MaybeExtendLine(position);
      } else {
        position.column = LineAt(position.line)->size();
      }
    }
  }
  if (modifiers.direction == BACKWARDS) {
    Seek(contents_, &position).Backwards().WrappingLines().Once();
  }

  *start = position;
  LOG(INFO) << "Initial position: " << position;
  SeekToStructure(modifiers.structure, forward, start);
  switch (modifiers.boundary_begin) {
    case Modifiers::CURRENT_POSITION:
      *start = modifiers.direction == FORWARDS ? max(position, *start)
                                               : min(position, *start);
      break;

    case Modifiers::LIMIT_CURRENT: {
      if (SeekToLimit(modifiers.structure, backward, start)) {
        Seek(contents_, start).WrappingLines().WithDirection(forward).Once();
      }
    } break;

    case Modifiers::LIMIT_NEIGHBOR:
      if (SeekToLimit(modifiers.structure, backward, start)) {
        SeekToStructure(modifiers.structure, backward, start);
        SeekToLimit(modifiers.structure, forward, start);
      }
  }
  LOG(INFO) << "After seek, initial position: " << *start;
  *end = modifiers.direction == FORWARDS ? max(position, *start)
                                         : min(position, *start);
  bool move_start = true;
  for (size_t i = 0; i < modifiers.repetitions - 1; i++) {
    if (!SeekToLimit(modifiers.structure, forward, end)) {
      move_start = false;
      break;
    }
    SeekToStructure(modifiers.structure, forward, end);
  }

  switch (modifiers.boundary_end) {
    case Modifiers::CURRENT_POSITION:
      break;

    case Modifiers::LIMIT_CURRENT:
      move_start &= SeekToLimit(modifiers.structure, forward, end);
      break;

    case Modifiers::LIMIT_NEIGHBOR:
      move_start &= SeekToLimit(modifiers.structure, forward, end);
      SeekToStructure(modifiers.structure, forward, end);
  }
  LOG(INFO) << "After adjusting end: " << *start << " to " << *end;

  if (*start > *end) {
    CHECK(modifiers.direction == BACKWARDS);
    auto tmp = *end;
    *end = *start;
    *start = tmp;
    if (move_start) {
      Seek(contents_, start).WrappingLines().Once();
    }
  }
  LOG(INFO) << "After wrap: " << *start << " to " << *end;
  return true;
}

const ParseTree* OpenBuffer::current_tree(const ParseTree* root) const {
  auto route = FindRouteToPosition(*root, position());
  if (route.size() < tree_depth_) {
    return root;
  }
  if (route.size() > tree_depth_) {
    route.resize(tree_depth_);
  }
  return FollowRoute(*root, route);
}

void OpenBuffer::set_lines_for_zoomed_out_tree(size_t lines) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (lines == lines_for_zoomed_out_tree_) {
    return;
  }

  lines_for_zoomed_out_tree_ = lines;
  zoomed_out_tree_ = nullptr;
  lock.unlock();

  editor_->ScheduleParseTreeUpdate(this);
}

std::shared_ptr<const ParseTree> OpenBuffer::zoomed_out_tree() const {
  std::unique_lock<std::mutex> lock(mutex_);
  return zoomed_out_tree_;
}

const shared_ptr<const Line> OpenBuffer::current_line() const {
  if (position().line >= contents_.size()) {
    return nullptr;
  }
  return contents_.at(position().line);
}

std::shared_ptr<OpenBuffer> OpenBuffer::GetBufferFromCurrentLine() {
  if (contents()->empty() || current_line() == nullptr) {
    return nullptr;
  }
  auto target = current_line()->environment()->Lookup(L"buffer");
  if (target == nullptr || target->type.type != VMType::OBJECT_TYPE ||
      target->type.object_type != L"Buffer") {
    return nullptr;
  }
  return std::static_pointer_cast<OpenBuffer>(target->user_value);
}

wstring OpenBuffer::ToString() const { return contents_.ToString(); }

void OpenBuffer::PushSignal(EditorState* editor_state, int sig) {
  switch (sig) {
    case SIGINT:
      if (Read(buffer_variables::pts())) {
        string sequence(1, 0x03);
        (void)write(fd_.fd, sequence.c_str(), sequence.size());
        editor_state->SetStatus(L"SIGINT");
      } else if (child_pid_ != -1) {
        editor_state->SetStatus(L"SIGINT >> pid:" + to_wstring(child_pid_));
        kill(child_pid_, sig);
      }
      break;

    case SIGTSTP:
      if (Read(buffer_variables::pts())) {
        string sequence(1, 0x1a);
        write(fd_.fd, sequence.c_str(), sequence.size());
      }
      break;

    default:
      editor_state->SetStatus(L"Unexpected signal received: " +
                              to_wstring(sig));
  }
}

wstring OpenBuffer::TransformKeyboardText(wstring input) {
  using afc::vm::VMType;
  for (Value::Ptr& t : keyboard_text_transformers_) {
    vector<Value::Ptr> args;
    args.push_back(Value::NewString(std::move(input)));
    Call(t.get(), std::move(args),
         [&input](Value::Ptr value) { input = std::move(value->str); });
  }
  return input;
}

bool OpenBuffer::AddKeyboardTextTransformer(EditorState* editor_state,
                                            unique_ptr<Value> transformer) {
  if (transformer == nullptr || transformer->type.type != VMType::FUNCTION ||
      transformer->type.type_arguments.size() != 2 ||
      transformer->type.type_arguments[0].type != VMType::VM_STRING ||
      transformer->type.type_arguments[1].type != VMType::VM_STRING) {
    editor_state->SetStatus(
        L": Unexpected type for keyboard text transformer: " +
        transformer->type.ToString());
    return false;
  }
  keyboard_text_transformers_.push_back(std::move(transformer));
  return true;
}

void OpenBuffer::Input::Reset() {
  low_buffer = nullptr;
  low_buffer_length = 0;
}

void OpenBuffer::Input::Close() {
  if (fd != -1) {
    close(fd);
    fd = -1;
  }
}

void OpenBuffer::SetInputFiles(EditorState* editor_state, int input_fd,
                               int input_error_fd, bool fd_is_terminal,
                               pid_t child_pid) {
  if (Read(buffer_variables::clear_on_reload())) {
    ClearContents(editor_state);
    ClearModified();
    fd_.Reset();
    fd_error_.Reset();
  }

  fd_.Close();
  fd_.fd = input_fd;

  fd_error_.Close();
  fd_error_.fd = input_error_fd;
  fd_error_.modifiers.clear();
  fd_error_.modifiers.insert(LineModifier::RED);

  CHECK_EQ(child_pid_, -1);
  fd_is_terminal_ = fd_is_terminal;
  child_pid_ = child_pid;
}

size_t OpenBuffer::current_position_line() const { return position().line; }

void OpenBuffer::set_current_position_line(size_t line) {
  set_current_cursor(LineColumn(min(line, contents_.size())));
}

size_t OpenBuffer::current_position_col() const { return position().column; }

void OpenBuffer::set_current_position_col(size_t column) {
  set_current_cursor(LineColumn(position().line, column));
}

const LineColumn OpenBuffer::position() const {
  return cursors_tracker_.position();
}

void OpenBuffer::set_position(const LineColumn& position) {
  if (!IsPastPosition(position)) {
    VLOG(5) << "Setting desired_position_: " << position;
    desired_position_ = position;
  }
  set_current_cursor(
      LineColumn(min(position.line, contents_.size()), position.column));
}

wstring OpenBuffer::FlagsString() const {
  wstring output;
  if (modified()) {
    output += L"~";
  }
  if (fd() != -1) {
    output += L"< l:" + to_wstring(contents_.size());
    if (Read(buffer_variables::follow_end_of_file())) {
      output += L" ↓";
    }
    wstring pts_path = Read(buffer_variables::pts_path());
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

  auto marks = GetLineMarksText(*editor_);
  if (!marks.empty()) {
    output += L" " + marks;
  }

  return output;
}

const bool& OpenBuffer::Read(const EdgeVariable<bool>* variable) const {
  return bool_variables_.Get(variable);
}

void OpenBuffer::set_bool_variable(const EdgeVariable<bool>* variable,
                                   bool value) {
  bool_variables_.Set(variable, value);
}

void OpenBuffer::toggle_bool_variable(const EdgeVariable<bool>* variable) {
  set_bool_variable(variable, !Read(variable));
}

const wstring& OpenBuffer::Read(const EdgeVariable<wstring>* variable) const {
  return string_variables_.Get(variable);
}

void OpenBuffer::set_string_variable(const EdgeVariable<wstring>* variable,
                                     wstring value) {
  string_variables_.Set(variable, value);

  // TODO: This should be in the variable definition, not here. Ugh.
  if (variable == buffer_variables::word_characters() ||
      variable == buffer_variables::tree_parser() ||
      variable == buffer_variables::language_keywords() ||
      variable == buffer_variables::typos()) {
    UpdateTreeParser();
  }
}

const int& OpenBuffer::Read(const EdgeVariable<int>* variable) const {
  return int_variables_.Get(variable);
}

void OpenBuffer::set_int_variable(const EdgeVariable<int>* variable,
                                  int value) {
  int_variables_.Set(variable, value);
}

const double& OpenBuffer::Read(const EdgeVariable<double>* variable) const {
  return double_variables_.Get(variable);
}

void OpenBuffer::set_double_variable(const EdgeVariable<double>* variable,
                                     double value) {
  double_variables_.Set(variable, value);
}

void OpenBuffer::ApplyToCursors(unique_ptr<Transformation> transformation) {
  ApplyToCursors(std::move(transformation),
                 Read(buffer_variables::multiple_cursors())
                     ? Modifiers::AFFECT_ALL_CURSORS
                     : Modifiers::AFFECT_ONLY_CURRENT_CURSOR);
}

void OpenBuffer::ApplyToCursors(unique_ptr<Transformation> transformation,
                                Modifiers::CursorsAffected cursors_affected) {
  CHECK(transformation != nullptr);

  if (!last_transformation_stack_.empty()) {
    CHECK(last_transformation_stack_.back() != nullptr);
    last_transformation_stack_.back()->PushBack(transformation->Clone());
  }

  transformations_past_.push_back(
      std::make_unique<Transformation::Result>(editor_));

  transformations_past_.back()->undo_stack->PushFront(
      NewSetCursorsTransformation(*active_cursors(), position()));

  for (auto& position : *active_cursors()) {
    CHECK_LE(position.line, contents_.size());
  }

  if (cursors_affected == Modifiers::AFFECT_ALL_CURSORS) {
    CursorsSet single_cursor;
    CursorsSet* cursors = active_cursors();
    CHECK(cursors != nullptr);
    cursors_tracker_.ApplyTransformationToCursors(
        cursors, [this, &transformation](LineColumn old_position) {
          transformations_past_.back()->cursor = old_position;
          auto new_position = Apply(editor_, transformation->Clone());
          CHECK_LE(new_position.line, contents_.size());
          return new_position;
        });
    CHECK_LE(position().line, lines_size());
  } else {
    transformations_past_.back()->cursor = position();
    auto new_position = Apply(editor_, transformation->Clone());
    VLOG(6) << "Adjusting default cursor (!multiple_cursors).";
    cursors_tracker_.MoveCurrentCursor(active_cursors(), new_position);
    CHECK_LE(position().line, lines_size());
  }

  transformations_future_.clear();
  if (transformations_past_.back()->modified_buffer) {
    editor_->StartHandlingInterrupts();
    last_transformation_ = std::move(transformation);
  }
}

LineColumn OpenBuffer::Apply(EditorState* editor_state,
                             unique_ptr<Transformation> transformation) {
  CHECK(transformation != nullptr);
  CHECK(!transformations_past_.empty());

  transformation->Apply(editor_state, this, transformations_past_.back().get());
  CHECK(!transformations_past_.empty());

  auto delete_buffer = transformations_past_.back()->delete_buffer;
  CHECK(delete_buffer != nullptr);
  if ((delete_buffer->contents()->size() > 1 ||
       delete_buffer->LineAt(0)->size() > 0) &&
      Read(buffer_variables::delete_into_paste_buffer())) {
    auto insert_result = editor_state->buffers()->insert(
        make_pair(delete_buffer->name(), delete_buffer));
    if (!insert_result.second) {
      insert_result.first->second = delete_buffer;
    }
  }

  return transformations_past_.back()->cursor;
}

void OpenBuffer::RepeatLastTransformation() {
  for (size_t i = 0; i < editor_->repetitions(); i++) {
    ApplyToCursors(last_transformation_->Clone());
  }
}

void OpenBuffer::PushTransformationStack() {
  last_transformation_stack_.emplace_back(
      std::make_unique<TransformationStack>());
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
  Undo(editor_state, SKIP_IRRELEVANT);
};

void OpenBuffer::Undo(EditorState* editor_state, UndoMode undo_mode) {
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
      target->emplace_back(
          std::make_unique<Transformation::Result>(editor_state));
      source->back()->undo_stack->Apply(editor_state, this,
                                        target->back().get());
      source->pop_back();
      modified_buffer =
          target->back()->modified_buffer || undo_mode == ONLY_UNDO_THE_LAST;
    }
    if (source->empty()) {
      return;
    }
  }
}

void OpenBuffer::set_filter(unique_ptr<Value> filter) {
  filter_ = std::move(filter);
  filter_version_++;
}

bool OpenBuffer::IsLineFiltered(size_t line_number) {
  if (line_number >= contents_.size()) {
    return true;
  }
  const auto& old_line = *LineAt(line_number);
  if (old_line.filter_version() >= filter_version_) {
    return old_line.filtered();
  }

  bool filtered;
  vector<Value::Ptr> args;
  args.push_back(Value::NewString(old_line.ToString()));
  Call(filter_.get(), std::move(args),
       [&filtered](Value::Ptr value) { filtered = value->boolean; });

  auto new_line = std::make_shared<Line>(old_line);
  new_line->set_filtered(filtered, filter_version_);
  contents_.set_line(line_number, new_line);
  return filtered;
}

const multimap<size_t, LineMarks::Mark>* OpenBuffer::GetLineMarks(
    const EditorState& editor_state) const {
  auto marks = editor_state.line_marks();
  if (marks->updates > line_marks_last_updates_) {
    LOG(INFO) << name_ << ": Updating marks.";
    line_marks_.clear();
    auto relevant_marks = marks->GetMarksForTargetBuffer(name_);
    for (auto& mark : relevant_marks) {
      line_marks_.insert(make_pair(mark.target.line, mark));
    }
    line_marks_last_updates_ = marks->updates;
  }
  VLOG(10) << "Returning multimap with size: " << line_marks_.size();
  return &line_marks_;
}

wstring OpenBuffer::GetLineMarksText(const EditorState& editor_state) const {
  const auto* marks = GetLineMarks(editor_state);
  wstring output;
  if (!marks->empty()) {
    size_t expired_marks = 0;
    for (const auto& mark : *marks) {
      if (mark.second.IsExpired()) {
        expired_marks++;
      }
    }
    CHECK_LE(expired_marks, marks->size());
    output = L"marks:" + to_wstring(marks->size() - expired_marks);
    if (expired_marks > 0) {
      output += L"(" + to_wstring(expired_marks) + L")";
    }
  }
  return output;
}

bool OpenBuffer::IsPastPosition(LineColumn position) const {
  return position != LineColumn::Max() &&
         (position.line + 1 < contents_.size() ||
          (position.line + 1 == contents_.size() &&
           position.column <= LineAt(position.line)->size()));
}

}  // namespace editor
}  // namespace afc

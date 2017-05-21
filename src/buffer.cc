#include "buffer.h"

#include <cassert>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <sstream>
#include <stdexcept>
#include <string>

extern "C" {
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "char_buffer.h"
#include "cpp_parse_tree.h"
#include "cursors_transformation.h"
#include "dirname.h"
#include "editor.h"
#include "file_link_mode.h"
#include "run_command_handler.h"
#include "lazy_string_append.h"
#include "line_marks.h"
#include "src/seek.h"
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

static const wchar_t* kOldCursors = L"old-cursors";

using std::unordered_set;

bool FromVmBool(const Value& value) { return value.boolean; }
wstring FromVmString(const Value& value) { return value.str; }
int FromVmInt(const Value& value) { return value.integer; }

template <typename EdgeStruct, typename FieldValue>
void RegisterBufferFields(
    EditorState* editor_state,
    EdgeStruct* edge_struct,
    afc::vm::ObjectType* object_type,
    const FieldValue& (OpenBuffer::*reader)(
        const EdgeVariable<FieldValue>*) const,
    void (OpenBuffer::*setter)(const EdgeVariable<FieldValue>*, FieldValue),
    std::unique_ptr<Value> (*to_vm_value)(FieldValue),
    FieldValue (*from_vm_value)(const Value& value)) {
  VMType buffer_type = VMType::ObjectType(object_type);

  vector<wstring> variable_names;
  edge_struct->RegisterVariableNames(&variable_names);
  for (const wstring& name : variable_names) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    VMType field_type = to_vm_value(variable->default_value())->type;

    // Getter.
    {
      unique_ptr<Value> callback(new Value(VMType::FUNCTION));
      callback->type.type_arguments.push_back(field_type);
      callback->type.type_arguments.push_back(buffer_type);
      callback->callback =
          [variable, reader, to_vm_value](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), 1);
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
            CHECK(buffer != nullptr);
            return to_vm_value((buffer->*reader)(variable));
          };
      object_type->AddField(variable->name(), std::move(callback));
    }

    // Setter.
    {
      unique_ptr<Value> callback(new Value(VMType::FUNCTION));
      callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
      callback->type.type_arguments.push_back(buffer_type);
      callback->type.type_arguments.push_back(field_type);
      callback->callback =
          [editor_state, field_type, variable, setter, from_vm_value](
              vector<unique_ptr<Value>> args) {
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
            CHECK(buffer != nullptr);

            CHECK_EQ(args[1]->type, field_type);
            (buffer->*setter)(variable, from_vm_value(*args[1]));
            editor_state->ScheduleRedraw();
            return Value::NewVoid();
          };
      object_type->AddField(L"set_" + variable->name(), std::move(callback));
    }
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

  RegisterBufferFields(
      editor_state, BoolStruct(), buffer.get(),
      &OpenBuffer::read_bool_variable, &OpenBuffer::set_bool_variable,
      &Value::NewBool, &FromVmBool);

  RegisterBufferFields(
      editor_state, StringStruct(), buffer.get(),
      &OpenBuffer::read_string_variable, &OpenBuffer::set_string_variable,
      &Value::NewString, &FromVmString);

  RegisterBufferFields(
      editor_state, IntStruct(), buffer.get(),
      &OpenBuffer::Read, &OpenBuffer::set_int_variable,
      &Value::NewInteger, &FromVmInt);

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
          CHECK_EQ(args.size(), 2);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          CHECK_EQ(args[1]->type, VMType::VM_INTEGER);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          CHECK(buffer != nullptr);
          auto line = min(static_cast<size_t>(max(args[1]->integer, 0)),
                          buffer->contents()->size() - 1);
          return Value::NewString(buffer->contents()->at(line)->ToString());
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
          size_t line = 0;
          unique_ptr<TransformationStack> transformation(
              new TransformationStack());
          while (line + 1 < buffer->contents()->size()) {
            wstring current_line = buffer->LineAt(line)->ToString();
            vector<unique_ptr<Value>> line_args;
            line_args.push_back(Value::NewString(current_line));
            unique_ptr<Value> result = args[1]->callback(std::move(line_args));
            if (result->str != current_line) {
              DeleteOptions options;
              options.copy_to_paste_buffer = false;
              transformation->PushBack(NewDeleteLinesTransformation(options));
              shared_ptr<OpenBuffer> buffer_to_insert(
                  new OpenBuffer(editor_state, L"tmp buffer"));
              buffer_to_insert->AppendLine(
                  editor_state, NewCopyString(result->str));
              transformation->PushBack(
                  NewInsertBufferTransformation(buffer_to_insert, 1, END));
            }
            line++;
          }
          buffer->Apply(editor_state, std::move(transformation));
          return Value::NewVoid();
        };
    buffer->AddField(L"Map", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_BOOLEAN));
    callback->type.type_arguments.push_back(VMType::ObjectType(buffer.get()));

    VMType function_type(VMType::FUNCTION);
    function_type.type_arguments.push_back(VMType(VMType::VM_STRING));
    function_type.type_arguments.push_back(VMType(VMType::VM_STRING));

    callback->type.type_arguments.push_back(function_type);

    callback->callback =
        [editor_state, function_type](vector<unique_ptr<Value>> args) {
          CHECK_EQ(args.size(), 2);
          CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
          auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
          CHECK(buffer != nullptr);
          CHECK_EQ(args[1]->type, function_type);
          return Value::NewBool(buffer->AddKeyboardTextTransformer(
              editor_state, std::move(args[1])));
        };
    buffer->AddField(L"AddKeyboardTextTransformer", std::move(callback));
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

          DeleteOptions options;
          options.modifiers.repetitions = args[1]->integer;
          buffer->ApplyToCursors(NewDeleteCharactersTransformation(options));
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
          bool insert_separator = false;
          while (std::getline(text_stream, line, wchar_t('\n'))) {
            if (insert_separator) {
              buffer_to_insert->AppendEmptyLine(editor_state);
            } else {
              insert_separator = true;
            }
            buffer_to_insert->AppendToLastLine(
                editor_state, NewCopyString(line));
          }

          buffer->ApplyToCursors(
              NewInsertBufferTransformation(buffer_to_insert, 1, END));

          return Value::NewVoid();
        };
    buffer->AddField(L"InsertText", std::move(callback));
  }

  environment->DefineType(L"Buffer", std::move(buffer));
}

void OpenBuffer::BackgroundThread() {
  while (true) {
    std::unique_lock<std::mutex> lock(mutex_);
    background_condition_.wait(lock,
        [this]() {
          return background_thread_shutting_down_ ||
                 contents_to_parse_ != nullptr;
        });
    VLOG(5) << "Background thread is waking up.";
    if (background_thread_shutting_down_) {
      return;
    }
    std::unique_ptr<const BufferContents> contents =
        std::move(contents_to_parse_);
    CHECK(contents_to_parse_ == nullptr);
    auto parser = tree_parser_;
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

    std::unique_lock<std::mutex> final_lock(mutex_);
    parse_tree_ = parse_tree;
    simplified_parse_tree_ = simplified_parse_tree;
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
      bool_variables_(BoolStruct()->NewInstance()),
      string_variables_(StringStruct()->NewInstance()),
      int_variables_(IntStruct()->NewInstance()),
      environment_(editor_state->environment()),
      filter_version_(0),
      last_transformation_(NewNoopTransformation()),
      parse_tree_(std::make_shared<ParseTree>()),
      tree_parser_(NewNullTreeParser()) {
  contents_.AddUpdateListener(
      [this](const CursorsTracker::Transformation& transformation) {
        editor_->ScheduleParseTreeUpdate(this);
        modified_ = true;
        time(&last_action_);
        cursors_tracker_.AdjustCursors(transformation);
      });
  UpdateTreeParser();

  environment_.Define(L"buffer", Value::NewObject(
      L"Buffer", shared_ptr<void>(this, [](void*){})));

  set_string_variable(variable_path(), L"");
  set_string_variable(variable_pts_path(), L"");
  set_string_variable(variable_command(), L"");
  set_bool_variable(variable_reload_after_exit(), false);
  if (name_ == kPasteBuffer) {
    set_bool_variable(variable_allow_dirty_delete(), true);
    set_bool_variable(variable_show_in_buffers_list(), false);
    set_bool_variable(variable_delete_into_paste_buffer(), false);
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
  if (read_bool_variable(variable_save_on_close())) {
    LOG(INFO) << name_ << ": attempting to save buffer.";
    // TODO(alejo): Let Save give us status?
    Save(editor_state);
    if (!dirty()) {
      LOG(INFO) << name_ << ": successful save.";
      return true;
    }
  }
  if (read_bool_variable(variable_allow_dirty_delete())) {
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
  if (dirty() && read_bool_variable(variable_save_on_close())) {
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
  if (read_bool_variable(variable_reload_on_enter())) {
    Reload(editor_state);
    CheckPosition();
  }
  time(&last_visit_);
  time(&last_action_);
}

time_t OpenBuffer::last_visit() const { return last_visit_; }
time_t OpenBuffer::last_action() const { return last_action_; }

bool OpenBuffer::PersistState() const {
  return true;
}

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
      editor_state->SetStatus(
          L"waitpid failed: " + FromByteString(strerror(errno)));
      return;
    }
  }

  // We can remove expired marks now. We know that the set of fresh marks is now
  // complete.
  editor_state->line_marks()->RemoveExpiredMarksFromSource(name_);
  editor_state->ScheduleRedraw();

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

  vector<std::function<void()>> observers;
  observers.swap(end_of_file_observers_);
  for (auto& observer : observers) {
    observer();
  }
}

void OpenBuffer::MaybeFollowToEndOfFile() {
  if (IsPastPosition(desired_position_)) {
    VLOG(5) << "desired_position_ is realized: " << desired_position_;
    set_position(desired_position_);
    desired_position_ = LineColumn::Max();
  }
  if (!read_bool_variable(variable_follow_end_of_file())) { return; }
  if (read_bool_variable(variable_pts())) {
    set_position(position_pts_);
  } else {
    set_position(LineColumn(contents_.size()));
  }
}

bool OpenBuffer::ShouldDisplayProgress() const {
  return fd_.fd !=-1 || fd_error_.fd != -1;
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

void OpenBuffer::Input::ReadData(
    EditorState* editor_state, OpenBuffer* target) {
  LOG(INFO) << "Reading input from " << fd << " for buffer " << target->name();
  static const size_t kLowBufferSize = 1024 * 60;
  if (low_buffer == nullptr) {
    CHECK_EQ(low_buffer_length, 0);
    low_buffer.reset(new char[kLowBufferSize]);
  }
  ssize_t characters_read = read(fd, low_buffer.get() + low_buffer_length,
                                 kLowBufferSize - low_buffer_length);
  LOG(INFO) << "Read returns: " << characters_read;
  if (characters_read == -1) {
    if (errno == EAGAIN) {
      return;
    }
    editor_state->RegisterProgress();
    Close();
    Reset();
    if (target->fd_.fd == -1 && target->fd_error_.fd == -1) {
      target->EndOfFile(editor_state);
    }
    return;
  }
  CHECK_GE(characters_read, 0);
  CHECK_LE(characters_read, kLowBufferSize - low_buffer_length);
  if (characters_read == 0) {
    Close();
    Reset();
    editor_state->RegisterProgress();
    if (target->fd_.fd == -1 && target->fd_error_.fd == -1) {
      target->EndOfFile(editor_state);
    }
    return;
  }
  low_buffer_length += characters_read;

  const char* low_buffer_tmp = low_buffer.get();
  int output_characters =
      mbsnrtowcs(nullptr, &low_buffer_tmp, low_buffer_length, 0, nullptr);
  std::vector<wchar_t> buffer(
      output_characters == -1 ? low_buffer_length : output_characters);

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
  VLOG(5) << target->name() << ": Characters produced: "
          << buffer_wrapper->size();
  CHECK_LE(processed, low_buffer_length);
  memmove(low_buffer.get(), low_buffer_tmp, low_buffer_length - processed);
  low_buffer_length -= processed;
  if (low_buffer_length == 0) {
    LOG(INFO) << "Consumed all input.";
    low_buffer = nullptr;
  }

  if (target->read_bool_variable(OpenBuffer::variable_vm_exec())) {
    LOG(INFO) << target->name() << "Evaluating VM code: "
              << buffer_wrapper->ToString();
    target->EvaluateString(editor_state, buffer_wrapper->ToString());
  }

  editor_state->RegisterProgress();
  bool previous_modified = target->modified();
  if (target->read_bool_variable(OpenBuffer::variable_pts())) {
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
        if (editor_state->has_current_buffer()
            && editor_state->current_buffer()->second.get() == target
            && target->contents()->size() <=
                   target->Read(OpenBuffer::variable_view_start_line())
                   + editor_state->visible_lines()) {
          editor_state->ScheduleRedraw();
        }
      }
    }
    if (line_start < buffer_wrapper->size()) {
      VLOG(8) << "Adding last line from " << line_start << " to "
              << buffer_wrapper->size();
      auto line = Substring(buffer_wrapper, line_start,
                            buffer_wrapper->size() - line_start);
      target->AppendToLastLine(
          editor_state, line, ModifiersVector(modifiers, line->size()));
    }
  }
  if (!previous_modified) {
    target->ClearModified();  // These changes don't count.
  }
  if (editor_state->has_current_buffer()
      && editor_state->current_buffer()->first == kBuffersName) {
    editor_state->current_buffer()->second->Reload(editor_state);
  }
  editor_state->ScheduleRedraw();
}

void OpenBuffer::UpdateTreeParser() {
  auto parser = read_string_variable(variable_tree_parser());
  std::unique_lock<std::mutex> lock(mutex_);
  if (parser == L"text") {
    tree_parser_ = NewLineTreeParser(NewWordsTreeParser(
        read_string_variable(variable_word_characters()),
        NewNullTreeParser()));
  } else if (parser == L"cpp") {
    std::wistringstream keywords(
        read_string_variable(variable_language_keywords()));
    tree_parser_ = NewCppTreeParser(std::unordered_set<wstring>(
        std::istream_iterator<wstring, wchar_t>(keywords),
        std::istream_iterator<wstring, wchar_t>()));
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

    wstring path;
    LineColumn position;
    wstring pattern;
    if (read_bool_variable(variable_contains_line_marks())
        && ResolvePath(editor_state, contents_.back()->ToString(), &path,
                       &position, &pattern)) {
      LineMarks::Mark mark;
      mark.source = name_;
      mark.source_line = contents_.size() - 1;
      mark.target_buffer = path;
      mark.target = position;
      LOG(INFO) << "Found a mark: " << mark;
      editor_state->line_marks()->AddMark(mark);
    }
  }
  contents_.push_back(std::make_shared<Line>());
}

void OpenBuffer::Reload(EditorState* editor_state) {
  if (child_pid_ != -1) {
    kill(-child_pid_, SIGTERM);
    set_bool_variable(variable_reload_after_exit(), true);
    return;
  }
  for (const auto& dir : editor_state->edge_path()) {
    EvaluateFile(editor_state, PathJoin(dir, L"hooks/buffer-reload.cc"));
  }
  auto buffer_path = read_string_variable(variable_path());
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
  ReloadInto(editor_state, this);
  CheckPosition();
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

void OpenBuffer::ProcessCommandInput(
    EditorState* editor_state, shared_ptr<LazyString> str) {
  assert(read_bool_variable(variable_pts()));
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
      if (!all_of(status.begin(), status.end(),
                  [](const wchar_t& c) {
                    return c == L'♪' || c == L'♫' || c == L'…' || c == L' ' ||
                           c == L'𝄞';
                  })) {
        status = L"𝄞";
      } else if (status.size() >= 40) {
        status = L"…" + status.substr(status.size() - 40, status.size());
      }
      editor_state->SetStatus(
          status + L" " + (status.back() == L'♪' ? L"♫" : L"♪"));
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
      read_index = ProcessTerminalEscapeSequence(
          editor_state, str, read_index, &modifiers);
      CHECK_LT(position_pts_.line, contents_.size());
      current_line = contents_.at(position_pts_.line);
    } else if (isprint(c) || c == '\t') {
      contents_.SetCharacter(
          position_pts_.line, position_pts_.column, c, modifiers);
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
        if (static_cast<size_t>(Read(variable_view_start_line()))
                > position_pts_.line) {
          set_int_variable(variable_view_start_line(), position_pts_.line);
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
      case '@':
        {
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
          position_pts_ = LineColumn(
              Read(variable_view_start_line()) + line_delta,
              column_delta);
          while (position_pts_.line >= contents_.size()) {
            contents_.push_back(std::make_shared<Line>());
          }
          MaybeFollowToEndOfFile();
          set_int_variable(variable_view_start_column(), column_delta);
        }
        return read_index;

      case 'J':
        // ed: clear to end of screen.
        EraseLines(position_pts_.line + 1, contents_.size());
        CHECK_LT(position_pts_.line, contents_.size());
        return read_index;

      case 'K':
        {
          // el: clear to end of line.
          contents_.DeleteCharactersFromLine(
              position_pts_.line, position_pts_.column);
          return read_index;
        }

      case 'M':
        // dl1: delete one line.
        {
          EraseLines(position_pts_.line, position_pts_.line + 1);
          CHECK_LT(position_pts_.line, contents_.size());
        }
        return read_index;

      case 'P':
        {
          contents_.DeleteCharactersFromLine(
               position_pts_.line,
               position_pts_.column,
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

void OpenBuffer::AppendToLastLine(
    EditorState* editor_state, shared_ptr<LazyString> str) {
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

unique_ptr<Value> OpenBuffer::EvaluateExpression(EditorState*,
                                                 Expression* expr) {
  return Evaluate(expr, &environment_);
}

unique_ptr<Value> OpenBuffer::EvaluateString(EditorState* editor_state,
                                             const wstring& code) {
  wstring error_description;
  LOG(INFO) << "Compiling code.";
  unique_ptr<Expression> expression =
      CompileString(editor_state, code, &error_description);
  if (expression == nullptr) {
    editor_state->SetStatus(L"Compilation error: " + error_description);
    return nullptr;
  }
  LOG(INFO) << "Code compiled, evaluating.";
  auto result = EvaluateExpression(editor_state, expression.get());
  LOG(INFO) << "Done evaluating compiled code.";
  return result;
}

unique_ptr<Value> OpenBuffer::EvaluateFile(EditorState* editor_state,
                                           const wstring& path) {
  wstring error_description;
  unique_ptr<Expression> expression(
      CompileFile(ToByteString(path), &environment_, &error_description));
  if (expression == nullptr) {
    editor_state->SetStatus(path + L": error: " + error_description);
    return nullptr;
  }
  return Evaluate(expression.get(), &environment_);
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
  if (buffer.empty()) { return input_position; }
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
  if (contents_.empty() || current_line() == nullptr) { return; }
  size_t line_length = current_line()->size();
  if (position().column > line_length) {
    set_current_position_col(line_length);
  }
}

void OpenBuffer::CheckPosition() {
  if (position().line > contents_.size()) {
    set_current_position_line(contents_.size());
  }
}

CursorsSet* OpenBuffer::FindCursors(const wstring& name) {
  return cursors_tracker_.FindOrCreateCursors(name);
}

CursorsSet* OpenBuffer::active_cursors() {
  return FindCursors(editor_->modifiers().active_cursors);
}

void OpenBuffer::set_active_cursors(const vector<LineColumn>& positions) {
  if (positions.empty()) { return; }
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
    case LINE:
      {
        auto structure = editor_->modifiers().structure;
        LineColumn first, last;
        Modifiers tmp_modifiers = editor_->modifiers();
        tmp_modifiers.structure = CURSOR;
        if (!FindPartialRange(tmp_modifiers, position(), &first, &last)) {
          return;
        }
        if (first == last) { return; }
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
      }
      break;
    default:
      CHECK_LE(position().line, contents_.size());
      active_cursors()->insert(position());
  }
  editor_->SetStatus(L"Cursor created.");
  editor_->ScheduleRedraw();
}

CursorsSet::iterator OpenBuffer::FindPreviousCursor(
    LineColumn position) {
  LOG(INFO) << "Visiting previous cursor: " << editor_->modifiers();
  editor_->set_direction(ReverseDirection(editor_->modifiers().direction));
  return FindNextCursor(position);
}

CursorsSet::iterator OpenBuffer::FindNextCursor(LineColumn position) {
  LOG(INFO) << "Visiting next cursor: " << editor_->modifiers();
  auto direction = editor_->modifiers().direction;
  auto cursors = active_cursors();
  if (cursors->empty()) { return cursors->end(); }

  size_t index = 0;
  auto output = cursors->begin();
  while (output != cursors->end()
         && (*output < position
             || (direction == FORWARDS
                 && *output == position
                 && std::next(output) != cursors->end()
                 && *std::next(output) == position))) {
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
  if (cursors->size() <= 1) { return; }
  size_t repetitions =
      min(editor_->modifiers().repetitions, cursors->size() - 1);
  for (size_t i = 0; i < repetitions; i++) {
    cursors_tracker_.DeleteCurrentCursor(cursors);
  }
  CHECK_LE(position().line, contents_.size());
  editor_->ScheduleRedraw();
}

void OpenBuffer::DestroyOtherCursors() {
  auto position = this->position();
  CHECK_LE(position, contents_.size());
  auto cursors = active_cursors();
  cursors->clear();
  cursors->insert(position);
  cursors_tracker_.SetCurrentCursor(cursors, position);
  set_bool_variable(variable_multiple_cursors(), false);
  editor_->ScheduleRedraw();
}

void OpenBuffer::SeekToStructure(
    Structure structure, Direction direction, LineColumn* position) {
  switch (structure) {
    case BUFFER:
    case CURSOR:
    case CHAR:
    case TREE:
      break;

    case LINE:
      Seek(*this, position)
          .WithDirection(direction)
          .WrappingLines()
          .UntilCurrentCharNotIn(L"\n");
      break;

    case WORD:
      Seek(*this, position)
          .WithDirection(direction)
          .WrappingLines()
          .UntilCurrentCharIn(read_string_variable(variable_word_characters()));
  }
}

bool OpenBuffer::SeekToLimit(
    Structure structure, Direction direction, LineColumn* position) {
  if (empty()) {
    *position = LineColumn();
  } else {
    position->line = min(lines_size() - 1, position->line);
    position->column = min(LineAt(position->line)->size(), position->column);
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
      return Seek(*this, position).WrappingLines().WithDirection(direction).Once()
          == Seek::DONE;
      break;

    case TREE:
      {
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
          Seek(*this, &boundary).WithDirection(direction).Once();
        }
        *position = boundary;
      }
      break;

    case LINE:
      if (direction == BACKWARDS) {
        position->column = 0;
        return Seek(*this, position).WrappingLines().Backwards().Once()
                   == Seek::DONE;
      }
      position->column = LineAt(position->line)->size();
      return true;
      break;

    case WORD:
      {
        return Seek(*this, position)
            .WithDirection(direction)
            .WrappingLines()
            .UntilCurrentCharNotIn(
                    read_string_variable(variable_word_characters()))
                == Seek::DONE;
      }
      break;

    case CURSOR:
      {
        bool has_boundary = false;
        LineColumn boundary;
        auto cursors = cursors_tracker_.FindCursors(L"");
        CHECK(cursors != nullptr);
        for (const auto& candidate : *cursors) {
          if (direction == FORWARDS
                  ? (candidate > *position &&
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
          Seek(*this, &boundary).WithDirection(direction).Once();
        }
        *position = boundary;
     }
  }
  return false;
}

bool OpenBuffer::FindPartialRange(
    const Modifiers& modifiers, const LineColumn& initial_position,
    LineColumn* start, LineColumn* end) {
  const auto forward = modifiers.direction;
  const auto backward = ReverseDirection(forward);

  LineColumn position = initial_position;
  if (!empty()) {
    position.line = min(lines_size() - 1, position.line);
    position.column = min(LineAt(position.line)->size(), position.column);
  }
  if (modifiers.direction == BACKWARDS) {
    Seek(*this, &position).Backwards().WrappingLines().Once();
  }

  *start = position;
  LOG(INFO) << "Initial position: " << position;
  SeekToStructure(modifiers.structure, forward, start);
  switch (modifiers.boundary_begin) {
    case Modifiers::CURRENT_POSITION:
      *start = modifiers.direction == FORWARDS
                   ? max(position, *start) : min(position, *start);
      break;

    case Modifiers::LIMIT_CURRENT:
      {
        if (SeekToLimit(modifiers.structure, backward, start)) {
          Seek(*this, start).WrappingLines().WithDirection(forward).Once();
        }
      }
      break;

    case Modifiers::LIMIT_NEIGHBOR:
      if (SeekToLimit(modifiers.structure, backward, start)) {
        SeekToStructure(modifiers.structure, backward, start);
        SeekToLimit(modifiers.structure, forward, start);
      }
  }
  LOG(INFO) << "After seek, initial position: " << *start;
  *end = modifiers.direction == FORWARDS
             ? max(position, *start) : min(position, *start);
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
      Seek(*this, start).WrappingLines().Once();
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

const shared_ptr<const Line> OpenBuffer::current_line() const {
  if (position().line >= contents_.size()) { return nullptr; }
  return contents_.at(position().line);
}

std::shared_ptr<OpenBuffer> OpenBuffer::GetBufferFromCurrentLine() {
  if (contents()->empty() || current_line() == nullptr) {
    return nullptr;
  }
  auto target = current_line()->environment()->Lookup(L"buffer");
  if (target == nullptr
      || target->type.type != VMType::OBJECT_TYPE
      || target->type.object_type != L"Buffer") {
    return nullptr;
  }
  return std::static_pointer_cast<OpenBuffer>(target->user_value);
}

wstring OpenBuffer::ToString() const {
  return contents_.ToString();
}

void OpenBuffer::PushSignal(EditorState* editor_state, int sig) {
  switch (sig) {
    case SIGINT:
      if (read_bool_variable(variable_pts())) {
        string sequence(1, 0x03);
        (void) write(fd_.fd, sequence.c_str(), sequence.size());
        editor_state->SetStatus(L"SIGINT");
      } else if (child_pid_ != -1) {
        editor_state->SetStatus(L"SIGINT >> pid:" + to_wstring(child_pid_));
        kill(child_pid_, sig);
      }
      break;

    case SIGTSTP:
      if (read_bool_variable(variable_pts())) {
        string sequence(1, 0x1a);
        write(fd_.fd, sequence.c_str(), sequence.size());
      }
      break;

    default:
      editor_state->SetStatus(L"Unexpected signal received: " + to_wstring(sig));
  }
}

wstring OpenBuffer::TransformKeyboardText(wstring input) {
  using afc::vm::VMType;
  for (auto& t : keyboard_text_transformers_) {
    vector<unique_ptr<Value>> args;
    args.push_back(afc::vm::Value::NewString(std::move(input)));
    auto result = t->callback(std::move(args));
    CHECK_EQ(result->type.type, VMType::VM_STRING);
    input = std::move(result->str);
  }
  return input;
}

bool OpenBuffer::AddKeyboardTextTransformer(EditorState* editor_state,
                                            unique_ptr<Value> transformer) {
  if (transformer == nullptr
      || transformer->type.type != VMType::FUNCTION
      || transformer->type.type_arguments.size() != 2
      || transformer->type.type_arguments[0].type != VMType::VM_STRING
      || transformer->type.type_arguments[1].type != VMType::VM_STRING) {
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
  if (fd != -1) { close(fd); fd = -1; }
}

void OpenBuffer::SetInputFiles(
    EditorState* editor_state, int input_fd, int input_error_fd,
    bool fd_is_terminal, pid_t child_pid) {
  if (read_bool_variable(variable_clear_on_reload())) {
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

size_t OpenBuffer::current_position_line() const {
  return position().line;
}

void OpenBuffer::set_current_position_line(size_t line) {
  set_current_cursor(LineColumn(min(line, contents_.size())));
}

size_t OpenBuffer::current_position_col() const {
  return position().column;
}

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
  set_current_cursor(LineColumn(min(position.line, contents_.size()),
                                position.column));
}

wstring OpenBuffer::FlagsString() const {
  wstring output;
  if (modified()) {
    output += L"~";
  }
  if (fd() != -1) {
    output += L"< l:" + to_wstring(contents_.size());
    if (read_bool_variable(variable_follow_end_of_file())) {
      output += L" ↓";
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

  auto marks = GetLineMarksText(*editor_);
  if (!marks.empty()) {
    output += L" " + marks;
  }

  return output;
}

/* static */ EdgeStruct<bool>* OpenBuffer::BoolStruct() {
  static EdgeStruct<bool>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<bool>();
    // Trigger registration of all fields.
    OpenBuffer::variable_pts();
    OpenBuffer::variable_vm_exec();
    OpenBuffer::variable_close_after_clean_exit();
    OpenBuffer::variable_allow_dirty_delete();
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
    OpenBuffer::variable_contains_line_marks();
    OpenBuffer::variable_multiple_cursors();
    OpenBuffer::variable_reload_on_display();
    OpenBuffer::variable_show_in_buffers_list();
    OpenBuffer::variable_push_positions_to_history();
    OpenBuffer::variable_delete_into_paste_buffer();
    OpenBuffer::variable_search_case_sensitive();
  }
  return output;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_pts() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"pts",
      L"If a command is forked that writes to this buffer, should it be run "
      L"with its own pseudoterminal?",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_vm_exec() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"vm_exec",
      L"If set, all input read into this buffer will be executed.",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_close_after_clean_exit() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"close_after_clean_exit",
      L"If a command is forked that writes to this buffer, should the buffer be "
      L"closed when the command exits with a successful status code?",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>*
OpenBuffer::variable_allow_dirty_delete() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"allow_dirty_delete",
      L"Allow this buffer to be deleted even if it's dirty (i.e. if it has "
      L"unsaved changes or an underlying process that's still running).",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_reload_after_exit() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"reload_after_exit",
      L"If a forked command that writes to this buffer exits, should Edge "
      L"reload the buffer?",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_default_reload_after_exit() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"default_reload_after_exit",
      L"If a forked command that writes to this buffer exits and "
      L"reload_after_exit is set, what should Edge set reload_after_exit just "
      L"after reloading the buffer?",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_reload_on_enter() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"reload_on_enter",
      L"Should this buffer be reloaded automatically when visited?",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_atomic_lines() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"atomic_lines",
      L"If true, lines can't be joined (e.g. you can't delete the last "
      L"character in a line unless the line is empty).  This is used by certain "
      L"buffers that represent lists of things (each represented as a line), "
      L"for which this is a natural behavior.",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_save_on_close() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"save_on_close",
      L"Should this buffer be saved automatically when it's closed?",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_clear_on_reload() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"clear_on_reload",
      L"Should any previous contents be discarded when this buffer is reloaded? "
      L"If false, previous contents will be preserved and new contents will be "
      L"appended at the end.",
      true);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_paste_mode() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"paste_mode",
      L"When paste_mode is enabled in a buffer, it will be displayed in a way "
      L"that makes it possible to select (with a mouse) parts of it (that are "
      L"currently shown).  It will also allow you to paste text directly into "
      L"the buffer.",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_follow_end_of_file() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"follow_end_of_file",
      L"Should the cursor stay at the end of the file?",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_commands_background_mode() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"commands_background_mode",
      L"Should new commands forked from this buffer be started in background "
      L"mode?  If false, we will switch to them automatically.",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_reload_on_buffer_write() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"reload_on_buffer_write",
      L"Should the current buffer (on which this variable is set) be reloaded "
      L"when any buffer is written?  This is useful mainly for command buffers "
      L"like 'make' or 'git diff'.",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_contains_line_marks() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"contains_line_marks",
      L"If set to true, this buffer will be scanned for line marks.",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_multiple_cursors() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"multiple_cursors",
      L"If set to true, operations in this buffer apply to all cursors defined "
      L"on it.",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>* OpenBuffer::variable_reload_on_display() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"reload_on_display",
      L"If set to true, a buffer will always be reloaded before being "
      L"displayed.",
      false);
  return variable;
}

/* static */ EdgeVariable<bool>*
OpenBuffer::variable_show_in_buffers_list() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"show_in_buffers_list",
      L"If set to true, includes this in the list of buffers.",
      true);
  return variable;
}

/* static */ EdgeVariable<bool>*
OpenBuffer::variable_push_positions_to_history() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"push_positions_to_history",
      L"If set to true, movement in this buffer result in positions being "
      L"pushed to the history of positions.",
      true);
  return variable;
}

/* static */ EdgeVariable<bool>*
OpenBuffer::variable_delete_into_paste_buffer() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"delete_into_paste_buffer",
      L"If set to true, deletions from this buffer will go into the shared "
      L"paste buffer.",
      true);
  return variable;
}

/* static */ EdgeVariable<bool>*
OpenBuffer::variable_search_case_sensitive() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"search_case_sensitive",
      L"If set to true, search (through \"/\") is case sensitive.",
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
    OpenBuffer::variable_dictionary();
    OpenBuffer::variable_tree_parser();
    OpenBuffer::variable_language_keywords();
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
      L"String with the current command. Empty if the buffer is not a "
      L"sub-process (e.g. a regular file).",
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

/* static */ EdgeVariable<wstring>*
OpenBuffer::variable_dictionary() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"dictionary",
      L"Path to a dictionary file used for autocompletion. If empty, pressing "
      L"TAB (in insert mode) just inserts a tab character into the file; "
      L"otherwise, it triggers completion to the first string from the "
      L"dictionary that matches the prefix of the current word. Pressing TAB "
      L"again iterates through all completions.",
      L"");
  return variable;
}

/* static */ EdgeVariable<wstring>*
OpenBuffer::variable_tree_parser() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"tree_parser",
      L"Name of the parser to use to extract the tree structure from the "
      L"current file. Valid values are: \"text\" (normal text), and \"cpp\". "
      L"Any other value disables the tree logic.",
      L"");
  return variable;
}

/* static */ EdgeVariable<wstring>*
OpenBuffer::variable_language_keywords() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"language_keywords",
      L"Space separated list of keywords that should be highlighted by the "
      L"\"cpp\" tree parser (see variable tree_parser).",
      L"");
  return variable;
}

/* static */ EdgeStruct<int>* OpenBuffer::IntStruct() {
  static EdgeStruct<int>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<int>;
    // Trigger registration of all fields.
    OpenBuffer::variable_line_width();
    OpenBuffer::variable_buffer_list_context_lines();
    OpenBuffer::variable_margin_lines();
    OpenBuffer::variable_margin_columns();
    OpenBuffer::variable_view_start_line();
    OpenBuffer::variable_view_start_column();
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

/* static */ EdgeVariable<int>*
    OpenBuffer::variable_buffer_list_context_lines() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"buffer_list_context_lines",
      L"Number of lines of context from this buffer to show in the list of "
      L"buffers.",
      0);
  return variable;
}

/* static */ EdgeVariable<int>*
    OpenBuffer::variable_margin_lines() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"margin_lines",
      L"Number of lines of context to display at the top/bottom of the current "
      L"position.",
      2);
  return variable;
}

/* static */ EdgeVariable<int>*
    OpenBuffer::variable_margin_columns() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"margin_columns",
      L"Number of characters of context to display at the left/right of the "
      L"current position.",
      2);
  return variable;
}

/* static */ EdgeVariable<int>*
    OpenBuffer::variable_view_start_line() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"view_start_line",
      L"The desired line to show at the beginning of the screen (at the "
      L"top-most position). This is adjusted automatically as the cursor moves "
      L"around in the buffer.",
      0);
  return variable;
}

/* static */ EdgeVariable<int>*
    OpenBuffer::variable_view_start_column() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"view_start_column",
      L"The desired column to show at the left-most part of the screen. This "
      L"is adjusted automatically as the cursor moves around in the buffer.",
      0);
  return variable;
}

const bool& OpenBuffer::read_bool_variable(
    const EdgeVariable<bool>* variable) const {
  return bool_variables_.Get(variable);
}

void OpenBuffer::set_bool_variable(
    const EdgeVariable<bool>* variable, bool value) {
  bool_variables_.Set(variable, value);
}

void OpenBuffer::toggle_bool_variable(const EdgeVariable<bool>* variable) {
  set_bool_variable(variable, !read_bool_variable(variable));
}

const wstring& OpenBuffer::read_string_variable(
    const EdgeVariable<wstring>* variable) const {
  return string_variables_.Get(variable);
}

void OpenBuffer::set_string_variable(
    const EdgeVariable<wstring>* variable, wstring value) {
  string_variables_.Set(variable, value);

  // TODO: This should be in the variable definition, not here. Ugh.
  if (variable == variable_word_characters()
      || variable == variable_tree_parser()
      || variable == variable_language_keywords()) {
    UpdateTreeParser();
  }
}

const int& OpenBuffer::Read(const EdgeVariable<int>* variable) const {
  return int_variables_.Get(variable);
}

void OpenBuffer::set_int_variable(
    const EdgeVariable<int>* variable, int value) {
  int_variables_.Set(variable, value);
}

void OpenBuffer::ApplyToCursors(unique_ptr<Transformation> transformation) {
  ApplyToCursors(std::move(transformation),
                 read_bool_variable(variable_multiple_cursors())
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

  transformations_past_.emplace_back(new Transformation::Result(editor_));

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
        cursors,
        [this, &transformation](LineColumn old_position) {
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

LineColumn OpenBuffer::Apply(
    EditorState* editor_state, unique_ptr<Transformation> transformation) {
  CHECK(transformation != nullptr);
  CHECK(!transformations_past_.empty());

  transformation->Apply(
      editor_state, this, transformations_past_.back().get());
  CHECK(!transformations_past_.empty());

  auto delete_buffer = transformations_past_.back()->delete_buffer;
  CHECK(delete_buffer != nullptr);
  if ((delete_buffer->contents()->size() > 1
       || delete_buffer->LineAt(0)->size() > 0)
      && read_bool_variable(variable_delete_into_paste_buffer())) {
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
  last_transformation_stack_.emplace_back(new TransformationStack());
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
      target->emplace_back(new Transformation::Result(editor_state));
      source->back()->undo_stack->Apply(editor_state, this, target->back().get());
      source->pop_back();
      modified_buffer =
          target->back()->modified_buffer || undo_mode == ONLY_UNDO_THE_LAST;
    }
    if (source->empty()) { return; }
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

  vector<unique_ptr<Value>> args;
  args.push_back(Value::NewString(old_line.ToString()));
  bool filtered = filter_->callback(std::move(args))->boolean;

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
    int expired_marks = 0;
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

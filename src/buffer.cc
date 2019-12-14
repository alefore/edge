#include "src/buffer.h"

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

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command_with_modifiers.h"
#include "src/cpp_parse_tree.h"
#include "src/cursors_transformation.h"
#include "src/dirname.h"
#include "src/editor.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/lazy_string.h"
#include "src/lazy_string_append.h"
#include "src/lazy_string_functional.h"
#include "src/line_marks.h"
#include "src/map_mode.h"
#include "src/parsers/diff.h"
#include "src/parsers/markdown.h"
#include "src/run_command_handler.h"
#include "src/screen.h"
#include "src/screen_vm.h"
#include "src/seek.h"
#include "src/server.h"
#include "src/status.h"
#include "src/substring.h"
#include "src/time.h"
#include "src/tracker.h"
#include "src/transformation.h"
#include "src/transformation_delete.h"
#include "src/viewers.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"
#include "src/vm_transformation.h"
#include "src/wstring.h"

namespace afc {
namespace vm {
std::shared_ptr<editor::OpenBuffer>
VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::get(Value* value) {
  return std::shared_ptr<editor::OpenBuffer>(
      static_cast<editor::OpenBuffer*>(value->user_value.get()),
      [dependency = value->user_value](editor::OpenBuffer*) { /* Nothing. */ });
}

/* static */ Value::Ptr VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::New(
    std::shared_ptr<editor::OpenBuffer> value) {
  return Value::NewObject(
      L"Buffer",
      shared_ptr<void>(value.get(), [value](void*) { /* Nothing. */ }));
}

const VMType VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::vmtype =
    VMType::ObjectType(L"Buffer");
}  // namespace vm
namespace editor {
namespace {
static const wchar_t* kOldCursors = L"old-cursors";

using std::unordered_set;

template <typename EdgeStruct, typename FieldValue>
void RegisterBufferFields(
    EdgeStruct* edge_struct, afc::vm::ObjectType* object_type,
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
        vm::NewCallback(std::function<FieldValue(std::shared_ptr<OpenBuffer>)>(
            [reader, variable](std::shared_ptr<OpenBuffer> buffer) {
              DVLOG(4) << "Buffer field reader is returning.";
              return (buffer.get()->*reader)(variable);
            })));

    // Setter.
    object_type->AddField(
        L"set_" + variable->name(),
        vm::NewCallback(
            std::function<void(std::shared_ptr<OpenBuffer>, FieldValue)>(
                [variable, setter](std::shared_ptr<OpenBuffer> buffer,
                                   FieldValue value) {
                  (buffer.get()->*setter)(variable, value);
                })));
  }
}
}  // namespace

using namespace afc::vm;
using std::to_wstring;

/* static */ const wstring OpenBuffer::kBuffersName = L"- buffers";
/* static */ const wstring OpenBuffer::kPasteBuffer = L"- paste buffer";

// TODO: Once we can capture std::unique_ptr, turn transformation into one.
void OpenBuffer::EvaluateMap(OpenBuffer* buffer, LineNumber line,
                             Value::Callback map_callback,
                             TransformationStack* transformation,
                             Trampoline* trampoline) {
  if ((line + LineNumberDelta(1)).ToDelta() >= buffer->lines_size()) {
    buffer->Apply(std::unique_ptr<Transformation>(transformation));
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

  Evaluate(
      map_line.get(), trampoline->environment(),
      [buffer, line, map_callback, transformation, trampoline, current_line,
       map_line](Value::Ptr value) {
        if (value->str != current_line) {
          DeleteOptions delete_options;
          delete_options.copy_to_paste_buffer = false;
          delete_options.modifiers.structure = StructureLine();
          transformation->PushBack(
              NewDeleteTransformation(std::move(delete_options)));
          InsertOptions insert_options;
          auto buffer_to_insert =
              std::make_shared<OpenBuffer>(buffer->editor(), L"tmp buffer");
          buffer_to_insert->AppendLine(NewLazyString(std::move(value->str)));
          insert_options.buffer_to_insert = std::move(buffer_to_insert);
          transformation->PushBack(
              NewInsertBufferTransformation(std::move(insert_options)));
        }
        EvaluateMap(buffer, line + LineNumberDelta(1), std::move(map_callback),
                    transformation, trampoline);
      },
      [buffer](std::function<void()> callback) {
        buffer->SchedulePendingWork(std::move(callback));
      });
}

/* static */
OpenBuffer::SyntaxDataOutput OpenBuffer::UpdateSyntaxData(
    SyntaxDataInput input) {
  VLOG(3) << "Executing parse tree update.";

  SyntaxDataOutput output;

  output.parse_tree = std::make_shared<ParseTree>(
      input.parser->FindChildren(*input.contents, input.contents->range()));

  output.simplified_parse_tree =
      std::make_shared<ParseTree>(SimplifyTree(*output.parse_tree));
  return output;
}

/* static */
int OpenBuffer::UpdateSyntaxDataZoom(SyntaxDataZoomInput input) {
  if (input.buffer->simplified_parse_tree() != input.simplified_parse_tree) {
    return 0;  // Parse tree changed in the meantime.
  }

  auto output = std::make_shared<ParseTree>(ZoomOutTree(
      *input.simplified_parse_tree, input.lines_size, input.view_size));
  input.buffer->work_queue_.Schedule([input, output]() mutable {
    if (input.buffer->simplified_parse_tree() != input.simplified_parse_tree) {
      return;  // Parse tree changed in the meantime.
    }
    input.buffer->zoomed_out_parse_trees_[input.view_size] = std::move(output);
  });
  return 0;
}

/* static */ void OpenBuffer::RegisterBufferType(
    EditorState* editor_state, afc::vm::Environment* environment) {
  auto buffer = std::make_unique<ObjectType>(L"Buffer");

  RegisterBufferFields<EdgeStruct<bool>, bool>(buffer_variables::BoolStruct(),
                                               buffer.get(), &OpenBuffer::Read,
                                               &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<wstring>, wstring>(
      buffer_variables::StringStruct(), buffer.get(), &OpenBuffer::Read,
      &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<int>, int>(buffer_variables::IntStruct(),
                                             buffer.get(), &OpenBuffer::Read,
                                             &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<double>, double>(
      buffer_variables::DoubleStruct(), buffer.get(), &OpenBuffer::Read,
      &OpenBuffer::Set);

  buffer->AddField(
      L"SetStatus",
      vm::NewCallback(std::function<void(std::shared_ptr<OpenBuffer>, wstring)>(
          [](std::shared_ptr<OpenBuffer> buffer, wstring s) {
            buffer->status()->SetInformationText(s);
          })));

  buffer->AddField(
      L"line_count",
      vm::NewCallback(std::function<int(std::shared_ptr<OpenBuffer>)>(
          [](std::shared_ptr<OpenBuffer> buffer) {
            return int(buffer->contents()->size().line_delta);
          })));

  buffer->AddField(
      L"set_position",
      vm::NewCallback(
          std::function<void(std::shared_ptr<OpenBuffer>, LineColumn)>(
              [](std::shared_ptr<OpenBuffer> buffer, LineColumn position) {
                buffer->set_position(position);
              })));

  buffer->AddField(
      L"position",
      vm::NewCallback(std::function<LineColumn(std::shared_ptr<OpenBuffer>)>(
          [](std::shared_ptr<OpenBuffer> buffer) {
            return LineColumn(buffer->position());
          })));

  buffer->AddField(
      L"line",
      vm::NewCallback(std::function<wstring(std::shared_ptr<OpenBuffer>, int)>(
          [](std::shared_ptr<OpenBuffer> buffer, int line_input) {
            LineNumber line =
                min(LineNumber(max(line_input, 0)),
                    LineNumber(0) + buffer->lines_size() - LineNumberDelta(1));
            return buffer->contents()->at(line)->ToString();
          })));

  buffer->AddField(
      L"ApplyTransformation",
      vm::NewCallback(
          std::function<void(std::shared_ptr<OpenBuffer>, Transformation*)>(
              [](std::shared_ptr<OpenBuffer> buffer,
                 Transformation* transformation) {
                buffer->ApplyToCursors(transformation->Clone());
              })));

  buffer->AddField(
      L"Map", Value::NewFunction(
                  {VMType::Void(), VMType::ObjectType(buffer.get()),
                   VMType::Function({VMType::String(), VMType::String()})},
                  [](vector<unique_ptr<Value>> args, Trampoline* evaluation) {
                    CHECK_EQ(args.size(), size_t(2));
                    CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
                    EvaluateMap(
                        static_cast<OpenBuffer*>(args[0]->user_value.get()),
                        LineNumber(0), args[1]->callback,
                        std::make_unique<TransformationStack>().release(),
                        evaluation);
                  }));

#if 0
  buffer->AddField(
      L"GetRegion",
      Value::NewFunction(
          {VMType::ObjectType(L"Range"), VMType::ObjectType(buffer.get()),
           VMType::String()},
          [](vector<Value::Ptr> args, Trampoline* trampoline) {
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
                    resume(Value::NewObject(
                        L"Range", std::make_shared<Range>(
                             buffer->FindPartialRange(
                                 modifiers, buffer->position()))));
                  } else {
                    buffer->PushTransformationStack();
                    DeleteOptions options;
                    options.modifiers = modifiers;
                    options.copy_to_paste_buffer = false;
                    buffer->ApplyToCursors(
                        NewDeleteTransformation(options),
                        Modifiers::AFFECT_ONLY_CURRENT_CURSOR,
                        Transformation::Result::Mode::kPreview);
                    buffer->PopTransformationStack();
                  }
                })
                ->ProcessInput(L'\n', editor_state);
          }));
#endif

  buffer->AddField(
      L"PushTransformationStack",
      vm::NewCallback(std::function<void(std::shared_ptr<OpenBuffer>)>(
          [](std::shared_ptr<OpenBuffer> buffer) {
            buffer->PushTransformationStack();
          })));

  buffer->AddField(
      L"PopTransformationStack",
      vm::NewCallback(std::function<void(std::shared_ptr<OpenBuffer>)>(
          [](std::shared_ptr<OpenBuffer> buffer) {
            buffer->PopTransformationStack();
          })));

  buffer->AddField(
      L"AddKeyboardTextTransformer",
      Value::NewFunction(
          {VMType::Bool(), VMType::ObjectType(buffer.get()),
           VMType::Function({VMType::String(), VMType::String()})},
          [](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), size_t(2));
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            auto buffer = static_cast<OpenBuffer*>(args[0]->user_value.get());
            CHECK(buffer != nullptr);
            return Value::NewBool(
                buffer->AddKeyboardTextTransformer(std::move(args[1])));
          }));

  buffer->AddField(
      L"Filter", Value::NewFunction(
                     {VMType::Void(), VMType::ObjectType(buffer.get()),
                      VMType::Function({VMType::Bool(), VMType::String()})},
                     [](vector<unique_ptr<Value>> args) {
                       CHECK_EQ(args.size(), size_t(2));
                       CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
                       auto buffer =
                           static_cast<OpenBuffer*>(args[0]->user_value.get());
                       CHECK(buffer != nullptr);
                       buffer->set_filter(std::move(args[1]));
                       return Value::NewVoid();
                     }));

  buffer->AddField(
      L"DeleteCharacters",
      vm::NewCallback(std::function<void(std::shared_ptr<OpenBuffer>, int)>(
          [](std::shared_ptr<OpenBuffer> buffer, int count) {
            DeleteOptions options;
            options.modifiers.repetitions = count;
            buffer->ApplyToCursors(NewDeleteTransformation(options));
          })));

  buffer->AddField(
      L"Reload",
      vm::NewCallback(std::function<void(std::shared_ptr<OpenBuffer>)>(
          [](std::shared_ptr<OpenBuffer> buffer) { buffer->Reload(); })));

  buffer->AddField(
      L"InsertText",
      vm::NewCallback(std::function<void(std::shared_ptr<OpenBuffer>, wstring)>(
          [](std::shared_ptr<OpenBuffer> buffer, wstring text) {
            if (text.empty()) {
              return;  // Optimization.
            }
            if (buffer->fd() != nullptr) {
              auto str = ToByteString(text);
              LOG(INFO) << "Insert text: " << str.size();
              if (write(buffer->fd()->fd(), str.c_str(), str.size()) == -1) {
                buffer->editor()->status()->SetWarningText(
                    L"Write failed: " + FromByteString(strerror(errno)));
              }
              return;
            }
            auto buffer_to_insert =
                std::make_shared<OpenBuffer>(buffer->editor(), L"tmp buffer");

            // getline will silently eat the last (empty) line.
            std::wistringstream text_stream(text + L"\n");
            std::wstring line;
            bool insert_separator = false;
            while (std::getline(text_stream, line, wchar_t('\n'))) {
              if (insert_separator) {
                buffer_to_insert->AppendEmptyLine();
              } else {
                insert_separator = true;
              }
              buffer_to_insert->AppendToLastLine(
                  NewLazyString(std::move(line)));
            }

            InsertOptions insert_options;
            insert_options.buffer_to_insert = std::move(buffer_to_insert);
            buffer->ApplyToCursors(
                NewInsertBufferTransformation(std::move(insert_options)));
          })));

  buffer->AddField(
      L"Save",
      vm::NewCallback(std::function<void(std::shared_ptr<OpenBuffer>)>(
          [](std::shared_ptr<OpenBuffer> buffer) { buffer->Save(); })));

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
      vm::NewCallback(
          std::function<void(std::shared_ptr<OpenBuffer>, wstring, wstring)>(
              [editor_state](std::shared_ptr<OpenBuffer> buffer, wstring keys,
                             wstring path) {
                LOG(INFO) << "AddBindingToFile: " << keys << " -> " << path;
                buffer->default_commands_->Add(
                    keys,
                    [buffer, path](EditorState* editor_state) {
                      wstring resolved_path;
                      auto options = ResolvePathOptions::New(editor_state);
                      options.path = path;
                      if (auto results = ResolvePath(std::move(options));
                          results.has_value()) {
                        buffer->EvaluateFile(results->path,
                                             [](std::unique_ptr<Value>) {});

                      } else {
                        buffer->status()->SetWarningText(
                            L"Unable to resolve: " + path);
                      }
                    },
                    L"Load file: " + path);
              })));

  buffer->AddField(
      L"ShowTrackers",
      vm::NewCallback(std::function<void(std::shared_ptr<OpenBuffer>)>(
          [](std::shared_ptr<OpenBuffer> buffer) {
            for (auto& data : Tracker::GetData()) {
              buffer->AppendLine(StringAppend(
                  StringAppend(NewLazyString(data.name), NewLazyString(L": ")),
                  NewLazyString(std::to_wstring(data.executions)),
                  NewLazyString(L" "),
                  NewLazyString(std::to_wstring(data.seconds))));
            }
          })));

  buffer->AddField(
      L"EvaluateFile",
      vm::NewCallback(std::function<void(std::shared_ptr<OpenBuffer>, wstring)>(
          [](std::shared_ptr<OpenBuffer> buffer, wstring path) {
            LOG(INFO) << "Evaluating file: " << path;
            buffer->EvaluateFile(path, [](std::unique_ptr<Value>) {});
          })));

  environment->DefineType(L"Buffer", std::move(buffer));
}

OpenBuffer::OpenBuffer(EditorState* editor_state, const wstring& name)
    : OpenBuffer([=]() {
        Options options;
        options.editor = editor_state;
        options.name = name;
        return options;
      }()) {}

OpenBuffer::OpenBuffer(Options options)
    : options_(std::move(options)),
      contents_([this](const CursorsTracker::Transformation& transformation) {
        if (syntax_data_state_ == SyntaxDataState::kDone) {
          syntax_data_state_ = SyntaxDataState::kPending;
          work_queue_.Schedule([this] {
            syntax_data_state_ = SyntaxDataState::kDone;
            if (!TreeParser::IsNull(tree_parser_.get())) {
              SyntaxDataInput input;
              input.contents = contents_.copy();
              input.parser = tree_parser_;
              syntax_data_.Push(std::move(input));
            }
          });
        }
        modified_ = true;
        time(&last_action_);
        cursors_tracker_.AdjustCursors(transformation);
      }),
      bool_variables_(buffer_variables::BoolStruct()->NewInstance()),
      string_variables_(buffer_variables::StringStruct()->NewInstance()),
      int_variables_(buffer_variables::IntStruct()->NewInstance()),
      double_variables_(buffer_variables::DoubleStruct()->NewInstance()),
      environment_(options_.editor->environment()),
      filter_version_(0),
      last_transformation_(NewNoopTransformation()),
      default_commands_(options_.editor->default_commands()->NewChild()),
      mode_(std::make_unique<MapMode>(default_commands_)),
      status_(options_.editor->GetConsole(), options_.editor->audio_player()),
      tree_parser_(NewNullTreeParser()),
      syntax_data_([this]() {
        AsyncProcessor<SyntaxDataInput, SyntaxDataOutput>::Options options;
        options.name = L"SyntaxData";
        options.factory = UpdateSyntaxData;
        options.notify_callback = [this]() {
          VLOG(5) << "Background thread is notifying internal event.";
          SchedulePendingWork([this]() {
            // TODO: Optimize this to not throw them away if the simplified
            // parse tree didn't change.
            zoomed_out_parse_trees_.clear();
          });
        };
        return options;
      }()),
      syntax_data_zoom_([this]() {
        AsyncProcessor<SyntaxDataZoomInput, int>::Options options;
        options.name = L"SyntaxDataZoom";
        options.factory = UpdateSyntaxDataZoom;
        options.push_behavior =
            AsyncProcessor<SyntaxDataZoomInput,
                           int>::Options::QueueBehavior::kWait;
        return options;
      }()) {
  UpdateTreeParser();

  environment_.Define(
      L"buffer",
      Value::NewObject(L"Buffer", shared_ptr<void>(this, [](void*) {})));

  Set(buffer_variables::name, options_.name);
  Set(buffer_variables::path, options_.path);
  Set(buffer_variables::pts_path, L"");
  Set(buffer_variables::command, L"");
  Set(buffer_variables::reload_after_exit, false);
  if (Read(buffer_variables::name) == kPasteBuffer) {
    Set(buffer_variables::allow_dirty_delete, true);
    Set(buffer_variables::show_in_buffers_list, false);
    Set(buffer_variables::delete_into_paste_buffer, false);
  }
  ClearContents(BufferContents::CursorsBehavior::kUnmodified);

  for (const auto& dir : options_.editor->edge_path()) {
    auto state_path =
        PathJoin(PathJoin(dir, L"state"),
                 PathJoin(Read(buffer_variables::path), L".edge_state"));
    struct stat stat_buffer;
    if (stat(ToByteString(state_path).c_str(), &stat_buffer) == -1) {
      continue;
    }
    EvaluateFile(state_path, [](std::unique_ptr<Value>) {});
  }
}

EditorState* OpenBuffer::editor() const { return options_.editor; }

Status* OpenBuffer::status() const { return &status_; }

std::optional<wstring> OpenBuffer::IsUnableToPrepareToClose() const {
  if (options_.editor->modifiers().strength > Modifiers::Strength::kNormal) {
    return std::nullopt;
  }
  if (child_pid_ != -1) {
    if (!Read(buffer_variables::term_on_close)) {
      return L"Running subprocess (pid: " + std::to_wstring(child_pid_) + L")";
    }
    return std::nullopt;
  }
  if (dirty() && !Read(buffer_variables::save_on_close) &&
      !Read(buffer_variables::allow_dirty_delete)) {
    return L"Unsaved changes";
  }
  return std::nullopt;
}

void OpenBuffer::PrepareToClose(std::function<void()> success,
                                std::function<void(wstring)> failure) {
  LOG(INFO) << "Preparing to close: " << Read(buffer_variables::name);
  auto is_unable = IsUnableToPrepareToClose();
  if (is_unable.has_value()) {
    return failure(is_unable.value());
  }

  if (!PersistState() &&
      options_.editor->modifiers().strength == Modifiers::Strength::kNormal) {
    LOG(INFO) << "Unable to persist state: " << Read(buffer_variables::name);
    return failure(L"Unable to persist state.");
  }
  if (child_pid_ != -1) {
    if (Read(buffer_variables::term_on_close)) {
      LOG(INFO) << "Sending termination and preparing handler: "
                << Read(buffer_variables::name);
      kill(child_pid_, SIGTERM);
      on_exit_handler_ = [this, success, failure]() {
        CHECK_EQ(child_pid_, -1);
        LOG(INFO) << "Subprocess terminated: " << Read(buffer_variables::name);
        PrepareToClose(success, failure);
      };
      return;
    }
    CHECK(options_.editor->modifiers().strength > Modifiers::Strength::kNormal);
  }
  if (!dirty()) {
    LOG(INFO) << Read(buffer_variables::name) << ": clean, skipping.";
    return success();
  }
  if (Read(buffer_variables::save_on_close)) {
    LOG(INFO) << Read(buffer_variables::name) << ": attempting to save buffer.";
    // TODO(alejo): Let Save give us status?
    Save();
    if (!dirty()) {
      LOG(INFO) << Read(buffer_variables::name) << ": successful save.";
      return success();
    }
  }
  if (Read(buffer_variables::allow_dirty_delete)) {
    LOG(INFO) << Read(buffer_variables::name)
              << ": allows dirty delete, skipping.";
    return success();
  }
  CHECK(options_.editor->modifiers().strength > Modifiers::Strength::kNormal);
  LOG(INFO) << Read(buffer_variables::name) << ": Deleting due to modifiers.";
  return success();
}

void OpenBuffer::Close() {
  LOG(INFO) << "Closing buffer: " << Read(buffer_variables::name);
  if (dirty() && Read(buffer_variables::save_on_close)) {
    LOG(INFO) << "Saving buffer: " << Read(buffer_variables::name);
    Save();
  }
  for (auto& observer : close_observers_) {
    observer();
  }
}

void OpenBuffer::AddEndOfFileObserver(std::function<void()> observer) {
  if (fd_ == nullptr && fd_error_ == nullptr) {
    observer();
    return;
  }
  end_of_file_observers_.push_back(std::move(observer));
}

void OpenBuffer::AddCloseObserver(std::function<void()> observer) {
  close_observers_.push_back(std::move(observer));
}

void OpenBuffer::Visit() {
  if (Read(buffer_variables::reload_on_enter)) {
    Reload();
    CheckPosition();
  }
  time(&last_visit_);
  time(&last_action_);
  if (options_.handle_visit != nullptr) {
    options_.handle_visit(this);
  }
}

time_t OpenBuffer::last_visit() const { return last_visit_; }
time_t OpenBuffer::last_action() const { return last_action_; }

bool OpenBuffer::PersistState() const {
  if (!Read(buffer_variables::persist_state)) {
    return true;
  }

  auto path_vector = options_.editor->edge_path();
  if (path_vector.empty()) {
    LOG(INFO) << "Empty edge path.";
    return false;
  }

  auto file_path = Read(buffer_variables::path);
  list<wstring> file_path_components;
  if (file_path.empty() || file_path[0] != '/') {
    status_.SetWarningText(L"Unable to persist buffer with empty path: " +
                           Read(buffer_variables::name) + L" " +
                           (dirty() ? L" (dirty)" : L" (clean)") + L" " +
                           (modified_ ? L"modified" : L"not modified"));
    return !dirty();
  }

  if (!DirectorySplit(file_path, &file_path_components)) {
    LOG(INFO) << "Unable to split path: " << file_path;
    return false;
  }

  file_path_components.push_front(L"state");

  wstring path = path_vector[0];
  LOG(INFO) << "PersistState: Preparing directory for state: " << path;
  for (auto& component : file_path_components) {
    path = PathJoin(path, component);
    struct stat stat_buffer;
    auto path_byte_string = ToByteString(path);
    if (stat(path_byte_string.c_str(), &stat_buffer) != -1) {
      if (S_ISDIR(stat_buffer.st_mode)) {
        continue;
      }
      LOG(INFO) << "Ooops, exists, but is not a directory: " << path;
      return false;
    }
    if (mkdir(path_byte_string.c_str(), 0700)) {
      status_.SetInformationText(L"mkdir: " + FromByteString(strerror(errno)) +
                                 L": " + path);
      return false;
    }
  }

  path = PathJoin(path, L".edge_state");
  LOG(INFO) << "PersistState: Preparing state file: " << path;
  BufferContents contents;
  contents.push_back(L"// State of file: " + path);
  contents.push_back(L"");

  contents.push_back(L"buffer.set_position(" + position().ToCppString() +
                     L");");
  contents.push_back(L"");

  contents.push_back(L"// String variables");
  for (const auto& variable : buffer_variables::StringStruct()->variables()) {
    contents.push_back(L"buffer.set_" + variable.first + L"(\"" +
                       CppEscapeString(Read(variable.second.get())) + L"\");");
  }
  contents.push_back(L"");

  contents.push_back(L"// Int variables");
  for (const auto& variable : buffer_variables::IntStruct()->variables()) {
    contents.push_back(L"buffer.set_" + variable.first + L"(" +
                       std::to_wstring(Read(variable.second.get())) + L");");
  }
  contents.push_back(L"");

  contents.push_back(L"// Bool variables");
  for (const auto& variable : buffer_variables::BoolStruct()->variables()) {
    contents.push_back(L"buffer.set_" + variable.first + L"(" +
                       (Read(variable.second.get()) ? L"true" : L"false") +
                       L");");
  }
  contents.push_back(L"");

  return SaveContentsToFile(path, contents, &status_);
}

void OpenBuffer::ClearContents(
    BufferContents::CursorsBehavior cursors_behavior) {
  VLOG(5) << "Clear contents of buffer: " << Read(buffer_variables::name);
  options_.editor->line_marks()->RemoveExpiredMarksFromSource(
      Read(buffer_variables::name));
  options_.editor->line_marks()->ExpireMarksFromSource(
      *this, Read(buffer_variables::name));
  contents_.EraseLines(LineNumber(0), LineNumber(0) + contents_.size(),
                       cursors_behavior);
  if (terminal_ != nullptr) {
    terminal_->SetPosition(LineColumn());
  }
  last_transformation_ = NewNoopTransformation();
  last_transformation_stack_.clear();
  transformations_past_.clear();
  transformations_future_.clear();
}

void OpenBuffer::AppendEmptyLine() {
  auto follower = GetEndPositionFollower();
  contents_.push_back(std::make_shared<Line>());
}

void OpenBuffer::EndOfFile() {
  time(&last_action_);
  CHECK(fd_ == nullptr);
  CHECK(fd_error_ == nullptr);
  if (child_pid_ != -1) {
    int exit_status;
    if (waitpid(child_pid_, &exit_status, 0) == -1) {
      status_.SetWarningText(L"waitpid failed: " +
                             FromByteString(strerror(errno)));
      return;
    }
    child_exit_status_ = exit_status;
    clock_gettime(0, &time_last_exit_);

    child_pid_ = -1;
    if (on_exit_handler_) {
      on_exit_handler_();
      on_exit_handler_ = nullptr;
    }
  }

  // We can remove expired marks now. We know that the set of fresh marks is now
  // complete.
  editor()->line_marks()->RemoveExpiredMarksFromSource(
      Read(buffer_variables::name));

  vector<std::function<void()>> observers;
  observers.swap(end_of_file_observers_);
  for (auto& observer : observers) {
    observer();
  }

  if (Read(buffer_variables::reload_after_exit)) {
    Set(buffer_variables::reload_after_exit,
        Read(buffer_variables::default_reload_after_exit));
    Reload();
  }
  if (Read(buffer_variables::close_after_clean_exit) &&
      child_exit_status_.has_value() && WIFEXITED(child_exit_status_.value()) &&
      WEXITSTATUS(child_exit_status_.value()) == 0) {
    editor()->CloseBuffer(this);
  }

  auto current_buffer = editor()->current_buffer();
  if (current_buffer != nullptr &&
      current_buffer->Read(buffer_variables::name) == kBuffersName) {
    current_buffer->Reload();
  }
}

std::unique_ptr<bool, std::function<void(bool*)>>
OpenBuffer::GetEndPositionFollower() {
  if (!Read(buffer_variables::follow_end_of_file)) {
    return nullptr;
  }
  if (position() < end_position() && terminal_ == nullptr) {
    return nullptr;  // Not at the end, so user must have scrolled up.
  }
  return std::unique_ptr<bool, std::function<void(bool*)>>(
      new bool(), [this](bool* value) {
        delete value;
        set_position(terminal_ != nullptr ? terminal_->position()
                                          : end_position());
      });
}

bool OpenBuffer::ShouldDisplayProgress() const {
  return (fd_ != nullptr || fd_error_ != nullptr) &&
         Read(buffer_variables::display_progress);
}

void OpenBuffer::RegisterProgress() {
  if (UpdateIfMillisecondsHavePassed(&last_progress_update_, 200).has_value()) {
    Set(buffer_variables::progress, Read(buffer_variables::progress) + 1);
  }
}

void OpenBuffer::ReadData() { ReadData(&fd_); }
void OpenBuffer::ReadErrorData() { ReadData(&fd_error_); }

void OpenBuffer::UpdateTreeParser() {
  auto parser = Read(buffer_variables::tree_parser);
  std::wistringstream typos_stream(Read(buffer_variables::typos));
  std::unordered_set<wstring> typos_set{
      std::istream_iterator<std::wstring, wchar_t>(typos_stream),
      std::istream_iterator<std::wstring, wchar_t>()};
  if (parser == L"text") {
    tree_parser_ = NewLineTreeParser(
        NewWordsTreeParser(Read(buffer_variables::symbol_characters), typos_set,
                           NewNullTreeParser()));
  } else if (parser == L"cpp") {
    std::wistringstream keywords(Read(buffer_variables::language_keywords));
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

  if (TreeParser::IsNull(tree_parser_.get())) {
    syntax_data_.PauseThread();  // TODO: Don't block until the thread is done?
  }
}

std::shared_ptr<const ParseTree> OpenBuffer::parse_tree() const {
  std::optional<const SyntaxDataOutput> data = syntax_data_.Get();
  return data.has_value() ? data.value().parse_tree
                          : std::make_shared<ParseTree>(Range());
}

std::shared_ptr<const ParseTree> OpenBuffer::simplified_parse_tree() const {
  std::optional<const SyntaxDataOutput> data = syntax_data_.Get();
  return data.has_value() ? data.value().simplified_parse_tree
                          : std::make_shared<ParseTree>(Range());
}

void OpenBuffer::StartNewLine(std::shared_ptr<Line> line) {
  static Tracker tracker(L"OpenBuffer::StartNewLine");
  auto tracker_call = tracker.Call();

  CHECK(line != nullptr);
  DVLOG(5) << "Line is completed: " << contents_.back()->ToString();

  if (Read(buffer_variables::contains_line_marks)) {
    static Tracker tracker(L"OpenBuffer::StartNewLine::ScanForMarks");
    auto tracker_call = tracker.Call();
    auto options = ResolvePathOptions::New(editor());
    options.path = contents_.back()->ToString();
    if (auto results = ResolvePath(std::move(options)); results.has_value()) {
      LineMarks::Mark mark;
      mark.source = Read(buffer_variables::name);
      mark.source_line = contents_.EndLine();
      mark.target_buffer = results->path;
      if (results->position.has_value()) {
        mark.target = *results->position;
      }
      LOG(INFO) << "Found a mark: " << mark;
      editor()->line_marks()->AddMark(mark);
    }
  }
  contents_.push_back(std::move(line));
}

void OpenBuffer::Reload() {
  if (child_pid_ != -1) {
    LOG(INFO) << "Sending SIGTERM.";
    kill(-child_pid_, SIGTERM);
    Set(buffer_variables::reload_after_exit, true);
    return;
  }

  switch (reload_state_) {
    case ReloadState::kDone:
      reload_state_ = ReloadState::kOngoing;
      break;
    case ReloadState::kOngoing:
      reload_state_ = ReloadState::kPending;
      return;
    case ReloadState::kPending:
      return;
  }

  // We need to wait until all instances of buffer-reload.cc have been
  // evaluated. To achieve that, we simply pass a function that depends on a
  // shared_ptr as the continuation. Once that function is deallocated, we'll
  // know that we're ready to run.
  std::shared_ptr<bool> reloader(new bool(), [this](bool* value) {
    delete value;
    if (options_.editor->exit_value().has_value()) return;
    ClearModified();
    LOG(INFO) << "Starting reload: " << Read(buffer_variables::name);
    if (options_.generate_contents != nullptr) {
      options_.generate_contents(this);
    }

    switch (reload_state_) {
      case ReloadState::kDone:
        LOG(FATAL) << "Invalid reload state! Can't be kDone.";
        break;
      case ReloadState::kOngoing:
        reload_state_ = ReloadState::kDone;
        break;
      case ReloadState::kPending:
        reload_state_ = ReloadState::kDone;
        Reload();
    }
  });

  for (const auto& dir : editor()->edge_path()) {
    EvaluateFile(PathJoin(dir, L"hooks/buffer-reload.cc"),
                 [reloader](std::unique_ptr<Value>) {});
  }
}

void OpenBuffer::Save() {
  LOG(INFO) << "Saving buffer: " << Read(buffer_variables::name);

  if (options_.handle_save != nullptr) {
    return options_.handle_save(this);
  }
  status_.SetWarningText(L"Buffer can't be saved.");
}

void OpenBuffer::AppendLazyString(std::shared_ptr<LazyString> input) {
  ColumnNumber start;
  ForEachColumn(*input, [&](ColumnNumber i, wchar_t c) {
    CHECK_GE(i, start);
    if (c == '\n') {
      AppendLine(Substring(input, start, i - start));
      start = i + ColumnNumberDelta(1);
    }
  });
  AppendLine(Substring(input, start));
}

static void AddToParseTree(const shared_ptr<LazyString>& str_input) {
  wstring str = str_input->ToString();
}

void OpenBuffer::SortContents(LineNumber first, LineNumber last,
                              std::function<bool(const shared_ptr<const Line>&,
                                                 const shared_ptr<const Line>&)>
                                  compare) {
  CHECK(first <= last);
  contents_.sort(first, last, compare);
}

LineNumberDelta OpenBuffer::lines_size() const { return contents_.size(); }

LineNumber OpenBuffer::EndLine() const { return contents_.EndLine(); }

void OpenBuffer::EraseLines(LineNumber first, LineNumber last) {
  CHECK_LE(first, last);
  CHECK_LE(last, LineNumber(0) + contents_.size());
  contents_.EraseLines(first, last, BufferContents::CursorsBehavior::kAdjust);
}

void OpenBuffer::InsertLine(LineNumber line_position, shared_ptr<Line> line) {
  contents_.insert_line(line_position, line);
}

void OpenBuffer::AppendLine(shared_ptr<LazyString> str) {
  CHECK(str != nullptr);
  if (reading_from_parser_) {
    switch (str->get(ColumnNumber(0))) {
      case 'E':
        return AppendRawLine(Substring(str, ColumnNumber(1)));

      case 'T':
        AddToParseTree(str);
        return;
    }
    return;
  }

  if (contents_.size() == LineNumberDelta(1) &&
      contents_.back()->EndColumn().IsZero()) {
    if (str->ToString() == L"EDGE PARSER v1.0") {
      reading_from_parser_ = true;
      return;
    }
  }

  AppendRawLine(str);
}

void OpenBuffer::AppendRawLine(std::shared_ptr<LazyString> str) {
  AppendRawLine(std::make_shared<Line>(Line::Options(str)));
}

void OpenBuffer::AppendRawLine(std::shared_ptr<Line> line) {
  auto follower = GetEndPositionFollower();
  contents_.push_back(line);
}

void OpenBuffer::AppendToLastLine(std::shared_ptr<LazyString> str) {
  Line::Options options;
  options.contents = std::move(str);
  AppendToLastLine(Line(std::move(options)));
}

void OpenBuffer::AppendToLastLine(Line line) {
  static Tracker tracker(L"OpenBuffer::AppendToLastLine");
  auto tracker_call = tracker.Call();
  auto follower = GetEndPositionFollower();
  contents_.AppendToLine(LineNumber(0) + contents_.size(), std::move(line));
}

unique_ptr<Expression> OpenBuffer::CompileString(const wstring& code,
                                                 wstring* error_description) {
  return afc::vm::CompileString(code, &environment_, error_description);
}

void OpenBuffer::EvaluateExpression(
    Expression* expr, std::function<void(std::unique_ptr<Value>)> consumer) {
  Evaluate(expr, &environment_, consumer,
           [this](std::function<void()> callback) {
             SchedulePendingWork(std::move(callback));
           });
}

bool OpenBuffer::EvaluateString(
    const wstring& code, std::function<void(std::unique_ptr<Value>)> consumer) {
  wstring error_description;
  LOG(INFO) << "Compiling code.";
  // TODO: Use unique_ptr and capture by value.
  std::shared_ptr<Expression> expression =
      CompileString(code, &error_description);
  if (expression == nullptr) {
    status_.SetWarningText(L"🐜Compilation error: " + error_description);
    return false;
  }
  LOG(INFO) << "Code compiled, evaluating.";
  EvaluateExpression(
      expression.get(),
      [expression, consumer](Value::Ptr value) { consumer(std::move(value)); });
  return true;
}

bool OpenBuffer::EvaluateFile(
    const wstring& path, std::function<void(std::unique_ptr<Value>)> callback) {
  wstring error_description;
  std::shared_ptr<Expression> expression =
      CompileFile(ToByteString(path), &environment_, &error_description);
  if (expression == nullptr) {
    status_.SetWarningText(path + L": error: " + error_description);
    return false;
  }
  LOG(INFO) << "Evaluating file: " << path;
  Evaluate(
      expression.get(), &environment_,
      [path, callback, expression](std::unique_ptr<Value> value) {
        LOG(INFO) << "Evaluation of file completed: " << path;
        callback(std::move(value));
      },
      [path, this](std::function<void()> resume) {
        LOG(INFO) << "Evaluation of file yields: " << path;
        SchedulePendingWork(std::move(resume));
      });
  return true;
}

void OpenBuffer::SchedulePendingWork(std::function<void()> callback) {
  work_queue_.Schedule(std::move(callback));
  options_.editor->NotifyInternalEvent();
}

void OpenBuffer::ExecutePendingWork() { work_queue_.Execute(); }
WorkQueue::State OpenBuffer::GetPendingWorkState() const {
  return work_queue_.state();
}

void OpenBuffer::DeleteRange(const Range& range) {
  if (range.begin.line == range.end.line) {
    contents_.DeleteCharactersFromLine(range.begin,
                                       range.end.column - range.begin.column);
  } else {
    contents_.DeleteToLineEnd(range.begin);
    contents_.DeleteCharactersFromLine(LineColumn(range.end.line),
                                       range.end.column.ToDelta());
    // Lines in the middle.
    EraseLines(range.begin.line + LineNumberDelta(1), range.end.line);
    contents_.FoldNextLine(range.begin.line);
  }
}

LineColumn OpenBuffer::InsertInPosition(
    const OpenBuffer& buffer, const LineColumn& input_position,
    const std::optional<LineModifierSet>& modifiers) {
  VLOG(5) << "InsertInPosition: " << input_position << " "
          << (modifiers.has_value() ? modifiers.value().size() : -1);
  auto blocker = cursors_tracker_.DelayTransformations();
  LineColumn position = input_position;
  if (position.line > contents_.EndLine()) {
    position.line = contents_.EndLine();
    position.column = contents_.at(position.line)->EndColumn();
  }
  if (position.column > contents_.at(position.line)->EndColumn()) {
    position.column = contents_.at(position.line)->EndColumn();
  }
  contents_.SplitLine(position);
  contents_.insert(position.line.next(), buffer.contents_, modifiers);
  contents_.FoldNextLine(position.line);

  LineNumber last_line =
      position.line + buffer.contents_.size() - LineNumberDelta(1);
  CHECK_LE(last_line, EndLine());
  auto line = LineAt(last_line);
  CHECK(line != nullptr);
  ColumnNumber column = line->EndColumn();

  contents_.FoldNextLine(last_line);
  return LineColumn(last_line, column);
}

void OpenBuffer::AdjustLineColumn(LineColumn* output) const {
  CHECK_GT(contents_.size(), LineNumberDelta(0));
  output->line = min(output->line, contents_.EndLine());
  CHECK(LineAt(output->line) != nullptr);
  output->column = min(LineAt(output->line)->EndColumn(), output->column);
}

void OpenBuffer::MaybeAdjustPositionCol() {
  if (current_line() == nullptr) {
    return;
  }
  set_current_position_col(
      std::min(position().column, current_line()->EndColumn()));
}

void OpenBuffer::MaybeExtendLine(LineColumn position) {
  CHECK_LE(position.line, contents_.EndLine());
  auto line = std::make_shared<Line>(*LineAt(position.line));
  if (line->EndColumn() > position.column + ColumnNumberDelta(1)) {
    return;
  }
  line->Append(Line(ColumnNumberDelta::PaddingString(
      position.column - line->EndColumn() + ColumnNumberDelta(1), L' ')));

  contents_.set_line(position.line, line);
}

void OpenBuffer::CheckPosition() {
  if (position().line > contents_.EndLine()) {
    set_position(LineColumn(contents_.EndLine()));
  }
}

CursorsSet* OpenBuffer::FindCursors(const wstring& name) {
  return cursors_tracker_.FindOrCreateCursors(name);
}

CursorsSet* OpenBuffer::active_cursors() {
  return const_cast<CursorsSet*>(
      const_cast<const OpenBuffer*>(this)->active_cursors());
}

const CursorsSet* OpenBuffer::active_cursors() const {
  // TODO: Remove const cast. Ugh.
  return const_cast<OpenBuffer*>(this)->FindCursors(
      options_.editor->modifiers().active_cursors);
}

void OpenBuffer::set_active_cursors(const vector<LineColumn>& positions) {
  if (positions.empty()) {
    return;
  }
  auto cursors = active_cursors();
  FindCursors(kOldCursors)->swap(cursors);
  cursors->clear();
  cursors->insert(positions.begin(), positions.end());

  // We find the first position (rather than just take cursors->begin()) so that
  // we start at the first requested position.
  cursors->SetCurrentCursor(positions.front());
}

void OpenBuffer::ToggleActiveCursors() {
  LineColumn desired_position = position();

  auto cursors = active_cursors();
  FindCursors(kOldCursors)->swap(cursors);

  // TODO: Maybe it'd be best to pick the nearest after the cursor?
  // TODO: This should probably be merged somewhat with set_active_cursors?
  for (auto it = cursors->begin(); it != cursors->end(); ++it) {
    if (desired_position == *it) {
      LOG(INFO) << "Desired position " << desired_position << " prevails.";
      cursors->SetCurrentCursor(desired_position);
      CHECK_LE(position().line, LineNumber(0) + lines_size());
      return;
    }
  }

  cursors->SetCurrentCursor(*cursors->begin());
  LOG(INFO) << "Picked up the first cursor: " << position();
  CHECK_LE(position().line, LineNumber(0) + contents_.size());
}

void OpenBuffer::PushActiveCursors() {
  auto stack_size = cursors_tracker_.Push();
  status_.SetInformationText(L"cursors stack (" + to_wstring(stack_size) +
                             L"): +");
}

void OpenBuffer::PopActiveCursors() {
  auto stack_size = cursors_tracker_.Pop();
  if (stack_size == 0) {
    status_.SetWarningText(L"cursors stack: -: Stack is empty!");
    return;
  }
  status_.SetInformationText(L"cursors stack (" + to_wstring(stack_size - 1) +
                             L"): -");
}

void OpenBuffer::SetActiveCursorsToMarks() {
  const auto& marks = *GetLineMarks();
  if (marks.empty()) {
    status_.SetWarningText(L"Buffer has no marks!");
    return;
  }

  std::vector<LineColumn> cursors;
  for (auto& it : marks) {
    cursors.push_back(it.second.target);
  }
  set_active_cursors(cursors);
}

void OpenBuffer::set_current_cursor(LineColumn new_value) {
  auto cursors = active_cursors();
  // Need to do find + erase because cursors is a multiset; we only want to
  // erase one cursor, rather than all cursors with the current value.
  cursors->erase(position());
  cursors->insert(new_value);
  cursors->SetCurrentCursor(new_value);
}

void OpenBuffer::CreateCursor() {
  if (options_.editor->modifiers().structure == StructureChar()) {
    CHECK_LE(position().line, LineNumber(0) + contents_.size());
    active_cursors()->insert(position());
  } else {
    auto structure = options_.editor->modifiers().structure;
    Modifiers tmp_modifiers = options_.editor->modifiers();
    tmp_modifiers.structure = StructureCursor();
    Range range = FindPartialRange(tmp_modifiers, position());
    if (range.IsEmpty()) {
      return;
    }
    options_.editor->set_direction(FORWARDS);
    LOG(INFO) << "Range for cursors: " << range;
    while (!range.IsEmpty()) {
      auto tmp_first = range.begin;
      structure->SeekToNext(this, FORWARDS, &tmp_first);
      if (tmp_first > range.begin && tmp_first < range.end) {
        VLOG(5) << "Creating cursor at: " << tmp_first;
        active_cursors()->insert(tmp_first);
      }
      if (!structure->SeekToLimit(this, FORWARDS, &tmp_first)) {
        break;
      }
      range.begin = tmp_first;
    }
  }
  status_.SetInformationText(L"Cursor created.");
}

LineColumn OpenBuffer::FindNextCursor(LineColumn position) {
  LOG(INFO) << "Visiting next cursor: " << options_.editor->modifiers();
  auto direction = options_.editor->modifiers().direction;
  auto cursors = active_cursors();
  CHECK(!cursors->empty());

  size_t index = 0;
  auto output = cursors->begin();
  while (output != cursors->end() &&
         (*output < position || (direction == FORWARDS && *output == position &&
                                 std::next(output) != cursors->end() &&
                                 *std::next(output) == position))) {
    ++output;
    ++index;
  }

  size_t repetitions =
      options_.editor->modifiers().repetitions % cursors->size();
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
  return *output;
}

void OpenBuffer::DestroyCursor() {
  auto cursors = active_cursors();
  if (cursors->size() <= 1) {
    return;
  }
  size_t repetitions =
      min(options_.editor->modifiers().repetitions, cursors->size() - 1);
  for (size_t i = 0; i < repetitions; i++) {
    cursors->DeleteCurrentCursor();
  }
  CHECK_LE(position().line, LineNumber(0) + contents_.size());
}

void OpenBuffer::DestroyOtherCursors() {
  CheckPosition();
  auto position = this->position();
  CHECK_LE(position, LineColumn(LineNumber(0) + contents_.size()));
  auto cursors = active_cursors();
  cursors->clear();
  cursors->insert(position);
  Set(buffer_variables::multiple_cursors, false);
}

Range OpenBuffer::FindPartialRange(const Modifiers& modifiers,
                                   const LineColumn& initial_position) {
  Range output;
  const auto forward = modifiers.direction;
  const auto backward = ReverseDirection(forward);

  LineColumn position = initial_position;
  position.line = min(contents_.EndLine(), position.line);
  if (position.column > LineAt(position.line)->EndColumn()) {
    if (Read(buffer_variables::extend_lines)) {
      MaybeExtendLine(position);
    } else {
      position.column = LineAt(position.line)->EndColumn();
    }
  }

  if (modifiers.direction == BACKWARDS &&
      modifiers.structure != StructureTree()) {
    // TODO: Handle this in structure.
    Seek(contents_, &position).Backwards().WrappingLines().Once();
  }

  output.begin = position;
  LOG(INFO) << "Initial position: " << position
            << ", structure: " << modifiers.structure->ToString();
  if (modifiers.structure->space_behavior() ==
      Structure::SpaceBehavior::kForwards) {
    modifiers.structure->SeekToNext(this, forward, &output.begin);
  }
  switch (modifiers.boundary_begin) {
    case Modifiers::CURRENT_POSITION:
      output.begin = modifiers.direction == FORWARDS
                         ? max(position, output.begin)
                         : min(position, output.begin);
      break;

    case Modifiers::LIMIT_CURRENT: {
      if (modifiers.structure->SeekToLimit(this, backward, &output.begin)) {
        Seek(contents_, &output.begin)
            .WrappingLines()
            .WithDirection(forward)
            .Once();
      }
    } break;

    case Modifiers::LIMIT_NEIGHBOR:
      if (modifiers.structure->SeekToLimit(this, backward, &output.begin)) {
        modifiers.structure->SeekToNext(this, backward, &output.begin);
        modifiers.structure->SeekToLimit(this, forward, &output.begin);
      }
  }
  LOG(INFO) << "After seek, initial position: " << output.begin;
  output.end = modifiers.direction == FORWARDS ? max(position, output.begin)
                                               : min(position, output.begin);
  bool move_start = true;
  for (size_t i = 0; i < modifiers.repetitions - 1; i++) {
    LineColumn position = output.end;
    if (!modifiers.structure->SeekToLimit(this, forward, &output.end)) {
      move_start = false;
      break;
    }
    modifiers.structure->SeekToNext(this, forward, &output.end);
    if (output.end == position) {
      break;
    }
  }

  switch (modifiers.boundary_end) {
    case Modifiers::CURRENT_POSITION:
      break;

    case Modifiers::LIMIT_CURRENT:
      move_start &=
          modifiers.structure->SeekToLimit(this, forward, &output.end);
      break;

    case Modifiers::LIMIT_NEIGHBOR:
      move_start &=
          modifiers.structure->SeekToLimit(this, forward, &output.end);
      modifiers.structure->SeekToNext(this, forward, &output.end);
  }
  LOG(INFO) << "After adjusting end: " << output;

  if (output.begin > output.end) {
    CHECK(modifiers.direction == BACKWARDS);
    auto tmp = output.end;
    output.end = output.begin;
    output.begin = tmp;
    if (move_start) {
      Seek(contents_, &output.begin).WrappingLines().Once();
    }
  }
  LOG(INFO) << "After wrap: " << output;
  return output;
}

const ParseTree* OpenBuffer::current_tree(const ParseTree* root) const {
  CHECK(root);
  auto route = FindRouteToPosition(*root, position());
  if (route.size() < tree_depth_) {
    return root;
  }
  if (route.size() > tree_depth_) {
    route.resize(tree_depth_);
  }
  return FollowRoute(*root, route);
}

std::shared_ptr<const ParseTree> OpenBuffer::current_zoomed_out_parse_tree(
    LineNumberDelta view_size) const {
  if (auto it = zoomed_out_parse_trees_.find(view_size);
      it != zoomed_out_parse_trees_.end()) {
    CHECK(it->second != nullptr);
    return it->second;
  }

  SyntaxDataZoomInput input;
  input.buffer = this;
  input.lines_size = lines_size();
  input.view_size = view_size;
  input.simplified_parse_tree = simplified_parse_tree();
  if (input.simplified_parse_tree != nullptr) {
    syntax_data_zoom_.Push(std::move(input));
  }
  return std::make_shared<ParseTree>(Range());
}

std::unique_ptr<BufferTerminal> OpenBuffer::NewTerminal() {
  return std::make_unique<BufferTerminal>(this, &contents_);
}

const std::shared_ptr<const Line> OpenBuffer::current_line() const {
  return LineAt(position().line);
}

std::shared_ptr<const Line> OpenBuffer::LineAt(LineNumber line_number) const {
  if (line_number > contents_.EndLine()) {
    return nullptr;
  }
  return contents_.at(line_number);
}

std::shared_ptr<OpenBuffer> OpenBuffer::GetBufferFromCurrentLine() {
  if (current_line() == nullptr) {
    return nullptr;
  }
  auto target = current_line()->environment()->Lookup(
      L"buffer", vm::VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::vmtype);
  if (target == nullptr) {
    return nullptr;
  }
  return std::static_pointer_cast<OpenBuffer>(target->user_value);
}

wstring OpenBuffer::ToString() const { return contents_.ToString(); }

const struct timespec OpenBuffer::time_last_exit() const {
  return time_last_exit_;
}

void OpenBuffer::PushSignal(int sig) {
  switch (sig) {
    case SIGINT:
      if (terminal_ == nullptr ? child_pid_ == -1 : fd_ == nullptr) {
        status_.SetWarningText(L"No subprocess found.");
      } else if (terminal_ == nullptr) {
        status_.SetInformationText(L"SIGINT >> pid:" + to_wstring(child_pid_));
        kill(child_pid_, sig);
      } else {
        string sequence(1, 0x03);
        (void)write(fd_->fd(), sequence.c_str(), sequence.size());
        status_.SetInformationText(L"SIGINT");
      }
      break;

    case SIGTSTP:
      static const string sequence(1, 0x1a);
      if (terminal_ != nullptr && fd_ != nullptr) {
        (void)write(fd_->fd(), sequence.c_str(), sequence.size());
      }
      break;

    default:
      status_.SetWarningText(L"Unexpected signal received: " + to_wstring(sig));
  }
}

Viewers* OpenBuffer::viewers() { return &viewers_; }

wstring OpenBuffer::TransformKeyboardText(wstring input) {
  using afc::vm::VMType;
  for (Value::Ptr& t : keyboard_text_transformers_) {
    vector<Value::Ptr> args;
    args.push_back(Value::NewString(std::move(input)));
    Call(
        t.get(), std::move(args),
        [&input](Value::Ptr value) { input = std::move(value->str); },
        [this](std::function<void()> callback) {
          SchedulePendingWork(std::move(callback));
        });
  }
  return input;
}

bool OpenBuffer::AddKeyboardTextTransformer(unique_ptr<Value> transformer) {
  if (transformer == nullptr || transformer->type.type != VMType::FUNCTION ||
      transformer->type.type_arguments.size() != 2 ||
      transformer->type.type_arguments[0].type != VMType::VM_STRING ||
      transformer->type.type_arguments[1].type != VMType::VM_STRING) {
    status_.SetWarningText(
        L": Unexpected type for keyboard text transformer: " +
        transformer->type.ToString());
    return false;
  }
  keyboard_text_transformers_.push_back(std::move(transformer));
  return true;
}

void OpenBuffer::SetInputFiles(int input_fd, int input_error_fd,
                               bool fd_is_terminal, pid_t child_pid) {
  if (Read(buffer_variables::clear_on_reload)) {
    ClearContents(BufferContents::CursorsBehavior::kUnmodified);
    ClearModified();
  }

  CHECK_EQ(child_pid_, -1);
  terminal_ = fd_is_terminal ? NewTerminal() : nullptr;

  auto new_reader = [this](int fd, LineModifierSet modifiers)
      -> std::unique_ptr<FileDescriptorReader> {
    if (fd == -1) {
      return nullptr;
    }
    FileDescriptorReader::Options options;
    options.buffer = this;
    options.terminal = terminal_.get();
    options.fd = fd;
    options.modifiers = std::move(modifiers);
    options.background_callback_runner = &background_read_runner_;
    return std::make_unique<FileDescriptorReader>(std::move(options));
  };

  fd_ = new_reader(input_fd, {});
  fd_error_ = new_reader(input_error_fd, {LineModifier::RED});

  if (terminal_ != nullptr) {
    terminal_->UpdateSize();
  }

  child_pid_ = child_pid;
}

const FileDescriptorReader* OpenBuffer::fd() const { return fd_.get(); }

const FileDescriptorReader* OpenBuffer::fd_error() const {
  return fd_error_.get();
}

LineNumber OpenBuffer::current_position_line() const { return position().line; }

void OpenBuffer::set_current_position_line(LineNumber line) {
  set_current_cursor(LineColumn(min(line, LineNumber(0) + contents_.size())));
}

ColumnNumber OpenBuffer::current_position_col() const {
  return position().column;
}

void OpenBuffer::set_current_position_col(ColumnNumber column) {
  set_current_cursor(LineColumn(position().line, column));
}

LineColumn OpenBuffer::PositionBefore(LineColumn position) const {
  if (position.column > ColumnNumber(0)) {
    position.column--;
  } else if (position.line > LineNumber(0)) {
    position.line = min(position.line, LineNumber(0) + contents_.size()) -
                    LineNumberDelta(1);
    position.column = contents_.at(position.line)->EndColumn();
  }
  return position;
}

const LineColumn OpenBuffer::position() const {
  return cursors_tracker_.position();
}

void OpenBuffer::set_position(const LineColumn& position) {
  set_current_cursor(position);
}

LineColumn OpenBuffer::end_position() const {
  CHECK_GT(contents_.size(), LineNumberDelta(0));
  return LineColumn(contents_.EndLine(), contents_.back()->EndColumn());
}

bool OpenBuffer::dirty() const {
  return (modified_ && (!Read(buffer_variables::path).empty() ||
                        !contents()->EveryLine([](LineNumber, const Line& l) {
                          return l.empty();
                        }))) ||
         child_pid_ != -1 ||
         (child_exit_status_.has_value() &&
          (!WIFEXITED(child_exit_status_.value()) ||
           WEXITSTATUS(child_exit_status_.value()) != 0));
}

std::map<wstring, wstring> OpenBuffer::Flags() const {
  std::map<wstring, wstring> output;
  if (options_.describe_status) {
    output = options_.describe_status(*this);
  }

  if (modified()) {
    output.insert({L"🐾", L""});
  }

  if (ShouldDisplayProgress()) {
    output.insert({ProgressString(Read(buffer_variables::progress),
                                  OverflowBehavior::kModulo),
                   L""});
  }

  if (fd() != nullptr) {
    output.insert({L"<", L""});
    switch (contents_.size().line_delta) {
      case 1:
        output.insert({L"⚊", L""});
        break;
      case 2:
        output.insert({L"⚌ ", L""});
        break;
      case 3:
        output.insert({L"☰ ", L""});
        break;
      default:
        output.insert({L"☰ ", contents_.EndLine().ToUserString()});
    }
    if (Read(buffer_variables::follow_end_of_file)) {
      output.insert({L"↓", L""});
    }
    wstring pts_path = Read(buffer_variables::pts_path);
    if (!pts_path.empty()) {
      output.insert({L"💻", pts_path});
    }
  }

  if (GetPendingWorkState() != WorkQueue::State::kIdle) {
    output.insert({L"⏳", L""});
  }

  if (child_pid_ != -1) {
    output.insert({L"🤴", std::to_wstring(child_pid_)});
  } else if (!child_exit_status_.has_value()) {
    // Nothing.
  } else if (WIFEXITED(child_exit_status_.value())) {
    output.insert(
        {L"exit", std::to_wstring(WEXITSTATUS(child_exit_status_.value()))});
  } else if (WIFSIGNALED(child_exit_status_.value())) {
    output.insert(
        {L"signal", std::to_wstring(WTERMSIG(child_exit_status_.value()))});
  } else {
    output.insert(
        {L"exit-status", std::to_wstring(child_exit_status_.value())});
  }

  auto marks = GetLineMarksText();
  if (!marks.empty()) {
    output.insert({marks, L""});  // TODO: Show better?
  }

  return output;
}

/* static */ wstring OpenBuffer::FlagsToString(
    std::map<wstring, wstring> flags) {
  wstring output;
  wstring separator = L"";
  for (auto& f : flags) {
    output += separator + f.first + f.second;
    separator = L"  ";
  }
  return output;
}

const bool& OpenBuffer::Read(const EdgeVariable<bool>* variable) const {
  return bool_variables_.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<bool>* variable, bool value) {
  bool_variables_.Set(variable, value);
}

void OpenBuffer::toggle_bool_variable(const EdgeVariable<bool>* variable) {
  Set(variable, !Read(variable));
}

const wstring& OpenBuffer::Read(const EdgeVariable<wstring>* variable) const {
  return string_variables_.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<wstring>* variable, wstring value) {
  string_variables_.Set(variable, value);

  // TODO: This should be in the variable definition, not here. Ugh.
  if (variable == buffer_variables::symbol_characters ||
      variable == buffer_variables::tree_parser ||
      variable == buffer_variables::language_keywords ||
      variable == buffer_variables::typos) {
    UpdateTreeParser();
  }
}

const int& OpenBuffer::Read(const EdgeVariable<int>* variable) const {
  return int_variables_.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<int>* variable, int value) {
  int_variables_.Set(variable, value);
}

const double& OpenBuffer::Read(const EdgeVariable<double>* variable) const {
  return double_variables_.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<double>* variable, double value) {
  double_variables_.Set(variable, value);
}

void OpenBuffer::ApplyToCursors(unique_ptr<Transformation> transformation) {
  ApplyToCursors(std::move(transformation),
                 Read(buffer_variables::multiple_cursors)
                     ? Modifiers::AFFECT_ALL_CURSORS
                     : Modifiers::AFFECT_ONLY_CURRENT_CURSOR,
                 Transformation::Result::Mode::kFinal);
}

void OpenBuffer::ApplyToCursors(unique_ptr<Transformation> transformation,
                                Modifiers::CursorsAffected cursors_affected,
                                Transformation::Result::Mode mode) {
  CHECK(transformation != nullptr);

  if (!last_transformation_stack_.empty()) {
    CHECK(last_transformation_stack_.back() != nullptr);
    last_transformation_stack_.back()->PushBack(transformation->Clone());
    CHECK(!transformations_past_.empty());
  } else {
    transformations_past_.push_back(
        std::make_unique<Transformation::Result>(editor()));
  }

  transformations_past_.back()->undo_stack->PushFront(
      NewSetCursorsTransformation(*active_cursors(), position()));

  transformations_past_.back()->mode = mode;

  if (cursors_affected == Modifiers::AFFECT_ALL_CURSORS) {
    CursorsSet single_cursor;
    CursorsSet* cursors = active_cursors();
    CHECK(cursors != nullptr);
    cursors_tracker_.ApplyTransformationToCursors(
        cursors, [this, &transformation, mode](LineColumn old_position) {
          transformations_past_.back()->cursor = old_position;
          auto new_position = Apply(transformation->Clone());
          return new_position;
        });
  } else {
    transformations_past_.back()->cursor = position();
    auto new_position = Apply(transformation->Clone());
    VLOG(6) << "Adjusting default cursor (!multiple_cursors).";
    active_cursors()->MoveCurrentCursor(new_position);
  }

  transformations_future_.clear();
  if (transformations_past_.back()->modified_buffer) {
    options_.editor->StartHandlingInterrupts();
    last_transformation_ = std::move(transformation);
  }
}

LineColumn OpenBuffer::Apply(unique_ptr<Transformation> transformation) {
  CHECK(transformation != nullptr);
  CHECK(!transformations_past_.empty());

  transformation->Apply(this, transformations_past_.back().get());
  CHECK(!transformations_past_.empty());

  auto delete_buffer = transformations_past_.back()->delete_buffer;
  CHECK(delete_buffer != nullptr);
  if ((delete_buffer->contents()->size() > LineNumberDelta(1) ||
       delete_buffer->LineAt(LineNumber(0))->EndColumn() > ColumnNumber(0)) &&
      Read(buffer_variables::delete_into_paste_buffer)) {
    auto insert_result = editor()->buffers()->insert(
        make_pair(delete_buffer->Read(buffer_variables::name), delete_buffer));
    if (!insert_result.second) {
      insert_result.first->second = delete_buffer;
    }
  }

  return transformations_past_.back()->cursor;
}

void OpenBuffer::RepeatLastTransformation() {
  int repetitions = options_.editor->repetitions();
  options_.editor->ResetRepetitions();
  ApplyToCursors(NewApplyRepetitionsTransformation(
      repetitions, last_transformation_->Clone()));
}

void OpenBuffer::PushTransformationStack() {
  if (last_transformation_stack_.empty()) {
    transformations_past_.push_back(
        std::make_unique<Transformation::Result>(editor()));
  }
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

void OpenBuffer::Undo() { Undo(SKIP_IRRELEVANT); };

void OpenBuffer::Undo(UndoMode undo_mode) {
  list<unique_ptr<Transformation::Result>>* source;
  list<unique_ptr<Transformation::Result>>* target;
  if (editor()->direction() == FORWARDS) {
    source = &transformations_past_;
    target = &transformations_future_;
  } else {
    source = &transformations_future_;
    target = &transformations_past_;
  }
  for (size_t i = 0; i < editor()->repetitions(); i++) {
    bool modified_buffer = false;
    while (!modified_buffer && !source->empty()) {
      target->emplace_back(std::make_unique<Transformation::Result>(editor()));
      source->back()->undo_stack->Apply(this, target->back().get());
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

bool OpenBuffer::IsLineFiltered(LineNumber line_number) {
  if (line_number > contents_.EndLine()) {
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

const multimap<size_t, LineMarks::Mark>* OpenBuffer::GetLineMarks() const {
  auto marks = editor()->line_marks();
  if (marks->updates > line_marks_last_updates_) {
    LOG(INFO) << Read(buffer_variables::name) << ": Updating marks.";
    line_marks_.clear();
    auto relevant_marks =
        marks->GetMarksForTargetBuffer(Read(buffer_variables::name));
    for (auto& mark : relevant_marks) {
      line_marks_.insert(make_pair(mark.target.line.line, mark));
    }
    line_marks_last_updates_ = marks->updates;
  }
  VLOG(10) << "Returning multimap with size: " << line_marks_.size();
  return &line_marks_;
}

wstring OpenBuffer::GetLineMarksText() const {
  const auto* marks = GetLineMarks();
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
         (position.line < contents_.EndLine() ||
          (position.line == contents_.EndLine() &&
           position.column <= LineAt(position.line)->EndColumn()));
}

void OpenBuffer::ReadData(std::unique_ptr<FileDescriptorReader>* source) {
  CHECK(source != nullptr);
  CHECK(*source != nullptr);
  if ((*source)->ReadData() == FileDescriptorReader::ReadResult::kDone) {
    RegisterProgress();
    (*source) = nullptr;
    if (fd_ == nullptr && fd_error_ == nullptr) {
      EndOfFile();
    }
  }
}

}  // namespace editor
}  // namespace afc

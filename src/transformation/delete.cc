#include "src/transformation/delete.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/modifiers.h"
#include "src/transformation.h"
#include "src/transformation/insert.h"
#include "src/transformation/move.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/function_call.h"
#include "src/vm_transformation.h"
#include "src/wstring.h"

namespace afc {
namespace vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::DeleteOptions>> {
  static std::shared_ptr<editor::DeleteOptions> get(Value* value) {
    CHECK(value != nullptr);
    CHECK(value->type.type == VMType::OBJECT_TYPE);
    CHECK(value->type.object_type == L"DeleteTransformationBuilder");
    CHECK(value->user_value != nullptr);
    return std::static_pointer_cast<editor::DeleteOptions>(value->user_value);
  }
  static Value::Ptr New(std::shared_ptr<editor::DeleteOptions> value) {
    return Value::NewObject(L"DeleteTransformationBuilder",
                            std::shared_ptr<void>(value, value.get()));
  }
  static const VMType vmtype;
};

const VMType VMTypeMapper<std::shared_ptr<editor::DeleteOptions>>::vmtype =
    VMType::ObjectType(L"DeleteTransformationBuilder");
}  // namespace vm
namespace editor {
std::ostream& operator<<(std::ostream& os, const DeleteOptions& options) {
  os << "[DeleteOptions: copy_to_paste_buffer:" << options.copy_to_paste_buffer
     << ", modifiers:" << options.modifiers << "]";
  return os;
}

std::wstring DeleteOptions::Serialize() const {
  std::wstring output = L"DeleteTransformationBuilder()";
  output += L".set_modifiers(" + modifiers.Serialize() + L")";
  if (!copy_to_paste_buffer) {
    output += L".set_copy_to_paste_buffer(false)";
  }
  if (line_end_behavior != LineEndBehavior::kDelete) {
    output += L".set_line_end_behavior(\"stop\")";
  }
  return output;
}

namespace {
// Copy to a new buffer the contents of `range`.
std::shared_ptr<OpenBuffer> GetDeletedTextBuffer(const OpenBuffer& buffer,
                                                 Range range) {
  LOG(INFO) << "Preparing deleted text buffer: " << range;
  auto delete_buffer =
      std::make_shared<OpenBuffer>(buffer.editor(), OpenBuffer::kPasteBuffer);
  for (LineNumber i = range.begin.line; i <= range.end.line; ++i) {
    Line::Options line(*buffer.LineAt(i));
    if (i == range.end.line) {
      line.DeleteSuffix(range.end.column);
    }
    if (i == range.begin.line) {
      line.DeleteCharacters(ColumnNumber(0), range.begin.column.ToDelta());
      delete_buffer->AppendToLastLine(Line(std::move(line)));
    } else {
      delete_buffer->AppendRawLine(std::make_shared<Line>(line));
    }
  }

  return delete_buffer;
}

// Calls the callbacks in the line (EdgeLineDeleteHandler).
void HandleLineDeletion(LineColumn position, OpenBuffer* buffer) {
  CHECK(buffer != nullptr);
  position = buffer->AdjustLineColumn(position);
  CHECK_GE(buffer->contents()->size(), position.line.ToDelta());

  LOG(INFO) << "Erasing line " << position.line << " in a buffer with size "
            << buffer->contents()->size();

  const auto contents = buffer->LineAt(position.line);
  DVLOG(5) << "Erasing line: " << contents->ToString();
  if (!position.column.IsZero()) return;
  auto target_buffer = buffer->GetBufferFromCurrentLine();
  if (target_buffer.get() != buffer && target_buffer != nullptr) {
    target_buffer->editor()->CloseBuffer(target_buffer.get());
  }

  if (contents == nullptr) return;
  Value* callback = contents->environment()->Lookup(
      L"EdgeLineDeleteHandler", VMType::Function({VMType::Void()}));
  if (callback == nullptr) return;
  LOG(INFO) << "Running EdgeLineDeleteHandler.";
  std::shared_ptr<Expression> expr = vm::NewFunctionCall(
      vm::NewConstantExpression(std::make_unique<Value>(*callback)), {});
  Evaluate(
      expr.get(), buffer->environment(), [expr](Value::Ptr) {},
      [work_queue = target_buffer->work_queue()](
          std::function<void()> callback) { work_queue->Schedule(callback); });
}

class DeleteTransformation : public Transformation {
 public:
  DeleteTransformation(DeleteOptions options) : options_(std::move(options)) {}

  std::wstring Serialize() const { return options_.Serialize() + L".build()"; }

  Result Apply(const Input& original_input) const {
    Input input = original_input;
    CHECK(input.buffer != nullptr);
    input.mode = options_.mode.value_or(input.mode);

    Result output(input.buffer->AdjustLineColumn(input.position));
    Range range =
        input.buffer->FindPartialRange(options_.modifiers, output.position);
    range.begin = min(range.begin, output.position);
    range.end = max(range.end, output.position);

    CHECK_LE(range.begin, range.end);
    if (range.IsEmpty()) {
      VLOG(5) << "No repetitions.";
      return output;
    }

    if (options_.modifiers.delete_type == Modifiers::DELETE_CONTENTS &&
        input.mode == Transformation::Input::Mode::kFinal) {
      LOG(INFO) << "Deleting superfluous lines (from " << range << ")";
      for (LineColumn delete_position = range.begin;
           delete_position.line < range.end.line;
           delete_position = LineColumn(delete_position.line.next())) {
        HandleLineDeletion(delete_position, input.buffer);
      }
    }

    output.success = true;
    output.made_progress = true;

    auto delete_buffer = GetDeletedTextBuffer(*input.buffer, range);
    if (options_.copy_to_paste_buffer &&
        input.mode == Transformation::Input::Mode::kFinal) {
      VLOG(5) << "Preparing delete buffer.";
      output.delete_buffer = delete_buffer;
    }

    if (options_.modifiers.delete_type == Modifiers::PRESERVE_CONTENTS &&
        input.mode == Transformation::Input::Mode::kFinal) {
      LOG(INFO) << "Not actually deleting region.";
      return output;
    }

    input.buffer->DeleteRange(range);
    output.modified_buffer = true;

    output.MergeFrom(NewSetPositionTransformation(range.begin)->Apply(input));

    InsertOptions insert_options;
    insert_options.buffer_to_insert = std::move(delete_buffer);
    insert_options.final_position = options_.modifiers.direction == FORWARDS
                                        ? InsertOptions::FinalPosition::kStart
                                        : InsertOptions::FinalPosition::kEnd;
    output.undo_stack->PushFront(NewInsertBufferTransformation(insert_options));
    output.undo_stack->PushFront(NewSetPositionTransformation(range.begin));

    if (input.mode != Transformation::Input::Mode::kPreview) return output;
    LOG(INFO) << "Inserting preview at: " << range.begin;
    insert_options.modifiers_set = {
        LineModifier::UNDERLINE,
        options_.modifiers.delete_type == Modifiers::PRESERVE_CONTENTS
            ? LineModifier::GREEN
            : LineModifier::RED};
    input.position = range.begin;
    output.MergeFrom(
        NewInsertBufferTransformation(std::move(insert_options))->Apply(input));
    return output;
  }

  unique_ptr<Transformation> Clone() const override {
    return NewDeleteTransformation(options_);
  }

 private:
  const DeleteOptions options_;
};
}  // namespace

std::unique_ptr<Transformation> NewDeleteTransformation(DeleteOptions options) {
  return std::make_unique<DeleteTransformation>(options);
}

void RegisterDeleteTransformation(vm::Environment* environment) {
  auto builder = std::make_unique<ObjectType>(L"DeleteTransformationBuilder");

  environment->Define(
      L"DeleteTransformationBuilder",
      vm::NewCallback(std::function<std::shared_ptr<DeleteOptions>()>(
          []() { return std::make_shared<DeleteOptions>(); })));

  builder->AddField(
      L"set_modifiers",
      NewCallback(
          std::function<std::shared_ptr<DeleteOptions>(
              std::shared_ptr<DeleteOptions>, std::shared_ptr<Modifiers>)>(
              [](std::shared_ptr<DeleteOptions> options,
                 std::shared_ptr<Modifiers> modifiers) {
                CHECK(options != nullptr);
                CHECK(modifiers != nullptr);
                options->modifiers = *modifiers;
                return options;
              })));

  builder->AddField(
      L"set_copy_to_paste_buffer",
      vm::NewCallback(std::function<std::shared_ptr<DeleteOptions>(
                          std::shared_ptr<DeleteOptions>, bool)>(
          [](std::shared_ptr<DeleteOptions> options,
             bool copy_to_paste_buffer) {
            CHECK(options != nullptr);
            options->copy_to_paste_buffer = copy_to_paste_buffer;
            return options;
          })));

  builder->AddField(
      L"set_line_end_behavior",
      vm::NewCallback(std::function<std::shared_ptr<DeleteOptions>(
                          std::shared_ptr<DeleteOptions>, std::wstring)>(
          [](std::shared_ptr<DeleteOptions> options, std::wstring value) {
            CHECK(options != nullptr);
            if (value == L"stop") {
              options->line_end_behavior =
                  DeleteOptions::LineEndBehavior::kStop;
            } else if (value == L"delete") {
              options->line_end_behavior =
                  DeleteOptions::LineEndBehavior::kDelete;
            }
            return options;
          })));

  builder->AddField(
      L"build",
      vm::NewCallback(
          std::function<Transformation*(std::shared_ptr<DeleteOptions>)>(
              [](std::shared_ptr<DeleteOptions> options) {
                CHECK(options != nullptr);
                return NewDeleteTransformation(*options).release();
              })));

  environment->DefineType(L"DeleteTransformationBuilder", std::move(builder));
}
}  // namespace editor
}  // namespace afc

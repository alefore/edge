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
#include "src/transformation/type.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/function_call.h"
#include "src/vm_transformation.h"
#include "src/wstring.h"

namespace afc {
namespace vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::transformation::Delete>> {
  static std::shared_ptr<editor::transformation::Delete> get(Value* value) {
    CHECK(value != nullptr);
    CHECK(value->type.type == VMType::OBJECT_TYPE);
    CHECK(value->type.object_type == L"DeleteTransformationBuilder");
    CHECK(value->user_value != nullptr);
    return std::static_pointer_cast<editor::transformation::Delete>(
        value->user_value);
  }
  static Value::Ptr New(std::shared_ptr<editor::transformation::Delete> value) {
    return Value::NewObject(L"DeleteTransformationBuilder",
                            std::shared_ptr<void>(value, value.get()));
  }
  static const VMType vmtype;
};

const VMType
    VMTypeMapper<std::shared_ptr<editor::transformation::Delete>>::vmtype =
        VMType::ObjectType(L"DeleteTransformationBuilder");
}  // namespace vm
namespace editor {
std::ostream& operator<<(std::ostream& os,
                         const transformation::Delete& options) {
  os << "[Delete: modifiers:" << options.modifiers << "]";
  return os;
}

namespace {
// Copy to a new buffer the contents of `range`.
std::shared_ptr<OpenBuffer> GetDeletedTextBuffer(const OpenBuffer& buffer,
                                                 Range range) {
  LOG(INFO) << "Preparing deleted text buffer: " << range;
  auto delete_buffer = OpenBuffer::New(
      {.editor = buffer.editor(), .name = OpenBuffer::kPasteBuffer});
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
      expr.get(), buffer->environment(),
      [work_queue = target_buffer->work_queue()](
          std::function<void()> callback) { work_queue->Schedule(callback); })
      .SetConsumer([expr](std::unique_ptr<Value>) { /* Keep expr alive. */ });
}
}  // namespace
namespace transformation {
futures::Value<Transformation::Result> ApplyBase(const Delete& options,
                                                 Transformation::Input input) {
  CHECK(input.buffer != nullptr);
  input.mode = options.mode.value_or(input.mode);

  auto output = std::make_shared<Transformation::Result>(
      input.buffer->AdjustLineColumn(input.position));
  Range range =
      input.buffer->FindPartialRange(options.modifiers, output->position);
  range.begin = min(range.begin, output->position);
  range.end = max(range.end, output->position);

  CHECK_LE(range.begin, range.end);
  if (range.IsEmpty()) {
    VLOG(5) << "No repetitions.";
    return futures::Past(std::move(*output));
  }

  if (options.modifiers.delete_behavior ==
          Modifiers::DeleteBehavior::kDeleteText &&
      input.mode == Transformation::Input::Mode::kFinal) {
    LOG(INFO) << "Deleting superfluous lines (from " << range << ")";
    for (LineColumn delete_position = range.begin;
         delete_position.line < range.end.line;
         delete_position = LineColumn(delete_position.line.next())) {
      HandleLineDeletion(delete_position, input.buffer);
    }
  }

  output->success = true;
  output->made_progress = true;

  auto delete_buffer = GetDeletedTextBuffer(*input.buffer, range);
  if (options.modifiers.paste_buffer_behavior ==
          Modifiers::PasteBufferBehavior::kDeleteInto &&
      input.mode == Transformation::Input::Mode::kFinal) {
    VLOG(5) << "Preparing delete buffer.";
    output->delete_buffer = delete_buffer;
  }

  if (options.modifiers.delete_behavior ==
          Modifiers::DeleteBehavior::kDoNothing &&
      input.mode == Transformation::Input::Mode::kFinal) {
    LOG(INFO) << "Not actually deleting region.";
    return futures::Past(std::move(*output));
  }

  input.buffer->DeleteRange(range);
  output->modified_buffer = true;

  return futures::Transform(
      transformation::Build(transformation::SetPosition(range.begin))
          ->Apply(input),
      [options, range, output, input,
       delete_buffer](Transformation::Result result) mutable {
        output->MergeFrom(std::move(result));

        transformation::Insert insert_options(std::move(delete_buffer));
        insert_options.final_position =
            options.modifiers.direction == Direction::kForwards
                ? Insert::FinalPosition::kStart
                : Insert::FinalPosition::kEnd;
        output->undo_stack->PushFront(insert_options);
        output->undo_stack->PushFront(transformation::SetPosition(range.begin));

        if (input.mode != Transformation::Input::Mode::kPreview) {
          return futures::Past(std::move(*output));
        }
        LOG(INFO) << "Inserting preview at: " << range.begin;
        insert_options.modifiers_set =
            options.modifiers.delete_behavior ==
                    Modifiers::DeleteBehavior::kDoNothing
                ? LineModifierSet{LineModifier::UNDERLINE, LineModifier::GREEN}
                : options.preview_modifiers;
        input.position = range.begin;
        return futures::Transform(
            transformation::Build(std::move(insert_options))->Apply(input),
            [output](Transformation::Result result) {
              output->MergeFrom(std::move(result));
              return std::move(*output);
            });
      });
}

void RegisterDelete(vm::Environment* environment) {
  auto builder = std::make_unique<ObjectType>(L"DeleteTransformationBuilder");

  environment->Define(
      L"DeleteTransformationBuilder",
      vm::NewCallback(std::function<std::shared_ptr<Delete>()>(
          []() { return std::make_shared<transformation::Delete>(); })));

  builder->AddField(
      L"set_modifiers",
      NewCallback(std::function<std::shared_ptr<Delete>(
                      std::shared_ptr<Delete>, std::shared_ptr<Modifiers>)>(
          [](std::shared_ptr<Delete> options,
             std::shared_ptr<Modifiers> modifiers) {
            CHECK(options != nullptr);
            CHECK(modifiers != nullptr);
            options->modifiers = *modifiers;
            return options;
          })));

  builder->AddField(
      L"set_line_end_behavior",
      vm::NewCallback(std::function<std::shared_ptr<Delete>(
                          std::shared_ptr<Delete>, std::wstring)>(
          [](std::shared_ptr<Delete> options, std::wstring value) {
            CHECK(options != nullptr);
            if (value == L"stop") {
              options->line_end_behavior = Delete::LineEndBehavior::kStop;
            } else if (value == L"delete") {
              options->line_end_behavior = Delete::LineEndBehavior::kDelete;
            }
            return options;
          })));

  builder->AddField(
      L"build",
      vm::NewCallback(std::function<Transformation*(std::shared_ptr<Delete>)>(
          [](std::shared_ptr<Delete> options) {
            CHECK(options != nullptr);
            return transformation::Build(*options).release();
          })));

  environment->DefineType(L"DeleteTransformationBuilder", std::move(builder));
}
}  // namespace transformation
}  // namespace editor
}  // namespace afc

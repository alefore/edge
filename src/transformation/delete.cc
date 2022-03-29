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
      {.editor = buffer.editor(), .name = BufferName::PasteBuffer()});
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
void HandleLineDeletion(LineColumn position, OpenBuffer& buffer) {
  position = buffer.AdjustLineColumn(position);
  CHECK_GE(buffer.contents().size(), position.line.ToDelta());

  LOG(INFO) << "Erasing line " << position.line << " in a buffer with size "
            << buffer.contents().size();

  const auto contents = buffer.LineAt(position.line);
  DVLOG(5) << "Erasing line: " << contents->ToString();
  if (!position.column.IsZero()) return;
  auto target_buffer = buffer.GetBufferFromCurrentLine();
  if (target_buffer.get() != &buffer && target_buffer != nullptr) {
    target_buffer->editor().CloseBuffer(*target_buffer);
  }

  if (contents == nullptr) return;
  auto callback = contents->environment()->Lookup(
      Environment::Namespace(), L"EdgeLineDeleteHandler",
      VMType::Function({VMType::Void()}));
  if (callback == nullptr) return;
  LOG(INFO) << "Running EdgeLineDeleteHandler.";
  std::shared_ptr<Expression> expr =
      vm::NewFunctionCall(vm::NewConstantExpression(std::move(callback)), {});
  // TODO(easy): I think we don't need to keep expr alive?
  Evaluate(
      expr.get(), buffer.environment(),
      [work_queue = target_buffer->work_queue()](
          std::function<void()> callback) { work_queue->Schedule(callback); })
      .SetConsumer([expr](auto) { /* Keep expr alive. */ });
}
}  // namespace
namespace transformation {
futures::Value<transformation::Result> ApplyBase(const Delete& options,
                                                 Input input) {
  input.mode = options.mode.value_or(input.mode);

  auto output = std::make_shared<transformation::Result>(
      input.buffer.AdjustLineColumn(input.position));
  Range range;

  if (options.range.has_value()) {
    range = *options.range;
  } else {
    range = input.buffer.FindPartialRange(options.modifiers, output->position);
    range.begin = min(range.begin, output->position);
    range.end = max(range.end, output->position);
    if (range.IsEmpty()) {
      switch (options.modifiers.direction) {
        case Direction::kForwards:
          range.end = input.buffer.contents().PositionAfter(range.end);
          break;
        case Direction::kBackwards:
          range.begin = input.buffer.contents().PositionBefore(range.begin);
          break;
      }
    }
  }
  range.begin = input.buffer.AdjustLineColumn(range.begin);
  range.end = input.buffer.AdjustLineColumn(range.end);

  CHECK_LE(range.begin, range.end);
  if (range.IsEmpty()) {
    VLOG(5) << "Nothing to delete.";
    return futures::Past(std::move(*output));
  }

  if (options.modifiers.delete_behavior ==
          Modifiers::DeleteBehavior::kDeleteText &&
      input.mode == Input::Mode::kFinal) {
    LOG(INFO) << "Deleting superfluous lines (from " << range << ")";
    for (LineColumn delete_position = range.begin;
         delete_position.line < range.end.line;
         delete_position = LineColumn(delete_position.line.next())) {
      HandleLineDeletion(delete_position, input.buffer);
    }
  }

  output->success = true;
  output->made_progress = true;

  auto delete_buffer = GetDeletedTextBuffer(input.buffer, range);
  if (options.modifiers.paste_buffer_behavior ==
          Modifiers::PasteBufferBehavior::kDeleteInto &&
      input.mode == Input::Mode::kFinal && input.delete_buffer != nullptr) {
    VLOG(5) << "Preparing delete buffer.";
    output->added_to_paste_buffer = true;
    input.delete_buffer->ApplyToCursors(transformation::Insert{
        .contents_to_insert = delete_buffer->contents().copy()});
  }

  if (options.modifiers.delete_behavior ==
          Modifiers::DeleteBehavior::kDoNothing &&
      input.mode == Input::Mode::kFinal) {
    LOG(INFO) << "Not actually deleting region.";
    output->position = range.end;
    return futures::Past(std::move(*output));
  }

  input.buffer.DeleteRange(range);
  output->modified_buffer = true;

  return Apply(transformation::SetPosition(range.begin), input)
      .Transform([options, range, output, input,
                  delete_buffer](transformation::Result result) mutable {
        output->MergeFrom(std::move(result));

        transformation::Insert insert_options{
            .contents_to_insert = delete_buffer->contents().copy(),
            .final_position =
                options.modifiers.direction == Direction::kForwards
                    ? Insert::FinalPosition::kEnd
                    : Insert::FinalPosition::kStart};
        output->undo_stack->PushFront(insert_options);
        output->undo_stack->PushFront(transformation::SetPosition(range.begin));

        if (input.mode != Input::Mode::kPreview) {
          return futures::Past(std::move(*output));
        }
        LOG(INFO) << "Inserting preview at: " << range.begin;
        insert_options.modifiers_set =
            options.modifiers.delete_behavior ==
                    Modifiers::DeleteBehavior::kDoNothing
                ? LineModifierSet{LineModifier::UNDERLINE, LineModifier::GREEN}
                : options.preview_modifiers;
        input.position = range.begin;
        return Apply(std::move(insert_options), input)
            .Transform([output](transformation::Result result) {
              output->MergeFrom(std::move(result));
              return std::move(*output);
            });
      });
}

std::wstring ToStringBase(const Delete&) { return L"Delete(...);"; }

Delete OptimizeBase(Delete transformation) { return transformation; }

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

  builder->AddField(L"build",
                    vm::NewCallback([](std::shared_ptr<Delete> options) {
                      CHECK(options != nullptr);
                      return std::make_unique<Variant>(*options).release();
                    }));

  environment->DefineType(L"DeleteTransformationBuilder", std::move(builder));
}
}  // namespace transformation
}  // namespace editor
}  // namespace afc

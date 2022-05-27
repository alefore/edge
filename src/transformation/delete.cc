#include "src/transformation/delete.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/language/safe_types.h"
#include "src/language/value_or_error.h"
#include "src/language/wstring.h"
#include "src/lazy_string_append.h"
#include "src/line_column_vm.h"
#include "src/line_prompt_mode.h"
#include "src/modifiers.h"
#include "src/transformation.h"
#include "src/transformation/insert.h"
#include "src/transformation/move.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/transformation/vm.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/function_call.h"

namespace afc {
using language::EmptyValue;
using language::NonNull;
using language::VisitPointer;

namespace gc = language::gc;
namespace vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::transformation::Delete>> {
  static std::shared_ptr<editor::transformation::Delete> get(Value& value) {
    return std::static_pointer_cast<editor::transformation::Delete>(
        value.get_user_value(vmtype));
  }
  static gc::Root<Value> New(
      language::gc::Pool& pool,
      std::shared_ptr<editor::transformation::Delete> value) {
    // TODO(2022-05-27, easy): Receive `value` as NonNull.
    return Value::NewObject(
        pool, vmtype.object_type,
        NonNull<std::shared_ptr<editor::transformation::Delete>>::Unsafe(
            value));
  }
  static const VMType vmtype;
};

const VMType
    VMTypeMapper<std::shared_ptr<editor::transformation::Delete>>::vmtype =
        VMType::ObjectType(
            VMTypeObjectTypeName(L"DeleteTransformationBuilder"));
}  // namespace vm
namespace editor {
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::VisitPointer;
std::ostream& operator<<(std::ostream& os,
                         const transformation::Delete& options) {
  os << "[Delete: modifiers:" << options.modifiers << "]";
  return os;
}

namespace {
using language::NonNull;

// Copy to a new buffer the contents of `range`.
gc::Root<OpenBuffer> GetDeletedTextBuffer(const OpenBuffer& buffer,
                                          Range range) {
  LOG(INFO) << "Preparing deleted text buffer: " << range;
  gc::Root<OpenBuffer> delete_buffer = OpenBuffer::New(
      {.editor = buffer.editor(), .name = BufferName::PasteBuffer()});
  for (LineNumber i = range.begin.line; i <= range.end.line; ++i) {
    Line::Options line_options = buffer.contents().at(i)->CopyOptions();
    if (i == range.end.line) {
      line_options.DeleteSuffix(range.end.column);
    }
    if (i == range.begin.line) {
      line_options.DeleteCharacters(ColumnNumber(0),
                                    range.begin.column.ToDelta());
      delete_buffer.ptr()->AppendToLastLine(Line(std::move(line_options)));
    } else {
      delete_buffer.ptr()->AppendRawLine(
          MakeNonNullShared<Line>(std::move(line_options)));
    }
  }

  return delete_buffer;
}

void HandleLineDeletion(Range range, OpenBuffer& buffer) {
  std::vector<std::function<void()>> observers;
  std::shared_ptr<const Line> first_line_contents;
  for (LineColumn delete_position = range.begin;
       delete_position.line < range.end.line;
       delete_position = LineColumn(delete_position.line.next())) {
    LineColumn position = buffer.AdjustLineColumn(delete_position);
    if (position.line != delete_position.line || !position.column.IsZero())
      continue;

    CHECK_GE(buffer.contents().size(), position.line.ToDelta());

    LOG(INFO) << "Erasing line " << position.line << " in a buffer with size "
              << buffer.contents().size();

    NonNull<std::shared_ptr<const Line>> line_contents =
        buffer.contents().at(position.line);
    DVLOG(5) << "Erasing line: " << line_contents->ToString();
    auto line_buffer = buffer.current_line()->buffer_line_column();
    if (line_buffer.has_value()) {
      VisitPointer(
          line_buffer->buffer.Lock(),
          [&buffer](gc::Root<OpenBuffer> target_buffer) {
            if (&target_buffer.ptr().value() != &buffer) {
              target_buffer.ptr()->editor().CloseBuffer(
                  target_buffer.ptr().value());
            }
          },
          [] {});
    }
    std::function<void()> f = line_contents->explicit_delete_observer();
    if (f == nullptr) continue;
    observers.push_back(f);
    if (first_line_contents == nullptr)
      first_line_contents = line_contents.get_shared();
  }
  if (observers.empty()) return;
  std::wstring details = observers.size() == 1
                             ? first_line_contents->ToString()
                             : L" files: " + std::to_wstring(observers.size());
  Prompt(PromptOptions{
      .editor_state = buffer.editor(),
      .prompt = L"unlink " + details + L"? [yes/no] ",
      .history_file = HistoryFile(L"confirmation"),
      .handler =
          [buffer = buffer.NewRoot(), observers](const std::wstring& input) {
            if (input == L"yes") {
              for (auto& o : observers) o();
            } else {
              // TODO: insert it again?  Actually, only let it
              // be erased in the other case?
              buffer.ptr()->status().SetInformationText(L"Ignored.");
            }
            return futures::Past(EmptyValue());
          },
      .predictor = PrecomputedPredictor({L"no", L"yes"}, '/')});
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
    range.begin = std::min(range.begin, output->position);
    range.end = std::max(range.end, output->position);
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

  if (options.modifiers.text_delete_behavior ==
          Modifiers::TextDeleteBehavior::kDelete &&
      input.mode == Input::Mode::kFinal &&
      options.initiator == Delete::Initiator::kUser) {
    LOG(INFO) << "Deleting superfluous lines (from " << range << ")";
    HandleLineDeletion(range, input.buffer);
  }

  output->success = true;
  output->made_progress = true;

  gc::Root<OpenBuffer> delete_buffer =
      GetDeletedTextBuffer(input.buffer, range);
  if (options.modifiers.paste_buffer_behavior ==
          Modifiers::PasteBufferBehavior::kDeleteInto &&
      input.mode == Input::Mode::kFinal && input.delete_buffer != nullptr) {
    VLOG(5) << "Preparing delete buffer.";
    output->added_to_paste_buffer = true;
    input.delete_buffer->ApplyToCursors(transformation::Insert{
        .contents_to_insert = delete_buffer.ptr()->contents().copy()});
  }

  if (options.modifiers.text_delete_behavior ==
          Modifiers::TextDeleteBehavior::kKeep &&
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
            .contents_to_insert = delete_buffer.ptr()->contents().copy(),
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
            options.modifiers.text_delete_behavior ==
                    Modifiers::TextDeleteBehavior::kKeep
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

void RegisterDelete(language::gc::Pool& pool, vm::Environment& environment) {
  using vm::ObjectType;
  using vm::PurityType;
  using vm::VMTypeMapper;

  auto builder = MakeNonNullUnique<ObjectType>(
      VMTypeMapper<std::shared_ptr<Delete>>::vmtype);

  environment.Define(
      builder->type().object_type.read(),
      vm::NewCallback(pool, PurityType::kPure,
                      std::function<std::shared_ptr<Delete>()>([]() {
                        return std::make_shared<transformation::Delete>();
                      })));

  builder->AddField(
      L"set_modifiers",
      vm::NewCallback(pool, vm::PurityTypeWriter,
                      std::function<std::shared_ptr<Delete>(
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
      vm::NewCallback(
          pool, vm::PurityTypeWriter,
          std::function<std::shared_ptr<Delete>(std::shared_ptr<Delete>,
                                                std::wstring)>(
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
      L"set_range",
      vm::NewCallback(pool, vm::PurityTypeWriter,
                      std::function<std::shared_ptr<Delete>(
                          std::shared_ptr<Delete>, Range)>(
                          [](std::shared_ptr<Delete> options, Range range) {
                            options->range = range;
                            return options;
                          })));

  builder->AddField(
      L"build", vm::NewCallback(pool, PurityType::kPure,
                                [](std::shared_ptr<Delete> options) {
                                  CHECK(options != nullptr);
                                  return MakeNonNullUnique<Variant>(*options);
                                }));

  environment.DefineType(std::move(builder));
}
}  // namespace transformation
}  // namespace editor
}  // namespace afc

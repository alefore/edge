#include "src/transformation/delete.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column_vm.h"
#include "src/language/wstring.h"
#include "src/line_prompt_mode.h"
#include "src/modifiers.h"
#include "src/transformation.h"
#include "src/transformation/insert.h"
#include "src/transformation/move.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/transformation/vm.h"
#include "src/vm/constant_expression.h"
#include "src/vm/environment.h"
#include "src/vm/function_call.h"

namespace afc {
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::EmptyValue;
using language::NonNull;
using language::VisitPointer;
using language::lazy_string::ColumnNumber;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::OutgoingLink;
using language::text::Range;

namespace gc = language::gc;
namespace vm {
template <>
const types::ObjectName VMTypeMapper<NonNull<
    std::shared_ptr<editor::transformation::Delete>>>::object_type_name =
    types::ObjectName(L"DeleteTransformationBuilder");

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
  for (LineNumber i = range.begin().line; i <= range.end().line; ++i) {
    LineBuilder line_options(buffer.contents().at(i).value());
    if (i == range.end().line) {
      line_options.DeleteSuffix(range.end().column);
    }
    if (i == range.begin().line) {
      line_options.DeleteCharacters(ColumnNumber(0),
                                    range.begin().column.ToDelta());
      delete_buffer.ptr()->AppendToLastLine(std::move(line_options).Build());
    } else {
      delete_buffer.ptr()->AppendRawLine(
          MakeNonNullShared<Line>(std::move(line_options).Build()));
    }
  }

  return delete_buffer;
}

void HandleLineDeletion(Range range, transformation::Input::Adapter& adapter,
                        OpenBuffer& buffer) {
  std::vector<std::function<void()>> observers;
  std::shared_ptr<const Line> first_line_contents;
  for (LineColumn delete_position = range.begin();
       delete_position.line < range.end().line;
       delete_position = LineColumn(delete_position.line.next())) {
    LineColumn position = adapter.contents().AdjustLineColumn(delete_position);
    if (position.line != delete_position.line || !position.column.IsZero())
      continue;

    CHECK_GE(adapter.contents().size(), position.line.ToDelta());

    LOG(INFO) << "Erasing line " << position.line << " in a buffer with size "
              << adapter.contents().size();

    NonNull<std::shared_ptr<const Line>> line_contents =
        adapter.contents().at(position.line);
    DVLOG(5) << "Erasing line: " << line_contents->ToString();
    VisitPointer(
        buffer.CurrentLine()->outgoing_link(),
        [&](const OutgoingLink& outgoing_link) {
          if (auto it = buffer.editor().buffers()->find(
                  BufferName(outgoing_link.path));
              it != buffer.editor().buffers()->end()) {
            gc::Root<OpenBuffer> target_buffer = it->second;
            if (&target_buffer.ptr().value() != &buffer) {
              target_buffer.ptr()->editor().CloseBuffer(
                  target_buffer.ptr().value());
            }
          }
        },
        [] {});
    std::function<void()> f = line_contents->explicit_delete_observer();
    if (f == nullptr) continue;
    observers.push_back(f);
    if (first_line_contents == nullptr)
      first_line_contents = line_contents.get_shared();
  }
  if (observers.empty()) return;
  // TODO(easy, 2022-06-05): Get rid of ToString.
  std::wstring details = observers.size() == 1
                             ? first_line_contents->ToString()
                             : L" files: " + std::to_wstring(observers.size());
  Prompt(PromptOptions{
      .editor_state = buffer.editor(),
      .prompt = NewLazyString(L"unlink " + details + L"? [yes/no] "),
      .history_file = HistoryFile(L"confirmation"),
      .handler =
          [buffer = buffer.NewRoot(), observers](LazyString input) {
            if (input == NewLazyString(L"yes")) {
              for (auto& o : observers) o();
            } else {
              // TODO: insert it again?  Actually, only let it
              // be erased in the other case?
              buffer.ptr()->status().SetInformationText(
                  MakeNonNullShared<Line>(L"Ignored."));
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
      input.adapter.contents().AdjustLineColumn(input.position));
  Range range;

  if (options.range.has_value()) {
    range = *options.range;
  } else {
    range = input.buffer.FindPartialRange(options.modifiers, output->position);
    range.set_begin(std::min(range.begin(), output->position));
    range.set_end(std::max(range.end(), output->position));
    if (range.IsEmpty()) {
      switch (options.modifiers.direction) {
        case Direction::kForwards:
          range.set_end(input.adapter.contents().PositionAfter(range.end()));
          break;
        case Direction::kBackwards:
          range.set_begin(
              input.adapter.contents().PositionBefore(range.begin()));
          break;
      }
    }
  }
  range = Range(input.adapter.contents().AdjustLineColumn(range.begin()),
                input.adapter.contents().AdjustLineColumn(range.end()));
  if (range.IsEmpty()) {
    VLOG(5) << "Nothing to delete.";
    return futures::Past(std::move(*output));
  }

  if (options.modifiers.text_delete_behavior ==
          Modifiers::TextDeleteBehavior::kDelete &&
      input.mode == Input::Mode::kFinal &&
      options.initiator == Delete::Initiator::kUser) {
    LOG(INFO) << "Deleting superfluous lines (from " << range << ")";
    HandleLineDeletion(range, input.adapter, input.buffer);
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
        .contents_to_insert = delete_buffer.ptr()->contents().snapshot()});
  }

  if (options.modifiers.text_delete_behavior ==
          Modifiers::TextDeleteBehavior::kKeep &&
      input.mode == Input::Mode::kFinal) {
    LOG(INFO) << "Not actually deleting region.";
    output->position = range.end();
    return futures::Past(std::move(*output));
  }

  input.buffer.DeleteRange(range);

  output->modified_buffer = true;

  return Apply(transformation::SetPosition(range.begin()), input)
      .Transform([options, range, output, input,
                  delete_buffer](transformation::Result result) mutable {
        output->MergeFrom(std::move(result));
        transformation::Insert insert_options{
            .contents_to_insert = delete_buffer.ptr()->contents().snapshot(),
            .final_position =
                options.modifiers.direction == Direction::kForwards
                    ? Insert::FinalPosition::kEnd
                    : Insert::FinalPosition::kStart};
        output->undo_stack->PushFront(insert_options);
        output->undo_stack->PushFront(
            transformation::SetPosition(range.begin()));
        if (input.mode != Input::Mode::kPreview) {
          return futures::Past(std::move(*output));
        }
        LOG(INFO) << "Inserting preview at: " << range.begin();
        insert_options.modifiers_set =
            options.modifiers.text_delete_behavior ==
                    Modifiers::TextDeleteBehavior::kKeep
                ? LineModifierSet{LineModifier::kUnderline,
                                  LineModifier::kYellow}
                : options.preview_modifiers;
        input.position = range.begin();
        return Apply(std::move(insert_options), input)
            .Transform([output](transformation::Result input_result) {
              output->MergeFrom(std::move(input_result));
              return std::move(*output);
            });
      });
}

std::wstring ToStringBase(const Delete& options) {
  std::wstring output = L"DeleteTransformationBuilder()";
  output += L".set_modifiers(" + options.modifiers.Serialize() + L")";
  if (options.range.has_value()) output += L".set_range(...)";
  output += L".build()";
  return output;
}

Delete OptimizeBase(Delete transformation) { return transformation; }

void RegisterDelete(language::gc::Pool& pool, vm::Environment& environment) {
  using vm::ObjectType;
  using vm::PurityType;
  using vm::VMTypeMapper;

  gc::Root<ObjectType> builder = ObjectType::New(
      pool, VMTypeMapper<NonNull<std::shared_ptr<Delete>>>::object_type_name);

  environment.Define(
      VMTypeMapper<NonNull<std::shared_ptr<Delete>>>::object_type_name.read(),
      vm::NewCallback(pool, PurityType::kPure,
                      MakeNonNullShared<transformation::Delete>));

  builder.ptr()->AddField(
      L"set_modifiers",
      vm::NewCallback(pool, vm::PurityTypeWriter,
                      [](NonNull<std::shared_ptr<Delete>> options,
                         NonNull<std::shared_ptr<Modifiers>> modifiers) {
                        options->modifiers = modifiers.value();
                        return options;
                      })
          .ptr());

  builder.ptr()->AddField(
      L"set_line_end_behavior",
      vm::NewCallback(
          pool, vm::PurityTypeWriter,
          [](NonNull<std::shared_ptr<Delete>> options, std::wstring value) {
            if (value == L"stop") {
              options->line_end_behavior = Delete::LineEndBehavior::kStop;
            } else if (value == L"delete") {
              options->line_end_behavior = Delete::LineEndBehavior::kDelete;
            }
            return options;
          })
          .ptr());
  builder.ptr()->AddField(
      L"set_range", vm::NewCallback(pool, vm::PurityTypeWriter,
                                    [](NonNull<std::shared_ptr<Delete>> options,
                                       Range range) {
                                      options->range = range;
                                      return options;
                                    })
                        .ptr());

  builder.ptr()->AddField(
      L"build",
      vm::NewCallback(pool, PurityType::kPure,
                      [](NonNull<std::shared_ptr<Delete>> options) {
                        return MakeNonNullShared<Variant>(options.value());
                      })
          .ptr());

  environment.DefineType(builder.ptr());
}
}  // namespace transformation
}  // namespace editor
}  // namespace afc

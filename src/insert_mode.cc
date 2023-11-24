#include "src/insert_mode.h"

#include <algorithm>
#include <memory>
#include <vector>

extern "C" {
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/buffer_variables.h"
#include "src/buffer_vm.h"
#include "src/command.h"
#include "src/command_mode.h"
#include "src/completion_model.h"
#include "src/concurrent/work_queue.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/futures/delete_notification.h"
#include "src/futures/futures.h"
#include "src/futures/listenable_value.h"
#include "src/infrastructure/time.h"
#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/parse_tree.h"
#include "src/terminal.h"
#include "src/tests/tests.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/expand.h"
#include "src/transformation/insert.h"
#include "src/transformation/move.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm/constant_expression.h"
#include "src/vm/function_call.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace audio = afc::infrastructure::audio;
namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::concurrent::WorkQueue;
using afc::futures::DeleteNotification;
using afc::infrastructure::AddSeconds;
using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::Now;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ToByteString;
using afc::language::VisitOptionalCallback;
using afc::language::VisitPointer;
using afc::language::lazy_string::Append;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::language::text::OutgoingLink;
using afc::language::text::Range;
using afc::vm::Type;
using afc::vm::VMTypeMapper;

namespace afc::editor {
using ::operator<<;

namespace {
class NewLineTransformation : public CompositeTransformation {
  std::wstring Serialize() const override { return L"NewLineTransformation()"; }
  futures::Value<Output> Apply(Input input) const override {
    const ColumnNumber column = input.position.column;
    auto line = input.buffer.LineAt(input.position.line);
    if (line == nullptr) return futures::Past(Output());
    if (input.buffer.Read(buffer_variables::atomic_lines) &&
        column != ColumnNumber(0) && column != line->EndColumn())
      return futures::Past(Output());
    const std::wstring& line_prefix_characters(
        input.buffer.Read(buffer_variables::line_prefix_characters));
    ColumnNumber prefix_end;
    if (!input.buffer.Read(buffer_variables::paste_mode)) {
      while (prefix_end < column &&
             (line_prefix_characters.find(line->get(prefix_end)) !=
              line_prefix_characters.npos)) {
        ++prefix_end;
      }
    }

    Output output;
    {
      LineBuilder line_without_suffix(*line);
      line_without_suffix.DeleteSuffix(prefix_end);
      MutableLineSequence contents_to_insert;
      contents_to_insert.push_back(
          MakeNonNullShared<Line>(std::move(line_without_suffix).Build()));
      output.Push(transformation::Insert{.contents_to_insert =
                                             contents_to_insert.snapshot()});
    }

    output.Push(transformation::SetPosition(input.position));
    output.Push(NewDeleteSuffixSuperfluousCharacters());
    output.Push(transformation::SetPosition(
        LineColumn(input.position.line + LineNumberDelta(1), prefix_end)));
    return futures::Past(std::move(output));
  }
};

class TestsHelper {
 public:
  void split(OpenBuffer& buffer, LineColumn position) {
    buffer.set_position(position);
    buffer.ApplyToCursors(MakeNonNullShared<NewLineTransformation>());
    LOG(INFO) << "Contents: " << buffer.contents().snapshot().ToString();
  };

  std::wstring StringAfterSplit(LineColumn position) {
    split(buffer(), position);
    return buffer().contents().snapshot().ToString();
  };

  void AddCursor(LineColumn position, LineColumn expectation) {
    OpenBuffer& buffer = buffer_.ptr().value();
    buffer.set_position(position);
    buffer.CreateCursor();
    expectations_.insert(expectation);
  };

  EditorState& editor() const { return editor_.value(); }

  OpenBuffer& buffer() const { return buffer_.ptr().value(); }

  infrastructure::screen::CursorsSet& expectations() { return expectations_; }
  void ValidateCursorExpectations() const {
    for (auto& cursor : buffer().active_cursors())
      LOG(INFO) << "Cursor: " << cursor;
    CHECK(buffer().active_cursors() == expectations_);
  }

 private:
  NonNull<std::shared_ptr<EditorState>> editor_ = EditorForTests();

  gc::Root<OpenBuffer> buffer_ = [this] {
    gc::Root<OpenBuffer> buffer_root = NewBufferForTests(editor_.value());
    OpenBuffer& buffer = buffer_root.ptr().value();
    buffer.AppendToLastLine(NewLazyString(L"foobarhey"));
    buffer.AppendRawLine(MakeNonNullShared<Line>(Line(L"  foxbarnowl")));
    buffer.AppendRawLine(MakeNonNullShared<Line>(Line(L"  aaaaa ")));
    buffer.AppendRawLine(MakeNonNullShared<Line>(Line(L"  alejo forero ")));
    return buffer_root;
  }();

  infrastructure::screen::CursorsSet expectations_;
};

const bool new_line_transformation_tests_registration =
    tests::Register(L"NewLineTransformation", [] {
      using infrastructure::screen::CursorsSet;
      return std::vector<tests::Test>(
          {{.name = L"Empty",
            .callback =
                [] {
                  TestsHelper helper;
                  gc::Root<OpenBuffer> buffer =
                      NewBufferForTests(helper.editor());
                  helper.split(buffer.ptr().value(), LineColumn());
                  CHECK(buffer.ptr()->contents().snapshot().ToString() ==
                        L"\n");
                }},
           {.name = L"AtBeginning",
            .callback =
                [] {
                  CHECK(
                      TestsHelper().StringAfterSplit(LineColumn()) ==
                      L"\nfoobarhey\n  foxbarnowl\n  aaaaa \n  alejo forero ");
                }},
           {.name = L"MiddleFirstLine",
            .callback =
                [] {
                  CHECK(
                      TestsHelper().StringAfterSplit(
                          LineColumn(LineNumber(), ColumnNumber(3))) ==
                      L"foo\nbarhey\n  foxbarnowl\n  aaaaa \n  alejo forero ");
                }},
           {.name = L"EndFirstLine",
            .callback =
                [] {
                  CHECK(
                      TestsHelper().StringAfterSplit(
                          LineColumn(LineNumber(),
                                     ColumnNumber(sizeof("foobarhey") - 1))) ==
                      L"foobarhey\n\n  foxbarnowl\n  aaaaa \n  alejo forero ");
                }},
           {.name = L"WithIndent",
            .callback =
                [] {
                  CHECK(
                      TestsHelper().StringAfterSplit(LineColumn(
                          LineNumber(1), ColumnNumber(sizeof("  fox") - 1))) ==
                      L"foobarhey\n  fox\n  barnowl\n  aaaaa \n  alejo "
                      L"forero ");
                }},
           {.name = L"WithIndentAtEnd",
            .callback =
                [] {
                  CHECK(TestsHelper().StringAfterSplit(LineColumn(
                            LineNumber(1),
                            ColumnNumber(sizeof("  foxbarnowl") - 1))) ==
                        L"foobarhey\n  foxbarnowl\n  \n  aaaaa \n  alejo "
                        L"forero ");
                }},
           {.name = L"CursorsBefore",
            .callback =
                [] {
                  TestsHelper helper;
                  helper.AddCursor(LineColumn(LineNumber(1), ColumnNumber(2)),
                                   LineColumn(LineNumber(1), ColumnNumber(2)));
                  helper.AddCursor(LineColumn(LineNumber(3), ColumnNumber(2)),
                                   LineColumn(LineNumber(3), ColumnNumber(2)));
                  helper.split(helper.buffer(),
                               LineColumn(LineNumber(3), ColumnNumber(4)));
                  helper.expectations().set_active(helper.expectations().insert(
                      LineColumn(LineNumber(4), ColumnNumber(2))));
                  helper.ValidateCursorExpectations();
                }},
           {.name = L"CursorsAfter", .callback = [] {
              TestsHelper helper;
              helper.AddCursor(
                  LineColumn(LineNumber(1), ColumnNumber(6)),
                  LineColumn(LineNumber(2), ColumnNumber(2 + 6 - 3)));
              helper.AddCursor(
                  LineColumn(LineNumber(1), ColumnNumber(13)),
                  LineColumn(LineNumber(2), ColumnNumber(2 + 13 - 3)));
              helper.AddCursor(LineColumn(LineNumber(2), ColumnNumber(0)),
                               LineColumn(LineNumber(3), ColumnNumber(0)));
              helper.AddCursor(LineColumn(LineNumber(2), ColumnNumber(2)),
                               LineColumn(LineNumber(3), ColumnNumber(2)));
              helper.split(helper.buffer(),
                           LineColumn(LineNumber(1), ColumnNumber(3)));
              helper.expectations().set_active(helper.expectations().insert(
                  LineColumn(LineNumber(2), ColumnNumber(2))));
              helper.ValidateCursorExpectations();
            }}});
    }());

class InsertEmptyLineTransformation : public CompositeTransformation {
 public:
  InsertEmptyLineTransformation(Direction direction) : direction_(direction) {}
  std::wstring Serialize() const override { return L""; }
  futures::Value<Output> Apply(Input input) const override {
    if (direction_ == Direction::kBackwards) {
      ++input.position.line;
    }
    Output output = Output::SetPosition(LineColumn(input.position.line));
    output.Push(transformation::ModifiersAndComposite{
        Modifiers(), MakeNonNullUnique<NewLineTransformation>()});
    output.Push(transformation::SetPosition(input.position));
    return futures::Past(std::move(output));
  }

 private:
  Direction direction_;
};

class FindCompletionCommand : public Command {
 public:
  FindCompletionCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}
  std::wstring Description() const override {
    return L"Autocompletes the current word.";
  }
  std::wstring Category() const override { return L"Edit"; }

  void ProcessInput(ExtendedChar) override {
    // TODO(multiple_buffers): Honor.
    VisitPointer(
        editor_state_.current_buffer(),
        [](gc::Root<OpenBuffer> buffer) {
          buffer.ptr()->ApplyToCursors(NewExpandTransformation());
        },
        [] {});
  }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};

std::unique_ptr<MutableLineSequence, std::function<void(MutableLineSequence*)>>
NewInsertion(EditorState& editor) {
  return std::unique_ptr<MutableLineSequence,
                         std::function<void(MutableLineSequence*)>>(
      new MutableLineSequence(), [&editor](MutableLineSequence* value) {
        CHECK(value != nullptr);
        editor.insert_history().Append(value->snapshot());
        delete value;
      });
}

class InsertMode : public EditorMode {
 public:
  InsertMode(InsertModeOptions options)
      : options_(std::move(options)),
        buffers_(MakeNonNullShared<std::vector<gc::Root<OpenBuffer>>>(
            options_.buffers->begin(), options_.buffers->end())),
        current_insertion_(NewInsertion(options_.editor_state)),
        completion_model_supplier_(MakeNonNullShared<CompletionModelManager>(
            [&editor = options_.editor_state](Path path) {
              return OpenOrCreateFile(
                         OpenFileOptions{
                             .editor_state = editor,
                             .path =
                                 Path::Join(editor.edge_path().front(), path),
                             .insertion_type =
                                 BuffersList::AddBufferType::kIgnore,
                             .use_search_paths = false})
                  .Transform([](gc::Root<OpenBuffer> buffer) {
                    buffer.ptr()->Set(buffer_variables::allow_dirty_delete,
                                      true);
                    return buffer.ptr()->WaitForEndOfFile().Transform(
                        [buffer](EmptyValue) {
                          return buffer.ptr()->contents().snapshot();
                        });
                  });
            })) {
    CHECK(options_.escape_handler);
    CHECK(options_.buffers.has_value());
    CHECK(!options_.buffers.value().empty());
  }

  void ProcessInput(ExtendedChar c) override {
    bool old_literal = status_expiration_for_literal_ != nullptr;
    status_expiration_for_literal_ = nullptr;

    CHECK(options_.buffers.has_value());
    auto future = futures::Past(futures::IterationControlCommand::kContinue);
    std::visit(
        overload{
            [&](wchar_t regular_c) { ProcessRegular(regular_c, old_literal); },
            [&](ControlChar control_c) {
              TRACK_OPERATION(InsertMode_ProcessInput_ControlChar);
              switch (control_c) {
                case ControlChar::kEscape:
                  ResetScrollBehavior();
                  StartNewInsertion();

                  ForEachActiveBuffer(
                      buffers_, old_literal ? std::wstring{27} : L"",
                      [options = options_, old_literal](OpenBuffer& buffer) {
                        if (buffer.fd() != nullptr) {
                          if (old_literal) {
                            buffer.status().SetInformationText(
                                MakeNonNullShared<Line>(L"ESC"));
                          } else {
                            buffer.status().Reset();
                          }
                          return futures::Past(EmptyValue());
                        }
                        buffer.MaybeAdjustPositionCol();
                        gc::Root<OpenBuffer> buffer_root = buffer.NewRoot();
                        // TODO(easy): Honor `old_literal`.
                        return buffer
                            .ApplyToCursors(
                                NewDeleteSuffixSuperfluousCharacters())
                            .Transform([options, buffer_root](EmptyValue) {
                              buffer_root.ptr()->PopTransformationStack();
                              auto repetitions =
                                  options.editor_state.repetitions().value_or(
                                      1);
                              if (repetitions > 0) {
                                options.editor_state.set_repetitions(
                                    repetitions - 1);
                              }
                              return buffer_root.ptr()
                                  ->RepeatLastTransformation();
                            })
                            .Transform([options, buffer_root](EmptyValue) {
                              buffer_root.ptr()->PopTransformationStack();
                              options.editor_state.PushCurrentPosition();
                              buffer_root.ptr()->status().Reset();
                              return EmptyValue();
                            });
                      })
                      .Transform([options = options_, old_literal](EmptyValue) {
                        if (old_literal) return EmptyValue();
                        options.editor_state.status().Reset();
                        CHECK(options.escape_handler != nullptr);
                        options.escape_handler();  // Probably deletes us.
                        options.editor_state.ResetRepetitions();
                        options.editor_state.ResetInsertionModifier();
                        options.editor_state.set_keyboard_redirect(nullptr);
                        return EmptyValue();
                      });
                  return;

                case ControlChar::kPageUp:
                  ApplyScrollBehavior({27, '[', '5', '~'},
                                      &ScrollBehavior::PageUp);
                  return;

                case ControlChar::kPageDown:
                  ApplyScrollBehavior({27, '[', '6', '~'},
                                      &ScrollBehavior::PageDown);
                  return;

                case ControlChar::kUpArrow:
                  ApplyScrollBehavior({27, '[', 'A'}, &ScrollBehavior::Up);
                  return;

                case ControlChar::kDownArrow:
                  ApplyScrollBehavior({27, '[', 'B'}, &ScrollBehavior::Down);
                  return;

                case ControlChar::kLeftArrow:
                  ApplyScrollBehavior({27, '[', 'D'}, &ScrollBehavior::Left);
                  return;

                case ControlChar::kRightArrow:
                  ApplyScrollBehavior({27, '[', 'C'}, &ScrollBehavior::Right);
                  return;

                case ControlChar::kCtrlA:
                  ApplyScrollBehavior({1}, &ScrollBehavior::Begin);
                  return;

                case ControlChar::kCtrlE:
                  ApplyScrollBehavior({5}, &ScrollBehavior::End);
                  return;

                case ControlChar::kCtrlL:
                  WriteLineBuffer(buffers_, {0x0c});
                  return;

                case ControlChar::kCtrlD:
                  HandleDelete({4}, Direction::kForwards);
                  return;

                case ControlChar::kDelete:
                  HandleDelete({27, '[', 51, 126}, Direction::kForwards);
                  return;

                case ControlChar::kBackspace:
                  HandleDelete({127}, Direction::kBackwards);
                  return;

                case ControlChar::kCtrlU: {
                  ResetScrollBehavior();
                  StartNewInsertion();
                  // TODO: Find a way to set `copy_to_paste_buffer` in the
                  // transformation.
                  std::optional<gc::Root<vm::Value>> callback =
                      options_.editor_state.environment().ptr()->Lookup(
                          options_.editor_state.gc_pool(), vm::Namespace(),
                          L"HandleKeyboardControlU",
                          vm::types::Function{
                              .output = vm::Type{vm::types::Void{}},
                              .inputs = {vm::GetVMType<
                                  gc::Root<OpenBuffer>>::vmtype()}});
                  if (!callback.has_value()) {
                    LOG(WARNING)
                        << "Didn't find HandleKeyboardControlU function.";
                    return;
                  }
                  ForEachActiveBuffer(
                      buffers_, {21},
                      [options = options_, callback](OpenBuffer& buffer) {
                        NonNull<std::unique_ptr<vm::Expression>> expression =
                            vm::NewFunctionCall(
                                vm::NewConstantExpression(callback.value()),
                                {vm::NewConstantExpression(
                                    {VMTypeMapper<gc::Root<OpenBuffer>>::New(
                                        buffer.editor().gc_pool(),
                                        buffer.NewRoot())})});
                        if (expression->Types().empty()) {
                          buffer.status().InsertError(
                              Error(L"Unable to compile (type mismatch)."));
                          return futures::Past(EmptyValue());
                        }
                        return buffer
                            .EvaluateExpression(std::move(expression),
                                                buffer.environment().ToRoot())
                            .ConsumeErrors([&pool = buffer.editor().gc_pool()](
                                               Error) {
                              return futures::Past(vm::Value::NewVoid(pool));
                            })
                            .Transform(ModifyHandler<gc::Root<vm::Value>>(
                                options.modify_handler, buffer));
                      });
                  return;
                }

                case ControlChar::kCtrlV:
                  if (old_literal) {
                    DLOG(INFO) << "Inserting literal CTRL_V";
                    WriteLineBuffer(buffers_, {22});
                    ProcessRegular(wchar_t(22), true);
                  } else {
                    DLOG(INFO) << "Set literal.";
                    status_expiration_for_literal_ =
                        options_.editor_state.status()
                            .SetExpiringInformationText(
                                MakeNonNullShared<Line>(Line(L"<literal>")));
                    return;
                  }
                  break;

                case ControlChar::kCtrlK: {
                  ResetScrollBehavior();
                  StartNewInsertion();

                  ForEachActiveBuffer(
                      buffers_, {0x0b},
                      [options = options_](OpenBuffer& buffer) {
                        return buffer
                            .ApplyToCursors(transformation::Delete{
                                .modifiers =
                                    {.structure = Structure::kLine,
                                     .paste_buffer_behavior = Modifiers::
                                         PasteBufferBehavior::kDoNothing,
                                     .boundary_begin =
                                         Modifiers::CURRENT_POSITION,
                                     .boundary_end = Modifiers::LIMIT_CURRENT},
                                .initiator =
                                    transformation::Delete::Initiator::kUser})
                            .Transform(ModifyHandler<EmptyValue>(
                                options.modify_handler, buffer));
                      });
                  return;
                }
              }
            }},
        c);
  }

  void ProcessRegular(wchar_t regular_c, bool old_literal) {
    TRACK_OPERATION(InsertMode_ProcessInput_Regular);
    switch (regular_c) {
      case '\t':
        ResetScrollBehavior();
        if (!old_literal) {
          bool started_completion = false;
          ForEachActiveBuffer(
              buffers_, L"",
              [options = options_, &started_completion](OpenBuffer& buffer) {
                if (buffer.fd() == nullptr && options.start_completion(buffer))
                  started_completion = true;
                return futures::Past(EmptyValue());
              });
          if (started_completion) {
            // Whatever was being typed, was probably just for
            // completion purposes; we might as well not let it be
            // added to the history (so as to not pollute it).
            current_insertion_->DeleteToLineEnd(LineColumn());
            return;
          }
        }

        LOG(INFO) << "Inserting TAB (old_literal: " << old_literal << ")";
        break;
      case '\n':
        ResetScrollBehavior();

        // TODO(2022-05-22): Not sure StartNewInsertion is the best to
        // do here; would be better to leave a \n in the insertion?
        StartNewInsertion();

        ForEachActiveBuffer(buffers_, {'\n'}, [&](OpenBuffer& buffer) {
          return options_.new_line_handler(buffer);
        });
        return;

      case ' ':
        ResetScrollBehavior();
        ForEachActiveBuffer(
            buffers_, {' '},
            [modify_mode = options_.editor_state.modifiers().insertion,
             completion_model_supplier =
                 completion_model_supplier_](OpenBuffer& buffer) {
              CHECK(buffer.fd() == nullptr);
              return ApplyCompletionModel(buffer, modify_mode,
                                          completion_model_supplier);
            });
        return;
    }

    ResetScrollBehavior();

    // TODO: Apply TransformKeyboardText for buffers with fd?
    ForEachActiveBuffer(
        buffers_, {regular_c},
        [regular_c, options = options_,
         completion_model_supplier =
             completion_model_supplier_](OpenBuffer& buffer) {
          gc::Root<OpenBuffer> buffer_root = buffer.NewRoot();
          return buffer.TransformKeyboardText(std::wstring(1, regular_c))
              .Transform([options, buffer_root](std::wstring value) {
                VLOG(6) << "Inserting text: [" << value << "]";
                return buffer_root.ptr()->ApplyToCursors(transformation::Insert{
                    .contents_to_insert =
                        LineSequence::WithLine(MakeNonNullShared<Line>(value)),
                    .modifiers = {
                        .insertion =
                            options.editor_state.modifiers().insertion}});
              })
              .Transform([buffer_root, completion_model_supplier](EmptyValue) {
                Range token_range = GetTokenRange(buffer_root.ptr().value());
                if (token_range.IsEmpty()) return futures::Past(EmptyValue{});
                CompletionModelManager::CompressedText token =
                    GetCompletionToken(buffer_root.ptr()->contents().snapshot(),
                                       token_range);
                return completion_model_supplier
                    ->Query(std::move(
                                CompletionModelPaths(buffer_root.ptr().value())
                                    .value()),
                            token)
                    .Transform([buffer_root, token](
                                   CompletionModelManager::QueryOutput output) {
                      std::visit(
                          overload{[](CompletionModelManager::NothingFound) {},
                                   [](CompletionModelManager::Suggestion) {},
                                   [&](CompletionModelManager::Text text) {
                                     ShowSuggestion(buffer_root.ptr().value(),
                                                    token, text);
                                   }},
                          output);
                      return futures::Past(EmptyValue{});
                    });
              })
              .Transform(
                  ModifyHandler<EmptyValue>(options.modify_handler, buffer));
        });
    current_insertion_->AppendToLine(current_insertion_->EndLine(),
                                     Line(std::wstring(1, regular_c)));
  }

  CursorMode cursor_mode() const override {
    switch (options_.editor_state.modifiers().insertion) {
      case Modifiers::ModifyMode::kShift:
        return CursorMode::kInserting;
      case Modifiers::ModifyMode::kOverwrite:
        return CursorMode::kOverwriting;
    }
    LOG(FATAL) << "Invalid cursor mode.";
    return CursorMode::kInserting;
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  // Writes `line_buffer` to every buffer with a fd, and runs `callable` in
  // every buffer without an fd.
  static futures::Value<EmptyValue> ForEachActiveBuffer(
      NonNull<std::shared_ptr<std::vector<gc::Root<OpenBuffer>>>> buffers,
      std::wstring line_buffer,
      std::function<futures::Value<EmptyValue>(OpenBuffer&)> callable) {
    return WriteLineBuffer(buffers, line_buffer)
        .Transform([buffers, callable](EmptyValue) {
          return futures::ForEach(
              buffers.get_shared(),
              [callable](gc::Root<OpenBuffer>& buffer_root) {
                return buffer_root.ptr()->fd() == nullptr
                           ? callable(buffer_root.ptr().value())
                                 .Transform([](EmptyValue) {
                                   return futures::IterationControlCommand::
                                       kContinue;
                                 })
                           : futures::Past(
                                 futures::IterationControlCommand::kContinue);
              });
        })
        .Transform(
            [](futures::IterationControlCommand) { return EmptyValue(); });
  }

  void StartNewInsertion() {
    current_insertion_ = NewInsertion(options_.editor_state);
  }

  void HandleDelete(std::wstring line_buffer, Direction direction) {
    ResetScrollBehavior();
    ForEachActiveBuffer(
        buffers_, line_buffer,
        [direction, options = options_](OpenBuffer& buffer) {
          buffer.MaybeAdjustPositionCol();
          gc::Root<OpenBuffer> buffer_root = buffer.NewRoot();
          transformation::Stack stack;
          stack.PushBack(transformation::Delete{
              .modifiers = {.direction = direction,
                            .paste_buffer_behavior =
                                Modifiers::PasteBufferBehavior::kDoNothing},
              .initiator = transformation::Delete::Initiator::kUser});
          switch (options.editor_state.modifiers().insertion) {
            case Modifiers::ModifyMode::kShift:
              break;
            case Modifiers::ModifyMode::kOverwrite:
              stack.PushBack(transformation::Insert{
                  .contents_to_insert =
                      LineSequence::WithLine(MakeNonNullShared<Line>(L" ")),
                  .final_position =
                      direction == Direction::kBackwards
                          ? transformation::Insert::FinalPosition::kStart
                          : transformation::Insert::FinalPosition::kEnd});
              break;
          }

          return buffer.ApplyToCursors(stack).Transform(
              ModifyHandler<EmptyValue>(options.modify_handler, buffer));
        });
    switch (direction) {
      case Direction::kBackwards:
        if (current_insertion_->back()->empty()) {
          StartNewInsertion();
        } else {
          current_insertion_->DeleteToLineEnd(
              current_insertion_->PositionBefore(
                  current_insertion_->range().end()));
        }
        break;
      case Direction::kForwards:
        StartNewInsertion();
    }
  }

  void ApplyScrollBehavior(std::wstring line_buffer,
                           void (ScrollBehavior::*method)(OpenBuffer&)) {
    current_insertion_ = NewInsertion(options_.editor_state);
    GetScrollBehavior().AddListener(
        [buffers = buffers_, line_buffer,
         abort_value = scroll_behavior_abort_notification_->listenable_value(),
         method](NonNull<std::shared_ptr<ScrollBehavior>> scroll_behavior) {
          if (abort_value.has_value()) return;
          ForEachActiveBuffer(buffers, line_buffer,
                              [scroll_behavior, method](OpenBuffer& buffer) {
                                if (buffer.fd() == nullptr) {
                                  (scroll_behavior.value().*method)(buffer);
                                }
                                return futures::Past(EmptyValue());
                              });
        });
  }

  template <typename T>
  static std::function<futures::Value<EmptyValue>(const T&)> ModifyHandler(
      std::function<futures::Value<language::EmptyValue>(OpenBuffer&)> handler,
      OpenBuffer& buffer) {
    return [handler, buffer_root = buffer.NewRoot()](const T&) {
      return handler(buffer_root.ptr().value());
    };
  }

  futures::ListenableValue<NonNull<std::shared_ptr<ScrollBehavior>>>
  GetScrollBehavior() {
    if (!scroll_behavior_.has_value()) {
      scroll_behavior_ = futures::ListenableValue(
          options_.scroll_behavior
              ->Build(scroll_behavior_abort_notification_->listenable_value())
              .Transform(
                  [](NonNull<std::unique_ptr<ScrollBehavior>> scroll_behavior) {
                    return NonNull<std::shared_ptr<ScrollBehavior>>(
                        std::move(scroll_behavior));
                  }));
    }
    return scroll_behavior_.value();
  }

  void ResetScrollBehavior() {
    scroll_behavior_abort_notification_ =
        MakeNonNullUnique<DeleteNotification>();
    scroll_behavior_ = std::nullopt;
  }

  static futures::Value<EmptyValue> WriteLineBuffer(
      NonNull<std::shared_ptr<std::vector<gc::Root<OpenBuffer>>>> buffers,
      std::wstring line_buffer) {
    if (line_buffer.empty()) return futures::Past(EmptyValue());
    return futures::ForEach(
               buffers.get_shared(),
               [line_buffer =
                    std::move(line_buffer)](gc::Root<OpenBuffer>& buffer_root) {
                 OpenBuffer& buffer = buffer_root.ptr().value();
                 if (auto fd = buffer.fd(); fd != nullptr) {
                   std::string bytes = ToByteString(line_buffer);
                   if (write(fd->fd().read(), bytes.c_str(), bytes.size()) ==
                       -1) {
                     buffer.status().InsertError(Error(
                         L"Write failed: " + FromByteString(strerror(errno))));
                   } else {
                     buffer.editor().StartHandlingInterrupts();
                   }
                 }
                 return futures::Past(
                     futures::IterationControlCommand::kContinue);
               })
        .Transform(
            [](futures::IterationControlCommand) { return EmptyValue(); });
  }

  static NonNull<std::shared_ptr<std::vector<Path>>> CompletionModelPaths(
      const OpenBuffer& buffer) {
    return MakeNonNullShared<std::vector<Path>>(container::Map(
        [](Path path) {
          VLOG(5) << "Loading model: " << path;
          return Path::Join(
              ValueOrDie(PathComponent::FromString(L"completion_models")),
              std::move(path));
        },
        container::Filter(container::Map(
            [](Token path_str) {
              return OptionalFrom(Path::FromString(path_str.value));
            },
            TokenizeBySpaces(
                NewLazyString(
                    buffer.Read(buffer_variables::completion_model_paths))
                    .value())))));
  }

  static Range GetTokenRange(OpenBuffer& buffer) {
    const LineColumn position = buffer.AdjustLineColumn(buffer.position());
    const NonNull<std::shared_ptr<const Line>> line =
        buffer.contents().at(position.line);

    ColumnNumber start = position.column;
    CHECK_LE(start.ToDelta(), line->contents()->size());
    while (!start.IsZero() &&
           (std::isalpha(line->get(start - ColumnNumberDelta(1))) ||
            line->get(start - ColumnNumberDelta(1)) == L'\''))
      --start;
    return Range::InLine(position.line, start, position.column - start);
  }

  static CompletionModelManager::CompressedText GetCompletionToken(
      const LineSequence& buffer_contents, Range token_range) {
    CompletionModelManager::CompressedText output = LowerCase(
        Substring(buffer_contents.at(token_range.begin().line)->contents(),
                  token_range.begin().column,
                  token_range.end().column - token_range.begin().column));
    VLOG(6) << "Found completion token: " << output.value();
    return output;
  }

  static void ShowSuggestion(
      OpenBuffer& buffer,
      CompletionModelManager::CompressedText compressed_text,
      CompletionModelManager::Text text) {
    buffer.work_queue()->DeleteLater(
        AddSeconds(Now(), 2.0),
        std::shared_ptr<StatusExpirationControl>(
            buffer.status().SetExpiringInformationText(MakeNonNullShared<Line>(
                LineBuilder(Append(NewLazyString(L"`"), compressed_text,
                                   Append(NewLazyString(L"` is an alias for `"),
                                          text, NewLazyString(L"`"))))
                    .Build()))));
  }

  static futures::Value<EmptyValue> ApplyCompletionModel(
      OpenBuffer& buffer, Modifiers::ModifyMode modify_mode,
      NonNull<std::shared_ptr<CompletionModelManager>>
          completion_model_supplier) {
    const auto model_paths = CompletionModelPaths(buffer);
    const LineColumn position = buffer.AdjustLineColumn(buffer.position());
    Range token_range = GetTokenRange(buffer);
    futures::Value<EmptyValue> output =
        buffer.ApplyToCursors(transformation::Insert{
            .contents_to_insert =
                LineSequence::WithLine(MakeNonNullShared<Line>(
                    LineBuilder(NewLazyString(L" ")).Build())),
            .modifiers = {.insertion = modify_mode}});

    if (model_paths->empty() || buffer.Read(buffer_variables::paste_mode)) {
      VLOG(5) << "No tokens found in buffer_variables::completion_model_paths.";
      return output;
    }

    const NonNull<std::shared_ptr<const Line>> line =
        buffer.contents().at(position.line);

    auto buffer_root = buffer.NewRoot();
    if (token_range.IsEmpty()) {
      VLOG(5) << "Unable to rewind for completion token.";
      static const wchar_t kCompletionDisableSuffix = L'-';
      if (token_range.end().column >= ColumnNumber(2) &&
          line->get(token_range.end().column - ColumnNumberDelta(1)) ==
              kCompletionDisableSuffix &&
          line->get(token_range.end().column - ColumnNumberDelta(2)) != L' ') {
        return std::move(output).Transform([buffer_root, position](EmptyValue) {
          VLOG(3) << "Found completion disabling suffix; removing it.";
          transformation::Stack stack;
          stack.PushBack(transformation::Delete{
              .range = Range::InLine(position.line,
                                     position.column - ColumnNumberDelta(1),
                                     ColumnNumberDelta(1)),
              .initiator = transformation::Delete::Initiator::kInternal});
          stack.PushBack(transformation::SetPosition(position.column));
          return buffer_root.ptr()->ApplyToCursors(std::move(stack));
        });
      }
      return output;
    }

    CompletionModelManager::CompressedText token =
        GetCompletionToken(buffer.contents().snapshot(), token_range);
    return std::move(output).Transform([model_paths = std::move(model_paths),
                                        token, position, modify_mode,
                                        buffer_root, completion_model_supplier](
                                           EmptyValue) mutable {
      return completion_model_supplier
          ->Query(std::move(model_paths.value()), token)
          .Transform(VisitCallback(overload{
              [buffer_root, token,
               position_start =
                   LineColumn(position.line, position.column - token->size()),
               length = token->size(),
               modify_mode](CompletionModelManager::Text completion_text) {
                transformation::Stack stack;
                stack.PushBack(transformation::Delete{
                    .range = Range::InLine(position_start, length),
                    .initiator = transformation::Delete::Initiator::kInternal});
                const ColumnNumberDelta completion_text_size =
                    completion_text->size();
                stack.PushBack(transformation::Insert{
                    .contents_to_insert =
                        LineSequence::WithLine(MakeNonNullShared<Line>(
                            LineBuilder(std::move(completion_text)).Build())),
                    .modifiers = {.insertion = modify_mode},
                    .position = position_start});
                stack.PushBack(transformation::SetPosition(
                    position_start.column + completion_text_size +
                    ColumnNumberDelta(1)));
                return buffer_root.ptr()->ApplyToCursors(std::move(stack));
              },
              [buffer_root,
               token](CompletionModelManager::Suggestion suggestion) {
                ShowSuggestion(buffer_root.ptr().value(),
                               suggestion.compressed_text,
                               CompletionModelManager::Text(token));
                return futures::Past(EmptyValue());
              },
              [](CompletionModelManager::NothingFound) {
                return futures::Past(EmptyValue());
              }}));
    });
  }

  const InsertModeOptions options_;

  // Copy of the contents of options_.buffers. shared_ptr to make it easy for
  // it to be captured efficiently.
  //
  // TODO(trivial, 2023-10-14): Turn them into `ptr` and just add them in
  // `Expand`.
  const NonNull<std::shared_ptr<std::vector<gc::Root<OpenBuffer>>>> buffers_;

  std::optional<
      futures::ListenableValue<NonNull<std::shared_ptr<ScrollBehavior>>>>
      scroll_behavior_;

  // If nullptr, next key should be interpreted directly. If non-null, next
  // key should be inserted literally.
  std::unique_ptr<StatusExpirationControl,
                  std::function<void(StatusExpirationControl*)>>
      status_expiration_for_literal_ = nullptr;

  // Given to ScrollBehaviorFactory::Build, and used to signal when we want to
  // abort the build of the history.
  NonNull<std::unique_ptr<DeleteNotification>>
      scroll_behavior_abort_notification_;

  std::unique_ptr<MutableLineSequence,
                  std::function<void(MutableLineSequence*)>>
      current_insertion_;

  NonNull<std::shared_ptr<CompletionModelManager>> completion_model_supplier_;
};

void EnterInsertCharactersMode(InsertModeOptions options) {
  for (OpenBuffer& buffer : RootValueView(options.buffers.value())) {
    if (buffer.fd() == nullptr) continue;
    if (buffer.Read(buffer_variables::extend_lines)) {
      buffer.MaybeExtendLine(buffer.position());
    } else {
      buffer.MaybeAdjustPositionCol();
    }
  }
  for (OpenBuffer& buffer : RootValueView(options.buffers.value()))
    buffer.status().SetInformationText(
        MakeNonNullShared<Line>(buffer.fd() == nullptr ? L"🔡" : L"🔡 (raw)"));
  options.editor_state.set_keyboard_redirect(
      std::make_unique<InsertMode>(options));

  if (std::ranges::any_of(
          RootValueView(options.buffers.value()), [](OpenBuffer& buffer) {
            return buffer.active_cursors().size() > 1 &&
                   buffer.Read(buffer_variables::multiple_cursors);
          }))
    audio::BeepFrequencies(
        options.editor_state.audio_player(), 0.1,
        {audio::Frequency(659.25), audio::Frequency(1046.50)});
}
}  // namespace

void DefaultScrollBehavior::PageUp(OpenBuffer& buffer) {
  buffer.ApplyToCursors(transformation::ModifiersAndComposite{
      {.structure = Structure::kPage, .direction = Direction::kBackwards},
      NewMoveTransformation()});
}

void DefaultScrollBehavior::PageDown(OpenBuffer& buffer) {
  buffer.ApplyToCursors(transformation::ModifiersAndComposite{
      {.structure = Structure::kPage}, NewMoveTransformation()});
}

void DefaultScrollBehavior::Up(OpenBuffer& buffer) {
  buffer.ApplyToCursors(transformation::ModifiersAndComposite{
      {.structure = Structure::kLine, .direction = Direction::kBackwards},
      NewMoveTransformation()});
}

void DefaultScrollBehavior::Down(OpenBuffer& buffer) {
  buffer.ApplyToCursors(transformation::ModifiersAndComposite{
      {.structure = Structure::kLine}, NewMoveTransformation()});
}

void DefaultScrollBehavior::Left(OpenBuffer& buffer) {
  buffer.ApplyToCursors(transformation::ModifiersAndComposite{
      {.direction = Direction::kBackwards}, NewMoveTransformation()});
}

void DefaultScrollBehavior::Right(OpenBuffer& buffer) {
  buffer.ApplyToCursors(NewMoveTransformation());
}

void DefaultScrollBehavior::Begin(OpenBuffer& buffer) {
  buffer.ApplyToCursors(transformation::SetPosition(ColumnNumber(0)));
}

void DefaultScrollBehavior::End(OpenBuffer& buffer) {
  buffer.ApplyToCursors(
      transformation::SetPosition(std::numeric_limits<ColumnNumber>::max()));
}

gc::Root<Command> NewFindCompletionCommand(EditorState& editor_state) {
  return editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<FindCompletionCommand>(editor_state));
}

/* static */ NonNull<std::unique_ptr<ScrollBehaviorFactory>>
ScrollBehaviorFactory::Default() {
  class DefaultScrollBehaviorFactory : public ScrollBehaviorFactory {
    futures::Value<NonNull<std::unique_ptr<ScrollBehavior>>> Build(
        DeleteNotification::Value) override {
      return futures::Past(NonNull<std::unique_ptr<ScrollBehavior>>(
          MakeNonNullUnique<DefaultScrollBehavior>()));
    }
  };

  return MakeNonNullUnique<DefaultScrollBehaviorFactory>();
}

void EnterInsertMode(InsertModeOptions options) {
  auto shared_options = std::make_shared<InsertModeOptions>(std::move(options));

  if (!shared_options->buffers.has_value()) {
    shared_options->buffers = shared_options->editor_state.active_buffers();
  }

  (shared_options->buffers.value().empty()
       ? OpenAnonymousBuffer(shared_options->editor_state)
             .Transform([shared_options](gc::Root<OpenBuffer> buffer) {
               shared_options->buffers.value().push_back(buffer);
               return EmptyValue();
             })
       : futures::Past(EmptyValue()))
      .Transform([shared_options](EmptyValue) {
        for (gc::Root<OpenBuffer>& buffer_root :
             shared_options->buffers.value()) {
          VisitPointer(
              buffer_root.ptr()->CurrentLine()->outgoing_link(),
              [&](const OutgoingLink& link) {
                if (auto it = buffer_root.ptr()->editor().buffers()->find(
                        BufferName(link.path));
                    it != buffer_root.ptr()->editor().buffers()->end())
                  buffer_root = it->second.ptr().ToRoot();
              },
              [] {});
        }

        if (shared_options->modify_handler == nullptr) {
          shared_options->modify_handler = [](OpenBuffer&) {
            return futures::Past(EmptyValue()); /* Nothing. */
          };
        }

        if (!shared_options->escape_handler) {
          shared_options->escape_handler = []() { /* Nothing. */ };
        }

        if (!shared_options->new_line_handler) {
          shared_options->new_line_handler = [](OpenBuffer& buffer) {
            return buffer.ApplyToCursors(
                MakeNonNullUnique<NewLineTransformation>());
          };
        }

        if (!shared_options->start_completion) {
          shared_options->start_completion = [](OpenBuffer& buffer) {
            LOG(INFO) << "Start default completion.";
            buffer.ApplyToCursors(NewExpandTransformation());
            return true;
          };
        }

        shared_options->editor_state.status().Reset();
        for (gc::Root<OpenBuffer>& buffer : shared_options->buffers.value()) {
          buffer.ptr()->status().Reset();
        }

        if (shared_options->editor_state.structure() == Structure::kChar ||
            shared_options->editor_state.structure() == Structure::kLine) {
          for (gc::Root<OpenBuffer>& buffer_root :
               shared_options->buffers.value()) {
            OpenBuffer& buffer = buffer_root.ptr().value();
            buffer.CheckPosition();
            buffer.PushTransformationStack();
            buffer.PushTransformationStack();
          }
          if (shared_options->editor_state.structure() == Structure::kLine) {
            for (gc::Root<OpenBuffer>& buffer :
                 shared_options->buffers.value()) {
              buffer.ptr()->ApplyToCursors(
                  MakeNonNullUnique<InsertEmptyLineTransformation>(
                      shared_options->editor_state.direction()));
            }
          }
          EnterInsertCharactersMode(*shared_options);
        }
        shared_options->editor_state.ResetDirection();
        shared_options->editor_state.ResetStructure();
        return EmptyValue();
      });
}

}  // namespace afc::editor

#include "src/line_prompt_mode.h"

#include <glog/logging.h>

#include <limits>
#include <memory>
#include <ranges>
#include <string>

#include "src/buffer.h"
#include "src/buffer_filter.h"
#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/command_mode.h"
#include "src/delay_input_receiver.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/file_link_mode.h"
#include "src/futures/delete_notification.h"
#include "src/infrastructure/dirname.h"
#include "src/insert_mode.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/overload.h"
#include "src/language/text/line_builder.h"
#include "src/language/wstring.h"
#include "src/predictor.h"
#include "src/terminal.h"
#include "src/tests/tests.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm/escape.h"
#include "src/vm/value.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::concurrent::ChannelAll;
using afc::concurrent::VersionPropertyKey;
using afc::concurrent::VersionPropertyReceiver;
using afc::concurrent::WorkQueueScheduler;
using afc::futures::DeleteNotification;
using afc::futures::JoinValues;
using afc::futures::ListenableValue;
using afc::infrastructure::AddSeconds;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::Now;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::InsertOrDie;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::language::text::Range;
using afc::vm::EscapedMap;
using afc::vm::EscapedString;
using afc::vm::Identifier;

namespace afc::editor {
namespace {

SingleLine GetPredictInput(const OpenBuffer& buffer) {
  Range range =
      buffer.FindPartialRange(Modifiers{.structure = Structure::kLine,
                                        .direction = Direction::kBackwards},
                              buffer.position());
  range.set_end(std::max(range.end(), buffer.position()));
  auto line = buffer.LineAt(range.begin().line);
  CHECK_LE(range.begin().column, line->EndColumn());
  if (range.begin().line == range.end().line) {
    CHECK_GE(range.end().column, range.begin().column);
    range.set_end_column(std::min(range.end().column, line->EndColumn()));
  } else {
    CHECK_GE(line->EndColumn(), range.begin().column);
  }
  return line->Substring(
      range.begin().column,
      (range.begin().line == range.end().line ? range.end().column
                                              : line->EndColumn()) -
          range.begin().column);
}

std::multimap<Identifier, EscapedString> GetCurrentFeatures(
    EditorState& editor) {
  std::multimap<Identifier, EscapedString> output;
  for (OpenBuffer& buffer :
       editor.buffer_registry().buffers() | gc::view::Value)
    if (buffer.Read(buffer_variables::show_in_buffers_list) &&
        editor.buffer_registry().GetListedBufferIndex(buffer).has_value())
      output.insert(
          {HistoryIdentifierName(),
           EscapedString{LazyString{buffer.Read(buffer_variables::name)}}});
  editor.ForEachActiveBuffer([&output](OpenBuffer& buffer) {
    output.insert(
        {HistoryIdentifierActive(),
         EscapedString{LazyString{buffer.Read(buffer_variables::name)}}});
    return futures::Past(EmptyValue());
  });
  return output;
}

futures::Value<gc::Root<OpenBuffer>> GetHistoryBuffer(EditorState& editor_state,
                                                      const HistoryFile& name) {
  HistoryBufferName buffer_name{name};
  if (std::optional<gc::Root<OpenBuffer>> buffer =
          editor_state.buffer_registry().Find(buffer_name);
      buffer.has_value())
    return futures::Past(buffer.value());
  return OpenOrCreateFile(
             {.editor_state = editor_state,
              .name = buffer_name,
              .path = editor_state.edge_path().empty()
                          ? std::nullopt
                          : std::make_optional(
                                Path::Join(editor_state.edge_path().front(),
                                           ValueOrDie(PathComponent::New(
                                               name.read().read().read() +
                                               LazyString{L"_history"})))),
              .insertion_type = BuffersList::AddBufferType::kIgnore})
      .Transform([&editor_state](gc::Root<OpenBuffer> buffer_root) {
        OpenBuffer& buffer = buffer_root.ptr().value();
        buffer.Set(buffer_variables::save_on_close, true);
        buffer.Set(buffer_variables::trigger_reload_on_buffer_write, false);
        buffer.Set(buffer_variables::show_in_buffers_list, false);
        buffer.Set(buffer_variables::atomic_lines, true);
        buffer.Set(buffer_variables::close_after_idle_seconds, 20.0);
        buffer.Set(buffer_variables::vm_lines_evaluation, false);
        if (!editor_state.has_current_buffer()) {
          // Seems lame, but what can we do?
          editor_state.set_current_buffer(buffer_root,
                                          CommandArgumentModeApplyMode::kFinal);
        }
        return buffer_root;
      });
}

SingleLine BuildHistoryLine(EditorState& editor, LazyString input) {
  EscapedMap::Map data = GetCurrentFeatures(editor);
  data.insert({HistoryIdentifierValue(), EscapedString{std::move(input)}});
  return EscapedMap{std::move(data)}.Serialize();
}

futures::Value<gc::Root<OpenBuffer>> FilterHistory(
    EditorState& editor_state, gc::Root<OpenBuffer> history_buffer,
    const HistoryFile&, DeleteNotification::Value abort_value,
    SingleLine filter) {
  gc::Root<OpenBuffer> filter_buffer_root = OpenBuffer::New(
      {.editor = editor_state,
       .name = FilterBufferName{
           .source_buffer = ToSingleLine(history_buffer.ptr()->name()),
           .filter = filter}});
  OpenBuffer& filter_buffer = filter_buffer_root.ptr().value();
  filter_buffer.Set(buffer_variables::allow_dirty_delete, true);
  filter_buffer.Set(buffer_variables::show_in_buffers_list, false);
  filter_buffer.Set(buffer_variables::delete_into_paste_buffer, false);
  filter_buffer.Set(buffer_variables::atomic_lines, true);
  filter_buffer.Set(buffer_variables::line_width, 1);

  return history_buffer.ptr()
      ->WaitForEndOfFile()
      .Transform([&editor_state, filter_buffer_root, history_buffer,
                  abort_value, filter](EmptyValue) {
        return editor_state.thread_pool().Run(std::bind_front(
            FilterSortBuffer,
            FilterSortBufferInput{
                .abort_value = abort_value,
                .filter = filter,
                .history = history_buffer.ptr()->contents().snapshot(),
                .current_features = GetCurrentFeatures(editor_state)}));
      })
      .Transform([&editor_state, abort_value, filter_buffer_root,
                  &filter_buffer](FilterSortBufferOutput output) {
        LOG(INFO) << "Receiving output from history evaluator.";
        if (!output.errors.empty()) {
          editor_state.work_queue()->DeleteLater(
              AddSeconds(Now(), 1.0),
              editor_state.status().SetExpiringInformationText(LineBuilder{
                  SingleLine{output.errors.front().read()}}.Build()));
        }
        if (!abort_value.has_value()) {
          filter_buffer.AppendLines(container::MaterializeVector(
              std::move(output.matches) |
              std::views::transform(&FilterSortBufferOutput::Match::preview)));
          if (filter_buffer.lines_size() > LineNumberDelta(1)) {
            VLOG(5) << "Erasing the first (empty) line.";
            CHECK(filter_buffer.LineAt(LineNumber())->empty());
            filter_buffer.EraseLines(LineNumber(), LineNumber().next());
          }
        }
        return filter_buffer_root;
      });
}

class StatusVersionAdapter;

futures::Value<gc::Root<OpenBuffer>> GetPromptBuffer(
    EditorState& editor, const LazyString& prompt_contents_type,
    Line initial_value) {
  BufferName name{LazyString{L"- prompt"}};
  gc::Root<OpenBuffer> output =
      editor.buffer_registry().MaybeAdd(name, [&editor, &name] {
        return OpenBuffer::New({.editor = editor, .name = name});
      });

  OpenBuffer& buffer = output.ptr().value();
  buffer.Set(buffer_variables::allow_dirty_delete, true);
  buffer.Set(buffer_variables::show_in_buffers_list, false);
  buffer.Set(buffer_variables::delete_into_paste_buffer, false);
  buffer.Set(buffer_variables::save_on_close, false);
  buffer.Set(buffer_variables::persist_state, false);
  buffer.Set(buffer_variables::completion_model_paths, LazyString{});
  return buffer.Reload()
      .ConsumeErrors([](Error) { return futures::Past(EmptyValue{}); })
      .Transform([&buffer, prompt_contents_type, initial_value](EmptyValue) {
        buffer.Set(buffer_variables::contents_type, prompt_contents_type);
        return buffer.ApplyToCursors(transformation::Insert{
            .contents_to_insert = LineSequence::WithLine(initial_value)});
      })
      .Transform([output = std::move(output)](EmptyValue) mutable {
        return std::move(output);
      });
}

// Holds the state required to show and update a prompt.
class PromptState : public std::enable_shared_from_this<PromptState> {
  struct ConstructorAccessKey {};

 public:
  static NonNull<std::shared_ptr<PromptState>> New(
      PromptOptions options, gc::Root<OpenBuffer> history,
      gc::Root<OpenBuffer> prompt_buffer) {
    return MakeNonNullShared<PromptState>(
        std::move(options), std::move(history), std::move(prompt_buffer),
        ConstructorAccessKey());
  }

  PromptState(PromptOptions options, gc::Root<OpenBuffer> history,
              gc::Root<OpenBuffer> prompt_buffer, ConstructorAccessKey)
      : options_(std::move(options)),
        history_(std::move(history)),
        prompt_buffer_(std::move(prompt_buffer)),
        status_buffer_([&]() -> std::optional<gc::Root<OpenBuffer>> {
          if (options.status == PromptOptions::Status::kEditor)
            return std::nullopt;
          auto active_buffers = editor_state().active_buffers();
          return active_buffers.size() == 1
                     ? active_buffers[0]
                     : std::optional<gc::Root<OpenBuffer>>();
        }()),
        status_(status_buffer_.has_value() ? status_buffer_->ptr()->status()
                                           : editor_state().status()),
        original_modifiers_(editor_state().modifiers()) {
    editor_state().set_modifiers(Modifiers());
  }

  InsertModeOptions insert_mode_options();
  const PromptOptions& options() const { return options_; }
  const gc::Root<OpenBuffer>& history() const { return history_; }
  const gc::Root<OpenBuffer>& prompt_buffer() const { return prompt_buffer_; }
  EditorState& editor_state() const { return options_.editor_state; }

  // The prompt has disappeared.
  bool IsGone() const { return status().GetType() != Status::Type::kPrompt; }

  Status& status() const { return status_; }

  futures::Value<EmptyValue> OnModify();

 private:
  void Reset() {
    status().Reset();
    editor_state().set_modifiers(original_modifiers_);
  }

  NonNull<std::unique_ptr<ProgressChannel>> NewProgressChannel(
      NonNull<std::shared_ptr<StatusVersionAdapter>> status_value_viewer);

  // status_buffer is the buffer with the contents of the prompt. tokens_future
  // is received as a future so that we can detect if the prompt input changes
  // between the time when `ColorizePrompt` is executed and the time when the
  // tokens become available.
  void ColorizePrompt(DeleteNotification::Value abort_value,
                      const Line& original_line,
                      ColorizePromptOptions options) {
    CHECK_EQ(prompt_buffer_.ptr()->lines_size(), LineNumberDelta(1));
    if (IsGone()) {
      LOG(INFO) << "Status is no longer a prompt, aborting colorize prompt.";
      return;
    }

    if (status().prompt_buffer().has_value() &&
        &status().prompt_buffer()->ptr().value() !=
            &prompt_buffer_.ptr().value()) {
      LOG(INFO) << "Prompt buffer has changed, aborting colorize prompt.";
      return;
    }
    if (abort_value.has_value()) {
      LOG(INFO) << "Abort notification notified, aborting colorize prompt.";
      return;
    }

    CHECK_EQ(prompt_buffer_.ptr()->lines_size(), LineNumberDelta(1));
    std::optional<Line> line = prompt_buffer_.ptr()->LineAt(LineNumber(0));
    if (original_line.contents() != line->contents()) {
      LOG(INFO) << "Line has changed, ignoring prompt colorize update.";
      return;
    }

    // TODO(easy, 2024-09-19): Avoid read():
    prompt_buffer_.ptr()->AppendRawLine(
        ColorizeLine(line->contents().read(), std::move(options.tokens)));
    prompt_buffer_.ptr()->EraseLines(LineNumber(0), LineNumber(1));
    std::visit(overload{[](ColorizePromptOptions::ContextUnmodified) {},
                        [&](ColorizePromptOptions::ContextClear) {
                          status().set_context(std::nullopt);
                        },
                        [&](const ColorizePromptOptions::ContextBuffer& value) {
                          status().set_context(value.buffer);
                        }},
               options.context);
  }

  const PromptOptions options_;
  const gc::Root<OpenBuffer> history_;

  // The buffer we create to hold the prompt.
  const gc::Root<OpenBuffer> prompt_buffer_;

  // If the status is associated with a buffer, we capture it here; that allows
  // us to ensure that the status won't be deallocated under our feet (when the
  // buffer is ephemeral).
  const std::optional<gc::Root<OpenBuffer>> status_buffer_;
  Status& status_;
  const Modifiers original_modifiers_;

  // Notification that can be used by a StatusVersionAdapter (and its customers)
  // to detect that the corresponding version is stale.
  NonNull<std::shared_ptr<DeleteNotification>> abort_notification_;
};

// Allows asynchronous operations to augment the information displayed in the
// status. Uses abort_notification_value to detect when the version is stale.
class StatusVersionAdapter {
 public:
  StatusVersionAdapter(NonNull<std::shared_ptr<PromptState>> prompt_state)
      : prompt_state_(std::move(prompt_state)),
        status_version_(prompt_state_->status()
                            .prompt_extra_information()
                            ->StartNewVersion()) {}

  // The prompt has disappeared.
  bool Expired() const {
    return prompt_state_->IsGone() || status_version_->IsExpired();
  }

  template <typename T>
  void SetStatusValue(VersionPropertyKey key, T value) {
    CHECK(prompt_state_->status().GetType() == Status::Type::kPrompt);
    status_version_->SetValue(key, value);
  }

  template <typename T>
  void SetStatusValues(T container) {
    if (!Expired())
      for (const auto& [key, value] : container) SetStatusValue(key, value);
  }

 private:
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;

  const NonNull<std::unique_ptr<VersionPropertyReceiver::Version>>
      status_version_;
};

NonNull<std::unique_ptr<ProgressChannel>> PromptState::NewProgressChannel(
    NonNull<std::shared_ptr<StatusVersionAdapter>> status_value_viewer) {
  return MakeNonNullUnique<ChannelAll<ProgressInformation>>(
      [work_queue = prompt_buffer_.ptr()->work_queue(),
       status_value_viewer](ProgressInformation extra_information) {
        work_queue->Schedule({.callback = [status_value_viewer,
                                           extra_information] {
          if (!status_value_viewer->Expired()) {
            status_value_viewer->SetStatusValues(extra_information.values);
            status_value_viewer->SetStatusValues(extra_information.counters);
          }
        }});
      });
}

futures::Value<EmptyValue> PromptState::OnModify() {
  Line line = prompt_buffer_.ptr()->contents().at(LineNumber());

  abort_notification_ = MakeNonNullShared<DeleteNotification>();
  auto abort_notification_value = abort_notification_->listenable_value();

  if (options().colorize_options_provider == nullptr ||
      status().GetType() != Status::Type::kPrompt)
    return futures::Past(EmptyValue());

  auto status_value_viewer = MakeNonNullShared<StatusVersionAdapter>(
      NonNull<std::shared_ptr<PromptState>>::Unsafe(shared_from_this()));

  return JoinValues(
             FilterHistory(editor_state(), history(), options_.history_file,
                           abort_notification_value, line.contents())
                 .Transform([status_value_viewer](
                                gc::Root<OpenBuffer> filtered_history) {
                   LOG(INFO) << "Propagating history information to status.";
                   if (!status_value_viewer->Expired()) {
                     bool last_line_empty =
                         filtered_history.ptr()
                             ->LineAt(filtered_history.ptr()->EndLine())
                             ->empty();
                     status_value_viewer->SetStatusValue(
                         VersionPropertyKey{
                             NON_EMPTY_SINGLE_LINE_CONSTANT(L"history")},
                         filtered_history.ptr()->lines_size().read() -
                             (last_line_empty ? 1 : 0));
                   }
                   return EmptyValue();
                 }),
             options()
                 .colorize_options_provider(
                     line.contents(), NewProgressChannel(status_value_viewer),
                     abort_notification_value)
                 .Transform([shared_this = shared_from_this(),
                             abort_notification_value, line](
                                ColorizePromptOptions colorize_prompt_options) {
                   LOG(INFO) << "Calling ColorizePrompt with results.";
                   shared_this->ColorizePrompt(abort_notification_value, line,
                                               colorize_prompt_options);
                   return EmptyValue();
                 }))
      .Transform([](auto) { return EmptyValue(); });
}

class HistoryScrollBehavior : public ScrollBehavior {
 public:
  HistoryScrollBehavior(
      futures::ListenableValue<gc::Root<OpenBuffer>> filtered_history,
      Line original_input, NonNull<std::shared_ptr<PromptState>> prompt_state)
      : filtered_history_(std::move(filtered_history)),
        original_input_(std::move(original_input)),
        prompt_state_(std::move(prompt_state)),
        previous_context_(prompt_state_->status().context()) {
    CHECK(prompt_state_->status().GetType() == Status::Type::kPrompt ||
          prompt_state_->IsGone());
  }

  void PageUp(OpenBuffer& buffer) override {
    ScrollHistory(buffer, LineNumberDelta(-1));
  }

  void PageDown(OpenBuffer& buffer) override {
    ScrollHistory(buffer, LineNumberDelta(+1));
  }

  void Up(OpenBuffer& buffer) override {
    ScrollHistory(buffer, LineNumberDelta(-1));
  }

  void Down(OpenBuffer& buffer) override {
    ScrollHistory(buffer, LineNumberDelta(+1));
  }

  void Left(OpenBuffer& buffer) override {
    DefaultScrollBehavior().Left(buffer);
  }

  void Right(OpenBuffer& buffer) override {
    DefaultScrollBehavior().Right(buffer);
  }

  void Begin(OpenBuffer& buffer) override {
    DefaultScrollBehavior().Begin(buffer);
  }

  void End(OpenBuffer& buffer) override { DefaultScrollBehavior().End(buffer); }

 private:
  void ScrollHistory(OpenBuffer& buffer, LineNumberDelta delta) const {
    if (prompt_state_->IsGone()) return;
    if (delta > LineNumberDelta() && !filtered_history_.has_value()) {
      ReplaceContents(buffer, LineSequence());
      return;
    }
    filtered_history_.AddListener([delta, buffer_root = buffer.NewRoot(),
                                   &buffer, original_input = original_input_,
                                   prompt_state = prompt_state_,
                                   previous_context = previous_context_](
                                      gc::Root<OpenBuffer> history_root) {
      std::optional<Line> line_to_insert;
      OpenBuffer& history = history_root.ptr().value();
      if (history.contents().size() > LineNumberDelta(1) ||
          !history.LineAt(LineNumber())->empty()) {
        LineColumn position = history.position();
        position.line = std::min(position.line.PlusHandlingOverflow(delta),
                                 LineNumber() + history.contents().size());
        history.set_position(position);
        if (position.line < LineNumber(0) + history.contents().size()) {
          prompt_state->status().set_context(history_root);
          VisitPointer(
              history.OptionalCurrentLine(),
              [&line_to_insert](Line line) { line_to_insert = line; }, [] {});
        } else if (prompt_state->status().context() != previous_context) {
          prompt_state->status().set_context(previous_context);
          line_to_insert = original_input;
        }
      }
      LineBuilder line_builder;
      VisitPointer(
          line_to_insert,
          [&](Line line) {
            VLOG(5) << "Inserting line: " << line.contents();
            line_builder.Append(LineBuilder(line));
          },
          [] {});
      ReplaceContents(buffer,
                      LineSequence::WithLine(std::move(line_builder).Build()));
    });
  }

  static void ReplaceContents(OpenBuffer& buffer,
                              LineSequence contents_to_insert) {
    buffer.ApplyToCursors(transformation::Delete{
        .modifiers = {.structure = Structure::kLine,
                      .paste_buffer_behavior =
                          Modifiers::PasteBufferBehavior::kDoNothing,
                      .boundary_begin = Modifiers::LIMIT_CURRENT,
                      .boundary_end = Modifiers::LIMIT_CURRENT},
        .initiator = transformation::Delete::Initiator::kInternal});

    buffer.ApplyToCursors(transformation::Insert{
        .contents_to_insert = std::move(contents_to_insert)});
  }

  const futures::ListenableValue<gc::Root<OpenBuffer>> filtered_history_;
  const Line original_input_;
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;
  const std::optional<gc::Root<OpenBuffer>> previous_context_;
};

class HistoryScrollBehaviorFactory : public ScrollBehaviorFactory {
 public:
  HistoryScrollBehaviorFactory(
      NonNull<std::shared_ptr<PromptState>> prompt_state)
      : prompt_state_(std::move(prompt_state)) {}

  futures::Value<NonNull<std::unique_ptr<ScrollBehavior>>> Build(
      DeleteNotification::Value abort_value) override {
    OpenBuffer& buffer = prompt_state_->prompt_buffer().ptr().value();
    CHECK_GT(buffer.lines_size(), LineNumberDelta(0));
    Line input = buffer.contents().at(LineNumber(0));
    return futures::Past(MakeNonNullUnique<HistoryScrollBehavior>(
        futures::ListenableValue(
            FilterHistory(prompt_state_->editor_state(),
                          prompt_state_->history(),
                          prompt_state_->options().history_file, abort_value,
                          input.contents())
                .Transform([](gc::Root<OpenBuffer> history_filtered) {
                  history_filtered.ptr()->set_current_position_line(
                      LineNumber(0) +
                      history_filtered.ptr()->contents().size());
                  return history_filtered;
                })),
        input, prompt_state_));
  }

 private:
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;
};

class LinePromptCommand : public Command {
  struct ConstructorAccessTag {};

  EditorState& editor_state_;
  const LazyString description_;
  const std::function<PromptOptions()> options_supplier_;

 public:
  static gc::Root<LinePromptCommand> New(
      EditorState& editor_state, std::wstring description,
      std::function<PromptOptions()> options_supplier) {
    return editor_state.gc_pool().NewRoot(MakeNonNullUnique<LinePromptCommand>(
        ConstructorAccessTag(), editor_state, std::move(description),
        std::move(options_supplier)));
  }

  LinePromptCommand(ConstructorAccessTag, EditorState& editor_state,
                    std::wstring description,
                    std::function<PromptOptions()> options_supplier)
      : editor_state_(editor_state),
        // TODO(2024-01-24): Avoid call to LazyString.
        description_(LazyString{std::move(description)}),
        options_supplier_(std::move(options_supplier)) {}

  LazyString Description() const override { return description_; }
  CommandCategory Category() const override {
    return CommandCategory::kPrompt();
  }

  void ProcessInput(ExtendedChar) override {
    auto buffer = editor_state_.current_buffer();
    if (!buffer.has_value()) return;
    auto options = options_supplier_();
    if (editor_state_.structure() == Structure::kLine) {
      editor_state_.ResetStructure();
      VisitPointer(
          buffer->ptr()->OptionalCurrentLine(),
          [&](Line line) {
            AddLineToHistory(editor_state_, options.history_file,
                             line.contents().read());
            options.handler(line.contents());
          },
          [] {});
    } else {
      Prompt(std::move(options));
    }
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }
};

}  // namespace

HistoryFile HistoryFileFiles() {
  return HistoryFile{NON_EMPTY_SINGLE_LINE_CONSTANT(L"files")};
}
HistoryFile HistoryFileCommands() {
  return HistoryFile{NON_EMPTY_SINGLE_LINE_CONSTANT(L"commands")};
}

// input must not be escaped.
void AddLineToHistory(EditorState& editor, const HistoryFile& history_file,
                      LazyString input) {
  if (input.size().IsZero()) return;
  switch (editor.args().prompt_history_behavior) {
    case CommandLineValues::HistoryFileBehavior::kUpdate:
      GetHistoryBuffer(editor, history_file)
          .Transform([history_line = BuildHistoryLine(editor, input)](
                         gc::Root<OpenBuffer> history) {
            history.ptr()->AppendLine(history_line);
            return Success();
          });
      return;
    case CommandLineValues::HistoryFileBehavior::kReadOnly:
      break;
  }
}

InsertModeOptions PromptState::insert_mode_options() {
  NonNull<std::shared_ptr<PromptState>> prompt_state =
      NonNull<std::shared_ptr<PromptState>>::Unsafe(shared_from_this());
  return InsertModeOptions{
      .editor_state = editor_state(),
      .buffers = {{prompt_state->prompt_buffer()}},
      .modify_handler =
          [prompt_state](OpenBuffer& buffer) {
            CHECK_EQ(&buffer, &prompt_state->prompt_buffer().ptr().value());
            return prompt_state->OnModify();
          },
      .scroll_behavior =
          MakeNonNullShared<HistoryScrollBehaviorFactory>(prompt_state),
      .escape_handler =
          [prompt_state]() {
            LOG(INFO) << "Running escape_handler from Prompt.";
            prompt_state->Reset();

            if (prompt_state->options().cancel_handler) {
              VLOG(5) << "Running cancel handler.";
              prompt_state->options().cancel_handler();
            } else {
              VLOG(5) << "Running handler on empty input.";
              prompt_state->options().handler(SingleLine{});
            }
            prompt_state->editor_state().set_keyboard_redirect(std::nullopt);
          },
      .new_line_handler =
          [prompt_state](OpenBuffer& buffer) {
            SingleLine input = buffer.CurrentLine().contents();
            AddLineToHistory(prompt_state->editor_state(),
                             prompt_state->options().history_file,
                             input.read());
            auto ensure_survival_of_current_closure =
                prompt_state->editor_state().set_keyboard_redirect(
                    std::nullopt);
            prompt_state->Reset();
            return prompt_state->options().handler(input);
          },
      .start_completion =
          [prompt_state](OpenBuffer& buffer) {
            SingleLine input = buffer.CurrentLine().contents();
            LOG(INFO) << "Triggering predictions from: " << input;
            CHECK(prompt_state->status().prompt_extra_information() != nullptr);
            auto status_version_value =
                MakeNonNullShared<StatusVersionAdapter>(prompt_state);
            NonNull<std::unique_ptr<ProgressChannel>> progress_channel =
                prompt_state->NewProgressChannel(status_version_value);
            progress_channel->Push(ProgressInformation{
                .values = {
                    {VersionPropertyKey{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🔮")},
                     SINGLE_LINE_CONSTANT(L"…")}}});
            Predict(
                prompt_state->options().predictor,
                PredictorInput{
                    .editor = prompt_state->editor_state(),
                    .input = GetPredictInput(buffer),
                    .input_column = buffer.position().column,
                    .source_buffers = prompt_state->options().source_buffers,
                    .progress_channel = std::move(progress_channel),
                    .abort_value =
                        prompt_state->abort_notification_->listenable_value()})
                .Transform([prompt_state, status_version_value,
                            input](std::optional<PredictResults> results) {
                  if (!results.has_value()) {
                    status_version_value->SetStatusValue(
                        VersionPropertyKey{
                            NON_EMPTY_SINGLE_LINE_CONSTANT(L"🔮")},
                        SINGLE_LINE_CONSTANT(L"empty"));
                    return EmptyValue();
                  }
                  // TODO(easy, 2024-08-28): Convert `common_prefix` to
                  // LazyString and avoid wrapping it here.
                  if (results.value().common_prefix.has_value() &&
                      !results.value().common_prefix.value().empty() &&
                      input.read() !=
                          LazyString{results.value().common_prefix.value()}) {
                    LOG(INFO) << "Prediction advanced from " << input << " to "
                              << results.value();
                    status_version_value->SetStatusValue(
                        VersionPropertyKey{
                            NON_EMPTY_SINGLE_LINE_CONSTANT(L"🔮")},
                        SINGLE_LINE_CONSTANT(L"advanced"));

                    prompt_state->prompt_buffer().ptr()->ApplyToCursors(
                        transformation::Delete{
                            .modifiers =
                                {.structure = Structure::kLine,
                                 .paste_buffer_behavior =
                                     Modifiers::PasteBufferBehavior::kDoNothing,
                                 .boundary_begin = Modifiers::LIMIT_CURRENT,
                                 .boundary_end = Modifiers::LIMIT_CURRENT},
                            .initiator =
                                transformation::Delete::Initiator::kInternal});

                    // TODO(easy, 2024-09-17): Change common_prefix to be
                    // SingleLine, avoid wrapping it here.
                    SingleLine line{
                        LazyString{results.value().common_prefix.value()}};

                    prompt_state->prompt_buffer().ptr()->ApplyToCursors(
                        transformation::Insert(
                            {.contents_to_insert = LineSequence::WithLine(
                                 LineBuilder{std::move(line)}.Build())}));
                    prompt_state->OnModify();
                    return EmptyValue();
                  }
                  LOG(INFO) << "Prediction didn't advance.";
                  status_version_value->SetStatusValue(
                      VersionPropertyKey{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🔮")},
                      SINGLE_LINE_CONSTANT(L"stuck"));
                  auto buffers =
                      prompt_state->editor_state().buffer_registry().buffers();
                  if (std::optional<gc::Root<OpenBuffer>> predictions_buffer =
                          prompt_state->editor_state().buffer_registry().Find(
                              PredictionsBufferName{});
                      predictions_buffer.has_value()) {
                    predictions_buffer->ptr()->set_current_position_line(
                        LineNumber(0));
                    prompt_state->editor_state().set_current_buffer(
                        predictions_buffer.value(),
                        CommandArgumentModeApplyMode::kFinal);
                    if (!prompt_state->editor_state()
                             .status()
                             .prompt_buffer()
                             .has_value()) {
                      predictions_buffer->ptr()->status().CopyFrom(
                          prompt_state->status());
                    }
                  } else {
                    prompt_state->editor_state().status().InsertError(Error{
                        LazyString{
                            L"Error: Predict: predictions buffer not found: "} +
                        ToSingleLine(PredictionsBufferName{}).read()});
                  }
                  return EmptyValue();
                });
            return true;
          }};
}

void Prompt(PromptOptions options) {
  CHECK(options.handler != nullptr);
  EditorState& editor_state = options.editor_state;
  NonNull<std::unique_ptr<InputReceiver>> delay_input_receiver =
      MakeNonNullUnique<DelayInputReceiver>(
          std::invoke([insertion = editor_state.modifiers().insertion] {
            switch (insertion) {
              case Modifiers::ModifyMode::kShift:
                return EditorMode::CursorMode::kInserting;
              case Modifiers::ModifyMode::kOverwrite:
                return EditorMode::CursorMode::kOverwriting;
            }
            LOG(FATAL) << "Invalid insertion mode.";
            return EditorMode::CursorMode::kDefault;
          }));

  editor_state.set_keyboard_redirect(
      options.editor_state.gc_pool().NewRoot(std::move(delay_input_receiver)));

  HistoryFile history_file = options.history_file;
  GetHistoryBuffer(editor_state, history_file)
      .Transform([options = std::move(options)](gc::Root<OpenBuffer> history) {
        history.ptr()->set_current_position_line(
            LineNumber(0) + history.ptr()->contents().size());

        futures::Value<gc::Root<OpenBuffer>> prompt_buffer_future =
            GetPromptBuffer(options.editor_state, options.prompt_contents_type,
                            options.initial_value);
        return std::move(prompt_buffer_future)
            .Transform([options = std::move(options),
                        history = std::move(history)](
                           gc::Root<OpenBuffer> prompt_buffer) {
              auto prompt_state =
                  PromptState::New(options, history, std::move(prompt_buffer));
              prompt_state->status().set_prompt(options.prompt,
                                                prompt_state->prompt_buffer());
              EnterInsertMode(prompt_state->insert_mode_options());

              prompt_state->OnModify();
              return futures::Past(EmptyValue());
            });
      });
}

gc::Root<Command> NewLinePromptCommand(
    EditorState& editor_state, std::wstring description,
    std::function<PromptOptions()> options_supplier) {
  return LinePromptCommand::New(editor_state, std::move(description),
                                std::move(options_supplier));
}

}  // namespace afc::editor

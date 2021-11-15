#include "src/set_buffer_mode.h"

#include "src/char_buffer.h"
#include "src/search_handler.h"
#include "src/tokenize.h"

namespace afc::editor {
namespace {
struct Operation {
  enum class Type {
    kForward,
    kBackward,
    // kPrevious and kNext move in the list of buffers according to their access
    // time (per OpenBuffer::last_visit).
    kPrevious,
    kNext,
    kNumber,
    kFilter,
    // Toggle WarningFilter: Only select buffers that have a warning status.
    kWarningFilter,
    // Toggle Search filter: Only select buffers that match a given regular
    // expression.
    kSearch,
  };
  Type type;
  size_t number = 0;
  std::wstring text_input = L"";
};

struct Data {
  enum class State { kDefault, kReadingFilter, kReadingSearch };
  // If state is kReadingFilter, the back of operations must be of type kFilter.
  // If state is kReadingSearch, the back of operations must be of type kSearch.
  State state = State::kDefault;

  std::vector<Operation> operations;
  std::optional<size_t> initial_number;
};

bool CharConsumer(wint_t c, Data* data) {
  CHECK(data->state != Data::State::kReadingFilter ||
        (!data->operations.empty() &&
         data->operations.back().type == Operation::Type::kFilter));
  CHECK(data->state != Data::State::kReadingSearch ||
        (!data->operations.empty() &&
         data->operations.back().type == Operation::Type::kSearch));
  switch (data->state) {
    case Data::State::kDefault:
      // TODO: Get rid of this cast, ugh.
      switch (static_cast<int>(c)) {
        case L'!':
          data->operations.push_back({Operation::Type::kWarningFilter});
          return true;

        case L'l':
          data->operations.push_back({Operation::Type::kForward});
          return true;

        case L'h':
          data->operations.push_back({Operation::Type::kBackward});
          return true;

        case L'j':
          data->operations.push_back({Operation::Type::kNext});
          return true;

        case L'k':
          data->operations.push_back({Operation::Type::kPrevious});
          return true;

        case L'0':
        case L'1':
        case L'2':
        case L'3':
        case L'4':
        case L'5':
        case L'6':
        case L'7':
        case L'8':
        case L'9':
          if (data->operations.empty() ||
              data->operations.back().type != Operation::Type::kNumber) {
            if (c == L'0') {
              return true;
            }
            data->operations.push_back({Operation::Type::kNumber});
          }
          data->operations.back().number *= 10;
          data->operations.back().number += c - L'0';
          return true;

        case L'w':
          data->state = Data::State::kReadingFilter;
          data->operations.push_back({Operation::Type::kFilter});
          return true;

        case L'/':
          data->state = Data::State::kReadingSearch;
          data->operations.push_back({Operation::Type::kSearch});
          return true;

        default:
          return false;
      }

    case Data::State::kReadingFilter:
    case Data::State::kReadingSearch:
      switch (static_cast<int>(c)) {
        case L'\n':
          // TODO: If text_input is empty, just pop it.
          data->state = Data::State::kDefault;
          return true;
        case Terminal::ESCAPE:
          return false;
        default:
          data->operations.back().text_input.push_back(c);
          return true;
      }
  }
  LOG(FATAL) << "Invalid state!";
  return false;
}

std::wstring BuildStatus(const Data& data) {
  static constexpr auto initial_value = L"set-buffer";
  std::wstring output = initial_value;
  for (size_t i = 0; i < data.operations.size(); ++i) {
    const auto& operation = data.operations[i];
    output += L" ";
    switch (operation.type) {
      case Operation::Type::kForward:
        output += L"⮞";
        break;
      case Operation::Type::kBackward:
        output += L"⮜";
        break;
      case Operation::Type::kPrevious:
        output += L"⮝";
        break;
      case Operation::Type::kNext:
        output += L"⮟";
        break;
      case Operation::Type::kNumber:
        output += std::to_wstring(operation.number);
        break;
      case Operation::Type::kFilter:
        output += L" w:" + operation.text_input;
        if (i == data.operations.size() - 1 &&
            data.state == Data::State::kReadingFilter) {
          output += L"…";
        }
        break;
      case Operation::Type::kWarningFilter:
        output += L" !";
        break;
      case Operation::Type::kSearch:
        output += L" /:" + operation.text_input;
        if (i == data.operations.size() - 1 &&
            data.state == Data::State::kReadingSearch) {
          output += L"…";
        }
        break;
    }
  }
  return output;
}

futures::Value<EmptyValue> Apply(EditorState* editor,
                                 CommandArgumentModeApplyMode mode, Data data) {
  // Each entry is an index (e.g., for BuffersList::GetBuffer) for an
  // available buffer.
  using Indices = std::vector<size_t>;
  auto buffers_list = editor->buffer_tree();

  Indices initial_indices(buffers_list->BuffersCount());
  for (size_t i = 0; i < initial_indices.size(); ++i) {
    initial_indices[i] = i;
  }

  bool warning_filter_enabled = false;
  for (const auto& operation : data.operations) {
    if (operation.type == Operation::Type::kWarningFilter) {
      warning_filter_enabled = !warning_filter_enabled;
    }
  }

  if (warning_filter_enabled) {
    Indices new_indices;
    for (auto& index : initial_indices) {
      if (auto buffer = buffers_list->GetBuffer(index).get();
          buffer->status()->GetType() == Status::Type::kWarning) {
        new_indices.push_back(index);
      }
    }
    if (new_indices.empty()) return futures::Past(EmptyValue());
    initial_indices = std::move(new_indices);
  }

  struct State {
    size_t index;  // This is an index into `indices`.
    Indices indices = {};
    std::optional<std::wstring> pattern_error = std::nullopt;
  };
  futures::Value<State> state = futures::Past(State{
      .index = data.initial_number.value_or(buffers_list->GetCurrentIndex()) %
               initial_indices.size(),

      .indices = std::move(initial_indices)});
  for (const auto& operation : data.operations) {
    switch (operation.type) {
      case Operation::Type::kForward:
        state = state.Transform([](State state) {
          if (state.indices.empty()) {
            state.index = 0;
          } else {
            state.index = (state.index + 1) % state.indices.size();
          }
          return state;
        });
        break;

      case Operation::Type::kBackward:
        state = state.Transform([](State state) {
          if (state.indices.empty()) {
            state.index = 0;
          } else if (state.index == 0) {
            state.index = state.indices.size() - 1;
          } else {
            state.index--;
          }
          return state;
        });
        break;

      case Operation::Type::kPrevious:
      case Operation::Type::kNext:
        state = state.Transform([buffers_list,
                                 op_type = operation.type](State state) {
          if (state.indices.empty()) {
            state.index = 0;
            return state;
          }
          CHECK_LT(state.index, state.indices.size());
          auto last_visit_current_buffer =
              buffers_list->GetBuffer(state.indices[state.index])->last_visit();
          std::optional<size_t> new_index;
          for (size_t i = 0; i < state.indices.size(); i++) {
            auto candidate = buffers_list->GetBuffer(state.indices[i]);
            std::optional<struct timespec> last_visit_new_index;
            if (new_index.has_value()) {
              last_visit_new_index =
                  buffers_list->GetBuffer(new_index.value())->last_visit();
            }
            if (op_type == Operation::Type::kPrevious
                    ? (candidate->last_visit() < last_visit_current_buffer &&
                       (last_visit_new_index == std::nullopt ||
                        candidate->last_visit() > last_visit_new_index))
                    : (candidate->last_visit() > last_visit_current_buffer &&
                       (last_visit_new_index == std::nullopt ||
                        candidate->last_visit() <= last_visit_new_index))) {
              new_index = i;
            }
          }
          if (new_index.has_value()) {
            state.index = new_index.value();
          }
          return state;
        });
        break;

      case Operation::Type::kNumber: {
        CHECK_GT(operation.number, 0ul);
        state = state.Transform(
            [number_requested = (operation.number - 1) %
                                buffers_list->BuffersCount()](State state) {
              auto it = std::find_if(state.indices.begin(), state.indices.end(),
                                     [number_requested](size_t index) {
                                       return index >= number_requested;
                                     });
              state.index = it == state.indices.end()
                                ? 0
                                : std::distance(state.indices.begin(), it);
              return state;
            });
      } break;

      case Operation::Type::kFilter:
        state = state.Transform(
            [filter = TokenizeBySpaces(*NewLazyString(operation.text_input)),
             buffers_list](State state) {
              Indices new_indices;
              for (auto& index : state.indices) {
                if (auto buffer = buffers_list->GetBuffer(index).get();
                    buffer != nullptr) {
                  if (std::shared_ptr<LazyString> str =
                          NewLazyString(buffer->Read(buffer_variables::name));
                      FindFilterPositions(
                          filter, ExtendTokensToEndOfString(
                                      str, TokenizeNameForPrefixSearches(str)))
                          .has_value()) {
                    new_indices.push_back(index);
                  }
                }
              }
              state.indices = std::move(new_indices);
              return state;
            });
        break;

      case Operation::Type::kWarningFilter:
        break;  // Already handled.

      case Operation::Type::kSearch: {
        state = state.Transform([editor, buffers_list,
                                 text_input =
                                     operation.text_input](State state) {
          // TODO: Maybe tweak the parameters to allow more than just one to
          // run at a given time? Would require changes to async_processor.h
          // (I think).
          auto evaluator = std::make_shared<AsyncSearchProcessor>(
              editor->work_queue(),
              BackgroundCallbackRunner::Options::QueueBehavior::kWait);
          auto progress_channel = std::make_shared<ProgressChannel>(
              editor->work_queue(), [](ProgressInformation) {},
              WorkQueueChannelConsumeMode::kLastAvailable);
          auto new_state = std::make_shared<State>(
              State{.index = state.index, .indices = {}});
          std::vector<futures::Value<futures::IterationControlCommand>>
              search_futures;
          for (auto& index : state.indices) {
            if (auto buffer = buffers_list->GetBuffer(index).get();
                buffer != nullptr) {
              // TODO: Pass SearchOptions::abort_notification to allow
              // aborting as the user continues to type?
              search_futures.push_back(
                  evaluator
                      ->Search(
                          {.search_query = text_input, .required_positions = 1},
                          *buffer, progress_channel)
                      .Transform(
                          [new_state,
                           index](AsyncSearchProcessor::Output search_output) {
                            if (search_output.pattern_error.has_value()) {
                              new_state->pattern_error = std::move(
                                  search_output.pattern_error.value());
                              return futures::IterationControlCommand::kStop;
                            }
                            if (search_output.matches > 0) {
                              new_state->indices.push_back(index);
                            }
                            return futures::IterationControlCommand::kContinue;
                          }));
            }
          }
          return futures::ForEachWithCopy(
                     search_futures.begin(), search_futures.end(),
                     [](futures::Value<futures::IterationControlCommand>
                            output) { return output; })
              .Transform([new_state](futures::IterationControlCommand) {
                return std::move(*new_state);
              });
        });
      } break;
    }
  }

  return state.Transform([editor, mode, buffers_list](State state) {
    if (state.pattern_error.has_value()) {
      // TODO: Find a better way to show it without hiding the input, ugh.
      editor->status()->SetWarningText(L"Pattern error: " +
                                       state.pattern_error.value());
      return EmptyValue();
    }
    if (state.indices.empty()) {
      return EmptyValue();
    }
    state.index %= state.indices.size();
    auto buffer = buffers_list->GetBuffer(state.indices[state.index]);
    editor->set_current_buffer(buffer, mode);
    switch (mode) {
      case CommandArgumentModeApplyMode::kFinal:
        editor->buffer_tree()->set_filter(std::nullopt);
        break;

      case CommandArgumentModeApplyMode::kPreview:
        if (state.indices.size() != buffers_list->BuffersCount()) {
          std::vector<std::weak_ptr<OpenBuffer>> filter;
          filter.reserve(state.indices.size());
          for (const auto& i : state.indices) {
            filter.push_back(buffers_list->GetBuffer(i));
          }
          editor->buffer_tree()->set_filter(std::move(filter));
        }
        break;
    }
    return EmptyValue();
  });
}

}  // namespace

std::unique_ptr<EditorMode> NewSetBufferMode(EditorState* editor) {
  auto buffers_list = editor->buffer_tree();
  Data initial_value;
  if (editor->modifiers().repetitions.has_value()) {
    editor->set_current_buffer(
        buffers_list->GetBuffer(
            (max(editor->modifiers().repetitions.value(), 1ul) - 1) %
            buffers_list->BuffersCount()),
        CommandArgumentModeApplyMode::kFinal);
    editor->ResetRepetitions();
    return nullptr;
  }
  CommandArgumentMode<Data>::Options options{
      .editor_state = editor,
      .initial_value = std::move(initial_value),
      .char_consumer = &CharConsumer,
      .status_factory = &BuildStatus,
      .undo =
          [editor, initial_buffer = editor->buffer_tree()->active_buffer()]() {
            editor->set_current_buffer(initial_buffer,
                                       CommandArgumentModeApplyMode::kFinal);
            editor->buffer_tree()->set_filter(std::nullopt);
            return futures::Past(EmptyValue());
          },
      .apply = [editor](CommandArgumentModeApplyMode mode,
                        Data data) { return Apply(editor, mode, data); }};

  return std::make_unique<CommandArgumentMode<Data>>(std::move(options));
}
}  // namespace afc::editor

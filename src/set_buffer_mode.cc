#include "src/set_buffer_mode.h"

#include "src/buffer_registry.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/container.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/search_handler.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::concurrent::ChannelLast;
using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineSequence;

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
  SingleLine text_input = SingleLine{};
};

struct Data {
  enum class State { kDefault, kReadingFilter, kReadingSearch };
  // If state is kReadingFilter, the back of operations must be of type kFilter.
  // If state is kReadingSearch, the back of operations must be of type kSearch.
  State state = State::kDefault;

  std::vector<Operation> operations;
  std::optional<size_t> initial_number;
};

bool CharConsumer(ExtendedChar c, Data& data) {
  CHECK(data.state != Data::State::kReadingFilter ||
        (!data.operations.empty() &&
         data.operations.back().type == Operation::Type::kFilter));
  CHECK(data.state != Data::State::kReadingSearch ||
        (!data.operations.empty() &&
         data.operations.back().type == Operation::Type::kSearch));
  switch (data.state) {
    case Data::State::kDefault:
      return std::visit(
          overload{
              [&](wchar_t regular_c) {
                switch (regular_c) {
                  case L'!':
                    data.operations.push_back(
                        {Operation::Type::kWarningFilter});
                    return true;

                  case L'l':
                    data.operations.push_back({Operation::Type::kForward});
                    return true;

                  case L'h':
                    data.operations.push_back({Operation::Type::kBackward});
                    return true;

                  case L'j':
                    data.operations.push_back({Operation::Type::kNext});
                    return true;

                  case L'k':
                    data.operations.push_back({Operation::Type::kPrevious});
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
                    if (data.operations.empty() ||
                        data.operations.back().type !=
                            Operation::Type::kNumber) {
                      if (regular_c == L'0') {
                        return true;
                      }
                      data.operations.push_back({Operation::Type::kNumber});
                    }
                    data.operations.back().number *= 10;
                    data.operations.back().number += regular_c - L'0';
                    return true;

                  case L'w':
                    data.state = Data::State::kReadingFilter;
                    data.operations.push_back({Operation::Type::kFilter});
                    return true;

                  case L'/':
                    data.state = Data::State::kReadingSearch;
                    data.operations.push_back({Operation::Type::kSearch});
                    return true;

                  default:
                    return false;
                }
              },
              [](ControlChar) { return false; }},
          c);

    case Data::State::kReadingFilter:
    case Data::State::kReadingSearch:
      if (c == ExtendedChar(ControlChar::kEscape))
        return false;
      else if (c == ExtendedChar(L'\n')) {
        data.state = Data::State::kDefault;
        CHECK(!data.operations.empty());
        CHECK(data.operations.back().type == Operation::Type::kFilter ||
              data.operations.back().type == Operation::Type::kSearch);
        if (data.operations.back().text_input.empty()) {
          data.operations.pop_back();
        }
        return true;
      } else if (wchar_t* regular_char = std::get_if<wchar_t>(&c);
                 regular_char != nullptr) {
        CHECK(*regular_char != L'\n');  // Handled above.
        data.operations.back().text_input =
            data.operations.back().text_input.Append(
                SingleLine{LazyString{ColumnNumberDelta{1}, *regular_char}});
        return true;
      } else {
        return false;
      }
  }
  LOG(FATAL) << "Invalid state!";
  return false;
}

Line BuildStatus(const Data& data) {
  LineBuilder output{SINGLE_LINE_CONSTANT(L"set-buffer")};
  // TODO(easy, 2023-12-30): Avoid explicit for loop, Concatenate. This is
  // complicated by the fact that some parts depend on `i`.
  for (size_t i = 0; i < data.operations.size(); ++i) {
    const auto& operation = data.operations[i];
    output.AppendString(SingleLine::Char<L' '>());
    switch (operation.type) {
      case Operation::Type::kForward:
        output.AppendString(SingleLine::Char<L'⮞'>());
        break;
      case Operation::Type::kBackward:
        output.AppendString(SingleLine::Char<L'⮜'>());
        break;
      case Operation::Type::kPrevious:
        output.AppendString(SingleLine::Char<L'⮝'>());
        break;
      case Operation::Type::kNext:
        output.AppendString(SingleLine::Char<L'⮟'>());
        break;
      case Operation::Type::kNumber:
        output.AppendString(
            SingleLine{LazyString{std::to_wstring(operation.number)}});
        break;
      case Operation::Type::kFilter:
        output.AppendString(SingleLine::Char<L'w'>(),
                            LineModifierSet{LineModifier::kCyan});
        output.AppendString(SingleLine::Char<L':'>(),
                            LineModifierSet{LineModifier::kDim});
        output.AppendString(operation.text_input);
        if (i == data.operations.size() - 1 &&
            data.state == Data::State::kReadingFilter) {
          output.AppendString(SingleLine::Char<L'…'>(),
                              LineModifierSet{LineModifier::kYellow});
        }
        break;
      case Operation::Type::kWarningFilter:
        output.AppendString(SINGLE_LINE_CONSTANT(L" !"),
                            LineModifierSet{LineModifier::kRed});
        break;
      case Operation::Type::kSearch:
        output.AppendString(SingleLine::Char<L'/'>(),
                            LineModifierSet{LineModifier::kGreen});
        output.AppendString(SingleLine::Char<L':'>(),
                            LineModifierSet{LineModifier::kDim});

        output.AppendString(operation.text_input);
        if (i == data.operations.size() - 1 &&
            data.state == Data::State::kReadingSearch) {
          output.AppendString(SingleLine::Char<L'…'>(),
                              LineModifierSet{LineModifier::kYellow});
        }
        break;
    }
  }
  return std::move(output).Build();
}

futures::Value<EmptyValue> Apply(EditorState& editor,
                                 CommandArgumentModeApplyMode mode, Data data) {
  // Each entry is an index (e.g., for BufferRegistry::GetListedBuffer) for an
  // available buffer.
  using Indices = std::vector<size_t>;

  Indices initial_indices(editor.buffer_registry().ListedBuffersCount());
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
      if (gc::Root<OpenBuffer> buffer =
              editor.buffer_registry().GetListedBuffer(index);
          buffer.ptr()->status().GetType() == Status::Type::kWarning) {
        new_indices.push_back(index);
      }
    }
    if (new_indices.empty()) return futures::Past(EmptyValue());
    initial_indices = std::move(new_indices);
  }

  struct State {
    size_t index;  // This is an index into `indices`.
    Indices indices = {};
    std::optional<Error> pattern_error = std::nullopt;
  };
  futures::Value<State> state_future =
      futures::Past(State{.index = data.initial_number.value_or(
                                       editor.buffer_tree().GetCurrentIndex()) %
                                   initial_indices.size(),
                          .indices = std::move(initial_indices)});
  for (const auto& operation : data.operations) {
    switch (operation.type) {
      case Operation::Type::kForward:
        state_future = std::move(state_future).Transform([](State state) {
          if (state.indices.empty()) {
            state.index = 0;
          } else {
            state.index = (state.index + 1) % state.indices.size();
          }
          return state;
        });
        break;

      case Operation::Type::kBackward:
        state_future = std::move(state_future).Transform([](State state) {
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
        state_future =
            std::move(state_future)
                .Transform([&editor, op_type = operation.type](State state) {
                  if (state.indices.empty()) {
                    state.index = 0;
                    return state;
                  }
                  CHECK_LT(state.index, state.indices.size());
                  auto last_visit_current_buffer =
                      editor.buffer_registry()
                          .GetListedBuffer(state.indices[state.index])
                          .ptr()
                          ->last_visit();
                  std::optional<size_t> new_index;
                  for (size_t i = 0; i < state.indices.size(); i++) {
                    OpenBuffer& candidate =
                        editor.buffer_registry()
                            .GetListedBuffer(state.indices[i])
                            .ptr()
                            .value();
                    std::optional<struct timespec> last_visit_new_index;
                    if (new_index.has_value()) {
                      last_visit_new_index =
                          editor.buffer_registry()
                              .GetListedBuffer(new_index.value())
                              .ptr()
                              ->last_visit();
                    }
                    if (op_type == Operation::Type::kPrevious
                            ? (candidate.last_visit() <
                                   last_visit_current_buffer &&
                               (last_visit_new_index == std::nullopt ||
                                candidate.last_visit() > last_visit_new_index))
                            : (candidate.last_visit() >
                                   last_visit_current_buffer &&
                               (last_visit_new_index == std::nullopt ||
                                candidate.last_visit() <=
                                    last_visit_new_index))) {
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
        state_future =
            std::move(state_future)
                .Transform([number_requested =
                                (operation.number - 1) %
                                editor.buffer_registry().ListedBuffersCount()](
                               State state) {
                  auto it =
                      std::find_if(state.indices.begin(), state.indices.end(),
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
        state_future =
            std::move(state_future)
                .Transform([filter = TokenizeBySpaces(operation.text_input),
                            &editor](State state) {
                  Indices new_indices;
                  for (auto& index : state.indices) {
                    gc::Root<OpenBuffer> buffer =
                        editor.buffer_registry().GetListedBuffer(index);
                    if (SingleLine name =
                            LineSequence::BreakLines(
                                buffer.ptr()->Read(buffer_variables::name))
                                .FoldLines();
                        FindFilterPositions(
                            filter,
                            ExtendTokensToEndOfString(
                                name, TokenizeNameForPrefixSearches(name)))
                            .has_value()) {
                      new_indices.push_back(index);
                    }
                  }
                  state.indices = std::move(new_indices);
                  return state;
                });
        break;

      case Operation::Type::kWarningFilter:
        break;  // Already handled.

      case Operation::Type::kSearch: {
        state_future =
            std::move(state_future)
                .Transform([&editor,
                            text_input = operation.text_input](State state) {
                  auto new_state = std::make_shared<State>(
                      State{.index = state.index, .indices = {}});
                  using Control = futures::IterationControlCommand;
                  std::vector<futures::Value<Control>> search_futures;
                  for (auto& index : state.indices) {
                    OpenBuffer& buffer = editor.buffer_registry()
                                             .GetListedBuffer(index)
                                             .ptr()
                                             .value();
                    // TODO: Pass SearchOptions::abort_notification to allow
                    // aborting as the user continues to type?
                    search_futures.push_back(
                        editor.thread_pool()
                            .Run(std::bind_front(
                                SearchHandler, Direction::kForwards,
                                SearchOptions{.search_query = text_input,
                                              .required_positions = 1,
                                              .case_sensitive = buffer.Read(
                                                  buffer_variables::
                                                      search_case_sensitive)},
                                buffer.contents().snapshot()))
                            .Transform([new_state, index](
                                           std::vector<LineColumn> results) {
                              if (results.size() > 0) {
                                new_state->indices.push_back(index);
                              }
                              return Success(Control::kContinue);
                            })
                            .ConsumeErrors([new_state](Error error) {
                              new_state->pattern_error = std::move(error);
                              return futures::Past(Control::kStop);
                            }));
                  }
                  return futures::ForEachWithCopy(
                             search_futures.begin(), search_futures.end(),
                             [](futures::Value<
                                 futures::IterationControlCommand>& output)
                                 -> futures::Value<
                                     futures::IterationControlCommand> {
                               return std::move(output);
                             })
                      .Transform([new_state](futures::IterationControlCommand) {
                        return std::move(*new_state);
                      });
                });
      } break;
    }
  }

  return std::move(state_future).Transform([&editor, mode](State state) {
    if (state.pattern_error.has_value()) {
      // TODO: Find a better way to show it without hiding the input, ugh.
      editor.status().Set(AugmentError(LazyString{L"Pattern error"},
                                       state.pattern_error.value()));
      return EmptyValue();
    }
    if (state.indices.empty()) {
      return EmptyValue();
    }
    state.index %= state.indices.size();
    gc::Root<OpenBuffer> buffer =
        editor.buffer_registry().GetListedBuffer(state.indices[state.index]);
    editor.set_current_buffer(buffer, mode);
    switch (mode) {
      case CommandArgumentModeApplyMode::kFinal:
        editor.buffer_tree().set_filter(std::nullopt);
        break;

      case CommandArgumentModeApplyMode::kPreview:
        if (state.indices.size() !=
            editor.buffer_registry().ListedBuffersCount())
          editor.buffer_tree().set_filter(container::MaterializeVector(
              state.indices | std::views::transform([&](size_t i) {
                return editor.buffer_registry()
                    .GetListedBuffer(i)
                    .ptr()
                    .ToWeakPtr();
              })));
        break;
    }
    return EmptyValue();
  });
}

}  // namespace

std::optional<gc::Root<InputReceiver>> NewSetBufferMode(EditorState& editor) {
  Data initial_value;
  if (editor.modifiers().repetitions.has_value()) {
    editor.set_current_buffer(
        editor.buffer_registry().GetListedBuffer(
            (std::max(editor.modifiers().repetitions.value(), 1ul) - 1) %
            editor.buffer_registry().ListedBuffersCount()),
        CommandArgumentModeApplyMode::kFinal);
    editor.ResetRepetitions();
    return std::nullopt;
  }

  initial_value.operations.push_back(
      Operation{.type = Operation::Type::kForward});
  return editor.gc_pool().NewRoot<InputReceiver>(
      MakeNonNullUnique<CommandArgumentMode<Data>>(
          CommandArgumentMode<Data>::Options{
              .editor_state = editor,
              .initial_value = std::move(initial_value),
              .char_consumer = &CharConsumer,
              .status_factory = &BuildStatus,
              .undo =
                  [&editor,
                   initial_buffer = editor.buffer_tree().active_buffer()]() {
                    VisitPointer(
                        initial_buffer,
                        [&editor](gc::Root<OpenBuffer> initial_buffer_root) {
                          editor.set_current_buffer(
                              initial_buffer_root,
                              CommandArgumentModeApplyMode::kFinal);
                        },
                        [] {});
                    editor.buffer_tree().set_filter(std::nullopt);
                    return futures::Past(EmptyValue());
                  },
              .apply = [&editor](
                           CommandArgumentModeApplyMode mode,
                           Data data) { return Apply(editor, mode, data); }}));
}
}  // namespace afc::editor

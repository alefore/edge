#include "src/search_command.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/futures/delete_notification.h"
#include "src/language/overload.h"
#include "src/line_prompt_mode.h"
#include "src/search_handler.h"
#include "src/tests/tests.h"
#include "src/transformation.h"

namespace afc::editor {
namespace {
using concurrent::VersionPropertyKey;
using futures::DeleteNotification;
using futures::IterationControlCommand;
using language::EmptyValue;
using language::Error;
using language::IgnoreErrors;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::Success;
using language::ValueOrError;
using language::lazy_string::ColumnNumber;
using language::lazy_string::LazyString;
using language::text::LineColumn;
using language::text::Range;

using ::operator<<;

namespace gc = language::gc;

void MergeInto(SearchResultsSummary current_results,
               ValueOrError<SearchResultsSummary>& final_results) {
  std::visit(
      overload{IgnoreErrors{},
               [&](SearchResultsSummary& output) {
                 output.matches += current_results.matches;
                 switch (current_results.search_completion) {
                   case SearchResultsSummary::SearchCompletion::kInterrupted:
                     output.search_completion =
                         SearchResultsSummary::SearchCompletion::kInterrupted;
                     break;
                   case SearchResultsSummary::SearchCompletion::kFull:
                     break;
                 }
               }},
      final_results);
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const ValueOrError<T>& a) {
  std::visit(overload{[&](const Error& error) { os << error; },
                      [&](const T& t) { os << t; }},
             a);
  return os;
}

const bool merge_into_tests_registration =
    tests::Register(L"SearchResultsSummary::MergeInto", [] {
      using S = SearchResultsSummary;
      auto test = [](std::wstring name, S current_results,
                     ValueOrError<S> input, ValueOrError<S> expected_output) {
        return tests::Test({.name = name, .callback = [=] {
                              ValueOrError<S> input_copy = input;
                              MergeInto(current_results, input_copy);
                              CHECK_EQ(input_copy, expected_output);
                            }});
      };
      const auto kInterrupted = S::SearchCompletion::kInterrupted;
      return std::vector(
          {test(L"BothEmpty", S{}, S{}, S{}),
           test(L"AddMatches", S{.matches = 5}, S{.matches = 7},
                S{.matches = 12}),
           test(L"CanHandleErrors", S{.matches = 12}, Error(L"Foo"),
                Error(L"Foo")),
           test(L"CurrentInterrupted",
                S{.matches = 5, .search_completion = kInterrupted},
                S{.matches = 7},
                S{.matches = 12, .search_completion = kInterrupted}),
           test(L"FinalInterrupted", S{.matches = 3},
                S{.matches = 5, .search_completion = kInterrupted},
                S{.matches = 8, .search_completion = kInterrupted}),
           test(L"BothInterrupted",
                S{.matches = 389, .search_completion = kInterrupted},
                S{.matches = 500, .search_completion = kInterrupted},
                S{.matches = 889, .search_completion = kInterrupted})});
    }());

void DoSearch(OpenBuffer& buffer, SearchOptions options) {
  ValueOrError<std::vector<LineColumn>> output =
      SearchHandler(buffer.editor(), options, buffer.contents());
  HandleSearchResults(output, buffer);
}

ColorizePromptOptions SearchResultsModifiers(
    NonNull<std::shared_ptr<LazyString>> line,
    ValueOrError<SearchResultsSummary> result_or_error) {
  LineModifierSet modifiers = std::visit(
      overload{[&](Error) { return LineModifierSet{LineModifier::kRed}; },
               [&](const SearchResultsSummary& result) -> LineModifierSet {
                 switch (result.matches) {
                   case 0:
                     return {};
                   case 1:
                     return {LineModifier::kCyan};
                   case 2:
                     return {LineModifier::kYellow};
                   default:
                     return {LineModifier::kGreen};
                 }
               }},
      result_or_error);

  return {.tokens = {{.token = {.value = L"",
                                .begin = ColumnNumber(0),
                                .end = ColumnNumber(0) + line->size()},
                      .modifiers = std::move(modifiers)}}};
}

// Wraps a progress channel and provides a builder to create "child" progress
// channels. Information added to the children gets aggregated before being
// propagated to the parent.
//
// This class isn't thread-safe.
class ProgressAggregator {
 public:
  ProgressAggregator(NonNull<std::unique_ptr<ProgressChannel>> parent_channel)
      : data_(MakeNonNullShared<Data>(std::move(parent_channel))) {}

  NonNull<std::unique_ptr<ProgressChannel>> NewChild() {
    NonNull<std::shared_ptr<ProgressInformation>> child_information;
    data_->children_created++;
    return MakeNonNullUnique<ProgressChannel>(
        data_->parent_channel->work_queue(),
        [data = data_, child_information](ProgressInformation information) {
          if (HasMatches(information) &&
              !HasMatches(child_information.value())) {
            data->buffers_with_matches++;
          }

          for (auto& [token, value] : information.counters) {
            auto& child_token_value = child_information->counters[token];
            data->aggregates.counters[token] -= child_token_value;
            child_token_value = value;
            data->aggregates.counters[token] += child_token_value;
          }

          for (auto& [token, value] : information.values) {
            data->aggregates.values[token] = value;
          }

          if (data->children_created > 1) {
            data->aggregates.values[VersionPropertyKey(L"buffers")] =
                std::to_wstring(data->buffers_with_matches) + L"/" +
                std::to_wstring(data->children_created);
          }

          data->parent_channel->Push(data->aggregates);
        },
        data_->parent_channel->consume_mode());
  }

 private:
  static bool HasMatches(const ProgressInformation& info) {
    auto it = info.counters.find(VersionPropertyKey(L"matches"));
    return it != info.counters.end() && it->second > 0;
  }

  struct Data {
    Data(NonNull<std::unique_ptr<ProgressChannel>> input_parent_channel)
        : parent_channel(std::move(input_parent_channel)) {}

    const NonNull<std::unique_ptr<ProgressChannel>> parent_channel;

    ProgressInformation aggregates;
    size_t buffers_with_matches = 0;
    size_t children_created = 0;
  };
  const NonNull<std::shared_ptr<Data>> data_;
};

class SearchCommand : public Command {
 public:
  SearchCommand(EditorState& editor_state) : editor_state_(editor_state) {}
  std::wstring Description() const override {
    return L"Searches for a string.";
  }
  std::wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t) override {
    if (GetStructureSearchQuery(editor_state_.structure()) ==
        StructureSearchQuery::kRegion) {
      editor_state_
          .ForEachActiveBuffer([&editor_state =
                                    editor_state_](OpenBuffer& buffer) {
            SearchOptions search_options;
            Range range = buffer.FindPartialRange(editor_state.modifiers(),
                                                  buffer.position());
            if (range.begin == range.end) {
              return futures::Past(EmptyValue());
            }
            VLOG(5) << "FindPartialRange: [position:" << buffer.position()
                    << "][range:" << range
                    << "][modifiers:" << editor_state.modifiers() << "]";
            CHECK_LT(range.begin, range.end);
            if (range.end.line > range.begin.line) {
              // This can happen when repetitions are used (to find multiple
              // words). We just cap it at the start/end of the line.
              if (editor_state.direction() == Direction::kBackwards) {
                range.begin = LineColumn(range.end.line);
              } else {
                range.end =
                    LineColumn(range.begin.line,
                               buffer.LineAt(range.begin.line)->EndColumn());
              }
            }
            CHECK_EQ(range.begin.line, range.end.line);
            if (range.begin == range.end) {
              return futures::Past(EmptyValue());
            }
            CHECK_LT(range.begin.column, range.end.column);
            buffer.set_position(range.begin);
            search_options.search_query =
                buffer.LineAt(range.begin.line)
                    ->Substring(range.begin.column,
                                range.end.column - range.begin.column)
                    ->ToString();
            search_options.starting_position = buffer.position();
            DoSearch(buffer, search_options);
            return futures::Past(EmptyValue());
          })
          .Transform([&editor_state = editor_state_](EmptyValue) {
            editor_state.ResetStructure();
            editor_state.ResetDirection();
            return EmptyValue();
          });
      return;
    }

    Prompt(
        {.editor_state = editor_state_,
         .prompt = L"🔎 ",
         .history_file = HistoryFile(L"search"),
         .colorize_options_provider =
             [&editor_state = editor_state_,
              buffers = std::make_shared<std::vector<gc::Root<OpenBuffer>>>(
                  editor_state_.active_buffers())](
                 const NonNull<std::shared_ptr<LazyString>>& line,
                 NonNull<std::unique_ptr<ProgressChannel>>
                     parent_progress_channel,
                 DeleteNotification::Value abort_value) {
               VLOG(5) << "Triggering async search.";
               auto results =
                   MakeNonNullShared<ValueOrError<SearchResultsSummary>>(
                       Success(SearchResultsSummary()));
               auto progress_aggregator = MakeNonNullShared<ProgressAggregator>(
                   std::move(parent_progress_channel));
               using Control = futures::IterationControlCommand;
               return futures::ForEach(
                          buffers,
                          [&editor_state, line, progress_aggregator,
                           abort_value,
                           results](const gc::Root<OpenBuffer>& buffer_root) {
                            OpenBuffer& buffer = buffer_root.ptr().value();
                            NonNull<std::shared_ptr<ProgressChannel>>
                                progress_channel =
                                    progress_aggregator->NewChild();
                            if (buffer.Read(
                                    buffer_variables::search_case_sensitive)) {
                              progress_channel->Push(
                                  {.values = {
                                       {VersionPropertyKey(L"case"), L"on"}}});
                            }
                            if (line->size().IsZero()) {
                              return futures::Past(Control::kContinue);
                            }
                            auto search_options = BuildPromptSearchOptions(
                                line, buffer, abort_value);
                            if (!search_options.has_value()) {
                              VLOG(6) << "search_options has no value.";
                              return futures::Past(Control::kContinue);
                            }
                            VLOG(5) << "Starting search in buffer: "
                                    << buffer.Read(buffer_variables::name);
                            return editor_state.thread_pool()
                                .Run(BackgroundSearchCallback(
                                    search_options.value(), buffer.contents(),
                                    progress_channel.value()))
                                .Transform(
                                    [results, abort_value, line, buffer_root,
                                     progress_channel](
                                        SearchResultsSummary current_results) {
                                      MergeInto(current_results,
                                                results.value());
                                      return abort_value.has_value()
                                                 ? Success(Control::kStop)
                                                 : Success(Control::kContinue);
                                    })
                                .ConsumeErrors([results](Error error) {
                                  results.value() = error;
                                  return futures::Past(Control::kStop);
                                });
                          })
                   .Transform([results, line](Control) {
                     VLOG(5) << "Drawing of search results.";
                     return SearchResultsModifiers(line,
                                                   std::move(results.value()));
                   });
             },
         .handler =
             [&editor_state =
                  editor_state_](NonNull<std::shared_ptr<LazyString>> input) {
               return editor_state
                   .ForEachActiveBuffer([input](OpenBuffer& buffer) {
                     if (auto search_options = BuildPromptSearchOptions(
                             input, buffer, DeleteNotification::Never());
                         search_options.has_value()) {
                       DoSearch(buffer, *search_options);
                     }
                     return futures::Past(EmptyValue());
                   })
                   .Transform([&editor_state](EmptyValue) {
                     editor_state.ResetDirection();
                     editor_state.ResetStructure();
                     return EmptyValue();
                   });
             },
         .predictor = SearchHandlerPredictor,
         .status = PromptOptions::Status::kBuffer});
  }

 private:
  static std::optional<SearchOptions> BuildPromptSearchOptions(
      NonNull<std::shared_ptr<LazyString>> input, OpenBuffer& buffer,
      DeleteNotification::Value abort_value) {
    auto& editor = buffer.editor();
    SearchOptions search_options;
    // TODO(easy, 2022-06-05): Avoid call to ToString.
    search_options.search_query = input->ToString();
    if (GetStructureSearchRange(editor.structure()) ==
        StructureSearchRange::kBuffer) {
      search_options.starting_position = buffer.position();
    } else {
      Range range =
          buffer.FindPartialRange(editor.modifiers(), buffer.position());
      if (range.begin == range.end) {
        buffer.status().SetInformationText(L"Unable to extract region.");
        return std::nullopt;
      }
      CHECK_LE(range.begin, range.end);
      if (editor.modifiers().direction == Direction::kBackwards) {
        search_options.starting_position = range.end;
        search_options.limit_position = range.begin;
      } else {
        search_options.starting_position = range.begin;
        search_options.limit_position = range.end;
      }
      LOG(INFO) << "Searching region: " << search_options.starting_position
                << " to " << search_options.limit_position.value();
    }
    search_options.abort_value = abort_value;
    search_options.case_sensitive =
        buffer.Read(buffer_variables::search_case_sensitive);
    return search_options;
  }

  EditorState& editor_state_;
};
}  // namespace

NonNull<std::unique_ptr<Command>> NewSearchCommand(EditorState& editor_state) {
  return MakeNonNullUnique<SearchCommand>(editor_state);
}

}  // namespace afc::editor

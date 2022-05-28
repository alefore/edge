#include "src/search_command.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/language/overload.h"
#include "src/line_prompt_mode.h"
#include "src/search_handler.h"
#include "src/transformation.h"

namespace afc::editor {
namespace {
using concurrent::Notification;
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

namespace gc = language::gc;

static void MergeInto(SearchResultsSummary current_results,
                      ValueOrError<SearchResultsSummary>& final_results) {
  std::visit(
      overload{IgnoreErrors{},
               [&](SearchResultsSummary output) {
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
      final_results.variant());
}

static void DoSearch(OpenBuffer& buffer, SearchOptions options) {
  ValueOrError<std::vector<LineColumn>> output =
      SearchHandler(buffer.editor(), options, buffer.contents());
  HandleSearchResults(output, buffer);
}

ColorizePromptOptions SearchResultsModifiers(
    NonNull<std::shared_ptr<LazyString>> line,
    ValueOrError<SearchResultsSummary> result) {
  LineModifierSet modifiers;
  std::visit(overload{[&](Error) { modifiers.insert(LineModifier::RED); },
                      [&](const SearchResultsSummary& result) {
                        switch (result.matches) {
                          case 0:
                            break;
                          case 1:
                            modifiers.insert(LineModifier::CYAN);
                            break;
                          case 2:
                            modifiers.insert(LineModifier::YELLOW);
                            break;
                          default:
                            modifiers.insert(LineModifier::GREEN);
                            break;
                        }
                      }},
             result.variant());

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
            data->aggregates
                .values[StatusPromptExtraInformationKey(L"buffers")] =
                std::to_wstring(data->buffers_with_matches) + L"/" +
                std::to_wstring(data->children_created);
          }

          data->parent_channel->Push(data->aggregates);
        },
        data_->parent_channel->consume_mode());
  }

 private:
  static bool HasMatches(const ProgressInformation& info) {
    auto it = info.counters.find(StatusPromptExtraInformationKey(L"matches"));
    return it != info.counters.end() && it->second > 0;
  }

  struct Data {
    Data(NonNull<std::unique_ptr<ProgressChannel>> parent_channel)
        : parent_channel(std::move(parent_channel)) {}

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

  void ProcessInput(wint_t) {
    if (editor_state_.structure()->search_query() ==
        Structure::SearchQuery::kRegion) {
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
                 NonNull<std::unique_ptr<ProgressChannel>> progress_channel,
                 NonNull<std::shared_ptr<Notification>> abort_notification) {
               VLOG(5) << "Triggering async search.";
               auto results =
                   MakeNonNullShared<ValueOrError<SearchResultsSummary>>(
                       Success(SearchResultsSummary()));
               auto progress_aggregator = MakeNonNullShared<ProgressAggregator>(
                   std::move(progress_channel));
               using Control = futures::IterationControlCommand;
               return futures::ForEach(
                          buffers,
                          [&editor_state, line, progress_aggregator,
                           abort_notification,
                           results](const gc::Root<OpenBuffer>& buffer_root) {
                            OpenBuffer& buffer = buffer_root.ptr().value();
                            NonNull<std::shared_ptr<ProgressChannel>>
                                progress_channel =
                                    progress_aggregator->NewChild();
                            if (buffer.Read(
                                    buffer_variables::search_case_sensitive)) {
                              progress_channel->Push(
                                  {.values = {{StatusPromptExtraInformationKey(
                                                   L"case"),
                                               L"on"}}});
                            }
                            if (line->size().IsZero()) {
                              return futures::Past(Control::kContinue);
                            }
                            auto search_options = BuildPromptSearchOptions(
                                line->ToString(), buffer, abort_notification);
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
                                .Transform([results, abort_notification, line,
                                            buffer_root, progress_channel](
                                               SearchResultsSummary
                                                   current_results) {
                                  MergeInto(current_results, results.value());
                                  return abort_notification->HasBeenNotified()
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
             [&editor_state = editor_state_](const std::wstring& input) {
               return editor_state
                   .ForEachActiveBuffer([input](OpenBuffer& buffer) {
                     if (auto search_options = BuildPromptSearchOptions(
                             input, buffer,
                             NonNull<std::shared_ptr<Notification>>());
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
      std::wstring input, OpenBuffer& buffer,
      NonNull<std::shared_ptr<Notification>> abort_notification) {
    auto& editor = buffer.editor();
    SearchOptions search_options;
    search_options.search_query = input;
    if (editor.structure()->search_range() == Structure::SearchRange::kBuffer) {
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
    search_options.abort_notification = abort_notification;
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

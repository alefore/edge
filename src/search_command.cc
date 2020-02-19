#include "src/search_command.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/line_prompt_mode.h"
#include "src/search_handler.h"
#include "src/transformation.h"

namespace afc::editor {
namespace {
using futures::IterationControlCommand;

static void MergeInto(AsyncSearchProcessor::Output current_results,
                      AsyncSearchProcessor::Output* final_results) {
  switch (current_results.results) {
    case AsyncSearchProcessor::Output::Results::kInvalidPattern:
    case AsyncSearchProcessor::Output::Results::kManyMatches:
      final_results->results = current_results.results;
      break;
    case AsyncSearchProcessor::Output::Results::kNoMatches:
      break;
    case AsyncSearchProcessor::Output::Results::kOneMatch:
      switch (final_results->results) {
        case AsyncSearchProcessor::Output::Results::kInvalidPattern:
        case AsyncSearchProcessor::Output::Results::kManyMatches:
          break;
        case AsyncSearchProcessor::Output::Results::kNoMatches:
          final_results->results =
              AsyncSearchProcessor::Output::Results::kOneMatch;
          break;
        case AsyncSearchProcessor::Output::Results::kOneMatch:
          final_results->results =
              AsyncSearchProcessor::Output::Results::kManyMatches;
          break;
      }
      break;
  }
}

static void DoSearch(OpenBuffer* buffer, SearchOptions options) {
  buffer->set_active_cursors(SearchHandler(buffer->editor(), options));
  buffer->ResetMode();
}

ColorizePromptOptions SearchResultsModifiers(
    std::shared_ptr<LazyString> line, AsyncSearchProcessor::Output results) {
  LineModifierSet modifiers;
  switch (results.results) {
    case AsyncSearchProcessor::Output::Results::kInvalidPattern:
      modifiers.insert(LineModifier::RED);
      break;
    case AsyncSearchProcessor::Output::Results::kNoMatches:
      break;
    case AsyncSearchProcessor::Output::Results::kOneMatch:
      modifiers.insert(LineModifier::CYAN);
      break;
    case AsyncSearchProcessor::Output::Results::kManyMatches:
      modifiers.insert(LineModifier::GREEN);
      break;
  }

  return {.tokens = {{{.value = L"",
                       .begin = ColumnNumber(0),
                       .end = ColumnNumber(0) + line->size()},
                      .modifiers = std::move(modifiers)}}};
}

class SearchCommand : public Command {
 public:
  wstring Description() const override { return L"Searches for a string."; }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (editor_state->structure()->search_query() ==
        Structure::SearchQuery::kRegion) {
      futures::Transform(
          editor_state->ForEachActiveBuffer(
              [editor_state](const std::shared_ptr<OpenBuffer>& buffer) {
                SearchOptions search_options;
                search_options.buffer = buffer.get();
                Range range = buffer->FindPartialRange(
                    editor_state->modifiers(), buffer->position());
                if (range.begin == range.end) {
                  return futures::Past(true);
                }
                VLOG(5) << "FindPartialRange: [position:" << buffer->position()
                        << "][range:" << range
                        << "][modifiers:" << editor_state->modifiers() << "]";
                CHECK_LT(range.begin, range.end);
                if (range.end.line > range.begin.line) {
                  // This can happen when repetitions are used (to find multiple
                  // words). We just cap it at the start/end of the line.
                  if (editor_state->direction() == Direction::kBackwards) {
                    range.begin = LineColumn(range.end.line);
                  } else {
                    range.end = LineColumn(
                        range.begin.line,
                        buffer->LineAt(range.begin.line)->EndColumn());
                  }
                }
                CHECK_EQ(range.begin.line, range.end.line);
                if (range.begin == range.end) {
                  return futures::Past(true);
                }
                CHECK_LT(range.begin.column, range.end.column);
                buffer->set_position(range.begin);
                search_options.search_query =
                    buffer->LineAt(range.begin.line)
                        ->Substring(range.begin.column,
                                    range.end.column - range.begin.column)
                        ->ToString();
                search_options.starting_position = buffer->position();
                search_options.case_sensitive =
                    buffer->Read(buffer_variables::search_case_sensitive);
                DoSearch(buffer.get(), search_options);
                return futures::Past(true);
              }),
          [editor_state](bool) {
            editor_state->ResetStructure();
            editor_state->ResetDirection();
            return true;
          });
      return;
    }

    PromptOptions options;
    options.editor_state = editor_state;
    options.prompt = L"🔎 ";
    options.history_file = L"search";
    options.handler = [](const wstring& input, EditorState* editor_state) {
      return futures::Transform(
          editor_state->ForEachActiveBuffer(
              [editor_state, input](const std::shared_ptr<OpenBuffer>& buffer) {
                auto search_options =
                    BuildPromptSearchOptions(input, buffer.get());
                if (search_options.has_value()) {
                  DoSearch(buffer.get(), search_options.value());
                }
                return futures::Past(true);
              }),
          [editor_state](bool) {
            editor_state->ResetDirection();
            editor_state->ResetStructure();
            return true;
          });
    };
    auto async_search_processor =
        std::make_shared<AsyncSearchProcessor>(editor_state->work_queue());

    options.change_handler = [editor_state, async_search_processor,
                              buffers = std::make_shared<
                                  std::vector<std::shared_ptr<OpenBuffer>>>(
                                  editor_state->active_buffers())](
                                 const std::shared_ptr<OpenBuffer>&
                                     prompt_buffer) {
      CHECK(prompt_buffer != nullptr);
      CHECK_EQ(prompt_buffer->lines_size(), LineNumberDelta(1));
      auto line = prompt_buffer->LineAt(LineNumber(0));
      if (line->empty()) {
        return futures::Past(true);
      }
      VLOG(5) << "Triggering async search.";
      auto results = std::make_shared<AsyncSearchProcessor::Output>();
      results->results = AsyncSearchProcessor::Output::Results::kNoMatches;
      return ColorizePrompt(
          prompt_buffer,
          futures::Transform(
              futures::ForEach(
                  buffers->begin(), buffers->end(),
                  [editor_state, async_search_processor, prompt_buffer, line,
                   results](const std::shared_ptr<OpenBuffer>& buffer) {
                    auto search_options = BuildPromptSearchOptions(
                        line->ToString(), buffer.get());
                    if (!search_options.has_value()) {
                      VLOG(6) << "search_options has no value.";
                      return futures::Past(
                          futures::IterationControlCommand::kContinue);
                    }
                    VLOG(5) << "Starting search in buffer: "
                            << buffer->Read(buffer_variables::name);
                    return futures::Transform(
                        async_search_processor->Search(
                            search_options.value(), buffer->contents()->copy()),
                        [prompt_buffer, results,
                         line](AsyncSearchProcessor::Output current_results) {
                          MergeInto(current_results, results.get());
                          return futures::IterationControlCommand::kContinue;
                        });
                  }),
              [results, buffers, line](futures::IterationControlCommand) {
                VLOG(5) << "Drawing of search results.";
                return SearchResultsModifiers(line->contents(),
                                              std::move(*results));
              }));
    };

    options.predictor = SearchHandlerPredictor;
    options.status = PromptOptions::Status::kBuffer;
    Prompt(std::move(options));
  }

 private:
  static std::optional<SearchOptions> BuildPromptSearchOptions(
      std::wstring input, OpenBuffer* buffer) {
    auto editor = buffer->editor();
    SearchOptions search_options;
    search_options.search_query = input;
    search_options.buffer = buffer;
    search_options.case_sensitive =
        buffer->Read(buffer_variables::search_case_sensitive);

    if (editor->structure()->search_range() ==
        Structure::SearchRange::kBuffer) {
      search_options.starting_position = buffer->position();
    } else {
      Range range =
          buffer->FindPartialRange(editor->modifiers(), buffer->position());
      if (range.begin == range.end) {
        buffer->status()->SetInformationText(L"Unable to extract region.");
        return std::nullopt;
      }
      CHECK_LE(range.begin, range.end);
      if (editor->modifiers().direction == Direction::kBackwards) {
        search_options.starting_position = range.end;
        search_options.limit_position = range.begin;
      } else {
        search_options.starting_position = range.begin;
        search_options.limit_position = range.end;
      }
      LOG(INFO) << "Searching region: " << search_options.starting_position
                << " to " << search_options.limit_position.value();
    }
    return search_options;
  }
};
}  // namespace

std::unique_ptr<Command> NewSearchCommand() {
  return std::make_unique<SearchCommand>();
}

}  // namespace afc::editor

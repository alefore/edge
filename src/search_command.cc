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
static void DoSearch(EditorState* editor_state, const SearchOptions& options) {
  auto buffer = editor_state->current_buffer();
  buffer->set_active_cursors(SearchHandler(editor_state, options));
  buffer->ResetMode();
  editor_state->ResetDirection();
  editor_state->ResetStructure();
}

futures::Value<bool> DrawSearchResults(
    OpenBuffer* buffer, const std::shared_ptr<const Line>& original_line,
    AsyncSearchProcessor::Output results) {
  CHECK(buffer != nullptr);
  CHECK_EQ(buffer->lines_size(), LineNumberDelta(1));
  auto line = buffer->LineAt(LineNumber(0));
  if (original_line->ToString() != line->ToString()) {
    LOG(INFO) << "Line has changed, ignoring call to `DrawSearchResults`.";
    return futures::Past(true);
  }

  VLOG(5) << "DrawSearchResults";
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

  Line::Options output;
  output.AppendString(line->contents(), std::move(modifiers));
  buffer->AppendRawLine(Line::New(std::move(output)));
  buffer->EraseLines(LineNumber(0), LineNumber(1));

  CHECK_EQ(buffer->lines_size(), LineNumberDelta(1));
  return futures::Past(true);
}

class SearchCommand : public Command {
 public:
  wstring Description() const override { return L"Searches for a string."; }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    SearchOptions search_options;
    search_options.buffer = buffer.get();

    search_options.case_sensitive =
        buffer->Read(buffer_variables::search_case_sensitive);

    if (editor_state->structure()->search_range() ==
        Structure::SearchRange::kBuffer) {
      search_options.starting_position = buffer->position();
    } else {
      Range range = buffer->FindPartialRange(editor_state->modifiers(),
                                             buffer->position());
      if (range.begin == range.end) {
        buffer->status()->SetInformationText(L"Unable to extract region.");
        return;
      }
      CHECK_LE(range.begin, range.end);
      if (editor_state->modifiers().direction == BACKWARDS) {
        search_options.starting_position = range.end;
        search_options.limit_position = range.begin;
      } else {
        search_options.starting_position = range.begin;
        search_options.limit_position = range.end;
      }
      editor_state->ResetStructure();
      LOG(INFO) << "Searching region: " << search_options.starting_position
                << " to " << search_options.limit_position.value();
    }

    if (editor_state->structure()->search_query() ==
        Structure::SearchQuery::kRegion) {
      Range range = buffer->FindPartialRange(editor_state->modifiers(),
                                             buffer->position());
      if (range.begin == range.end) {
        editor_state->ResetStructure();
        return;
      }
      VLOG(5) << "FindPartialRange: [position:" << buffer->position()
              << "][range:" << range
              << "][modifiers:" << editor_state->modifiers() << "]";
      editor_state->ResetStructure();
      CHECK_LT(range.begin, range.end);
      if (range.end.line > range.begin.line) {
        // This can happen when repetitions are used (to find multiple
        // words). We just cap it at the start/end of the line.
        if (editor_state->direction() == BACKWARDS) {
          range.begin = LineColumn(range.end.line);
        } else {
          range.end = LineColumn(range.begin.line,
                                 buffer->LineAt(range.begin.line)->EndColumn());
        }
      }
      CHECK_EQ(range.begin.line, range.end.line);
      if (range.begin == range.end) {
        editor_state->ResetStructure();
        return;
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
      DoSearch(editor_state, search_options);

      buffer->ResetMode();
      editor_state->ResetDirection();
      editor_state->ResetStructure();
      return;
    }

    PromptOptions options;
    options.editor_state = editor_state;
    options.prompt = L"🔎 ";
    options.history_file = L"search";
    options.handler = [search_options](const wstring& input,
                                       EditorState* editor_state) {
      SearchOptions options = search_options;
      options.search_query = input;
      DoSearch(editor_state, options);
    };
    auto async_search_processor =
        std::make_shared<AsyncSearchProcessor>(editor_state->work_queue());

    options.change_handler = [async_search_processor, search_options,
                              editor_state](
                                 const std::shared_ptr<OpenBuffer>& buffer) {
      CHECK(buffer != nullptr);
      CHECK_EQ(buffer->lines_size(), LineNumberDelta(1));
      auto line = buffer->LineAt(LineNumber(0));
      if (line->empty()) {
        return futures::Past(true);
      }
      VLOG(5) << "Triggering async search.";

      auto search_options_copy = search_options;
      search_options_copy.search_query = line->ToString();
      return futures::Transform(
          async_search_processor->Search(
              search_options_copy,
              editor_state->current_buffer()->contents()->copy()),
          [buffer, line](AsyncSearchProcessor::Output results) {
            VLOG(5) << "Drawing of search results.";
            return DrawSearchResults(buffer.get(), line, std::move(results));
          });
    };

    options.predictor = SearchHandlerPredictor;
    options.source_buffer = buffer;
    options.status = PromptOptions::Status::kBuffer;
    Prompt(std::move(options));
  }
};
}  // namespace

std::unique_ptr<Command> NewSearchCommand() {
  return std::make_unique<SearchCommand>();
}

}  // namespace afc::editor

#include "search_command.h"

#include "buffer.h"
#include "buffer_variables.h"
#include "command.h"
#include "editor.h"
#include "line_prompt_mode.h"
#include "search_handler.h"
#include "transformation.h"

namespace afc {
namespace editor {

namespace {
static void DoSearch(EditorState* editor_state, const SearchOptions& options) {
  auto buffer = editor_state->current_buffer()->second;
  buffer->set_active_cursors(SearchHandler(editor_state, options));
  buffer->ResetMode();
  editor_state->ResetDirection();
  editor_state->ResetStructure();
  editor_state->ScheduleRedraw();
}

class SearchCommand : public Command {
 public:
  wstring Description() const override { return L"Searches for a string."; }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    auto buffer = editor_state->current_buffer()->second;

    SearchOptions search_options;
    search_options.case_sensitive =
        buffer->Read(buffer_variables::search_case_sensitive());

    if (editor_state->structure()->search_range() ==
        Structure::SearchRange::kBuffer) {
      search_options.starting_position = buffer->position();
    } else {
      Range range = buffer->FindPartialRange(editor_state->modifiers(),
                                             buffer->position());
      if (range.begin == range.end) {
        editor_state->SetStatus(L"Unable to extract region.");
        return;
      }
      search_options.starting_position = range.begin;
      search_options.limit_position = range.end;
      CHECK_LE(search_options.starting_position, search_options.limit_position);
      editor_state->ResetStructure();
      if (editor_state->modifiers().direction == BACKWARDS) {
        LOG(INFO) << "Swaping positions (backwards search).";
        LineColumn tmp = search_options.starting_position;
        search_options.starting_position = search_options.limit_position;
        search_options.limit_position = tmp;
      }
      LOG(INFO) << "Searching region: " << search_options.starting_position
                << " to " << search_options.limit_position;
      search_options.has_limit_position = true;
    }

    if (editor_state->structure()->search_query() ==
        Structure::SearchQuery::kRegion) {
      auto buffer = editor_state->current_buffer()->second;
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
                                 buffer->LineAt(range.begin.line)->size());
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
          buffer->Read(buffer_variables::search_case_sensitive());
      DoSearch(editor_state, search_options);

      buffer->ResetMode();
      editor_state->ResetDirection();
      editor_state->ResetStructure();
      editor_state->ScheduleRedraw();
      return;
    }

    PromptOptions options;
    options.prompt = L"/";
    options.history_file = L"search";
    options.handler = [search_options](const wstring& input,
                                       EditorState* editor_state) {
      SearchOptions options = search_options;
      options.search_query = input;
      DoSearch(editor_state, options);
    };
    options.predictor = SearchHandlerPredictor;
    Prompt(editor_state, std::move(options));
  }
};
}  // namespace

std::unique_ptr<Command> NewSearchCommand() {
  return std::make_unique<SearchCommand>();
}

}  // namespace editor
}  // namespace afc

#include "search_command.h"

#include "buffer.h"
#include "command.h"
#include "editor.h"
#include "line_prompt_mode.h"
#include "search_handler.h"
#include "transformation.h"

namespace afc {
namespace editor {

namespace {
static void DoSearch(EditorState* editor_state,
                     const SearchOptions& options) {
  editor_state->current_buffer()
      ->second->set_active_cursors(SearchHandler(editor_state, options));
  editor_state->ResetMode();
  editor_state->ResetDirection();
  editor_state->ResetStructure();
  editor_state->ScheduleRedraw();
}

class SearchCommand : public Command {
 public:
  const wstring Description() {
    return L"Searches for a string.";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    switch (editor_state->structure()) {
      case WORD:
        {
          if (!editor_state->has_current_buffer()) { return; }
          auto buffer = editor_state->current_buffer()->second;
          LineColumn start, end;
          if (!buffer->FindPartialRange(
                   editor_state->modifiers(), buffer->position(), &start,
                   &end) || start == end) {
            editor_state->ResetStructure();
            return;
          }
          editor_state->ResetStructure();
          CHECK_LT(start, end);
          CHECK_EQ(start.line, end.line);
          CHECK_LT(start.column, end.column);
          buffer->set_position(start);
          {
            SearchOptions options;
            options.search_query =
                buffer->LineAt(start.line)
                    ->Substring(start.column, end.column - start.column)
                    ->ToString();
            options.starting_position = buffer->position();
            DoSearch(editor_state, options);
          }

          editor_state->ResetMode();
          editor_state->ResetDirection();
          editor_state->ResetStructure();
          editor_state->ScheduleRedraw();
        }
        break;

      default:
        auto position = editor_state->current_buffer()->second->position();
        PromptOptions options;
        options.prompt = L"/";
        options.history_file = L"search";
        options.handler = [position](const wstring& input,
                                     EditorState* editor_state) {
          SearchOptions search_options;
          search_options.search_query = input;
          search_options.starting_position = position;
          DoSearch(editor_state, search_options);
        };
        options.predictor = SearchHandlerPredictor;
        Prompt(editor_state, std::move(options));
        break;
    }
  }
};
}  // namespace

std::unique_ptr<Command> NewSearchCommand() {
  return std::unique_ptr<Command>(new SearchCommand());
}

}  // namespace afc
}  // namespace editor

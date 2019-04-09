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
    switch (editor_state->structure()) {
      case WORD:
      case SYMBOL: {
        if (!editor_state->has_current_buffer()) {
          return;
        }
        auto buffer = editor_state->current_buffer()->second;
        LineColumn start, end;
        if (!buffer->FindPartialRange(editor_state->modifiers(),
                                      buffer->position(), &start, &end) ||
            start == end) {
          editor_state->ResetStructure();
          return;
        }
        VLOG(5) << "FindPartialRange: [position:" << buffer->position()
                << "][start:" << start << "][end:" << end
                << "][modifiers:" << editor_state->modifiers() << "]";
        editor_state->ResetStructure();
        CHECK_LT(start, end);
        if (end.line > start.line) {
          // This can happen when repetitions are used (to find multiple
          // words). We just cap it at the start/end of the line.
          if (editor_state->direction() == BACKWARDS) {
            start.line = end.line;
            start.column = 0;
          } else {
            end.line = start.line;
            end.column = buffer->LineAt(start.line)->size();
          }
        }
        CHECK_EQ(start.line, end.line);
        if (start == end) {
          editor_state->ResetStructure();
          return;
        }
        CHECK_LT(start.column, end.column);
        buffer->set_position(start);
        {
          SearchOptions options;
          options.search_query =
              buffer->LineAt(start.line)
                  ->Substring(start.column, end.column - start.column)
                  ->ToString();
          options.starting_position = buffer->position();
          options.case_sensitive =
              buffer->Read(buffer_variables::search_case_sensitive());
          DoSearch(editor_state, options);
        }

        buffer->ResetMode();
        editor_state->ResetDirection();
        editor_state->ResetStructure();
        editor_state->ScheduleRedraw();
      } break;

      default:
        SearchOptions search_options;
        if (editor_state->current_buffer() != editor_state->buffers()->end()) {
          auto buffer = editor_state->current_buffer()->second;
          search_options.case_sensitive =
              buffer->Read(buffer_variables::search_case_sensitive());
          if (editor_state->structure() == CURSOR) {
            if (!buffer->FindPartialRange(editor_state->modifiers(),
                                          buffer->position(),
                                          &search_options.starting_position,
                                          &search_options.limit_position) ||
                search_options.starting_position ==
                    search_options.limit_position) {
              editor_state->SetStatus(L"Unable to extract region.");
              return;
            }
            CHECK_LE(search_options.starting_position,
                     search_options.limit_position);
            editor_state->ResetStructure();
            if (editor_state->modifiers().direction == BACKWARDS) {
              LOG(INFO) << "Swaping positions (backwards search).";
              LineColumn tmp = search_options.starting_position;
              search_options.starting_position = search_options.limit_position;
              search_options.limit_position = tmp;
            }
            LOG(INFO) << "Searching region: "
                      << search_options.starting_position << " to "
                      << search_options.limit_position;
            search_options.has_limit_position = true;
          } else {
            CHECK(editor_state->current_buffer() !=
                  editor_state->buffers()->end());
            CHECK(editor_state->current_buffer()->second != nullptr);
            search_options.starting_position =
                editor_state->current_buffer()->second->position();
          }
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
        break;
    }
  }
};
}  // namespace

std::unique_ptr<Command> NewSearchCommand() {
  return std::make_unique<SearchCommand>();
}

}  // namespace editor
}  // namespace afc

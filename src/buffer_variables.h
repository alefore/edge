#ifndef __AFC_EDITOR_BUFFER_VARIABLES_H__
#define __AFC_EDITOR_BUFFER_VARIABLES_H__

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/text/line_column.h"
#include "src/variables.h"
#include "src/vm/value.h"

namespace afc {
namespace editor {
namespace buffer_variables {

EdgeStruct<bool>* BoolStruct();
extern EdgeVariable<bool>* const pts;
extern EdgeVariable<bool>* const vm_exec;
extern EdgeVariable<bool>* const buffers_list_preview_follows_cursor;
extern EdgeVariable<bool>* const close_after_clean_exit;
extern EdgeVariable<bool>* const allow_dirty_delete;
extern EdgeVariable<bool>* const reload_after_exit;
extern EdgeVariable<bool>* const default_reload_after_exit;
extern EdgeVariable<bool>* const reload_on_enter;
extern EdgeVariable<bool>* const atomic_lines;
extern EdgeVariable<bool>* const term_on_close;
extern EdgeVariable<bool>* const save_on_close;
extern EdgeVariable<bool>* const clear_on_reload;
extern EdgeVariable<bool>* const paste_mode;
extern EdgeVariable<bool>* const follow_end_of_file;
extern EdgeVariable<bool>* const commands_background_mode;
extern EdgeVariable<bool>* const reload_on_buffer_write;
extern EdgeVariable<bool>* const trigger_reload_on_buffer_write;
extern EdgeVariable<bool>* const contains_line_marks;
extern EdgeVariable<bool>* const multiple_cursors;
extern EdgeVariable<bool>* const reload_on_display;
extern EdgeVariable<bool>* const show_in_buffers_list;
extern EdgeVariable<bool>* const push_positions_to_history;
extern EdgeVariable<bool>* const delete_into_paste_buffer;
extern EdgeVariable<bool>* const scrollbar;
extern EdgeVariable<bool>* const search_case_sensitive;
extern EdgeVariable<bool>* const search_filter_buffer;
extern EdgeVariable<bool>* const wrap_from_content;
extern EdgeVariable<bool>* const extend_lines;
extern EdgeVariable<bool>* const display_progress;
extern EdgeVariable<bool>* const persist_state;
extern EdgeVariable<bool>* const pin;
extern EdgeVariable<bool>* const vm_lines_evaluation;
extern EdgeVariable<bool>* const view_center_lines;
extern EdgeVariable<bool>* const flow_mode;

EdgeStruct<language::lazy_string::LazyString>* StringStruct();
extern EdgeVariable<language::lazy_string::LazyString>* const name;
extern EdgeVariable<language::lazy_string::LazyString>* const symbol_characters;
extern EdgeVariable<language::lazy_string::LazyString>* const path_characters;
extern EdgeVariable<language::lazy_string::LazyString>* const path;
extern EdgeVariable<language::lazy_string::LazyString>* const pts_path;
extern EdgeVariable<language::lazy_string::LazyString>* const children_path;
extern EdgeVariable<language::lazy_string::LazyString>* const command;
extern EdgeVariable<language::lazy_string::LazyString>* const
    editor_commands_path;
extern EdgeVariable<language::lazy_string::LazyString>* const
    line_prefix_characters;
extern EdgeVariable<language::lazy_string::LazyString>* const
    paragraph_line_prefix_characters;
extern EdgeVariable<language::lazy_string::LazyString>* const
    line_suffix_superfluous_characters;
extern EdgeVariable<language::lazy_string::LazyString>* const dictionary;
extern EdgeVariable<language::lazy_string::LazyString>* const tree_parser;
extern EdgeVariable<language::lazy_string::LazyString>* const language_keywords;
extern EdgeVariable<language::lazy_string::LazyString>* const typos;
extern EdgeVariable<language::lazy_string::LazyString>* const directory_noise;
extern EdgeVariable<language::lazy_string::LazyString>* const contents_type;
extern EdgeVariable<language::lazy_string::LazyString>* const shell_command;
extern EdgeVariable<language::lazy_string::LazyString>* const
    cpp_prompt_namespaces;
extern EdgeVariable<language::lazy_string::LazyString>* const
    file_context_extensions;
extern EdgeVariable<language::lazy_string::LazyString>* const
    identifier_behavior;
extern EdgeVariable<language::lazy_string::LazyString>* const
    completion_model_paths;

EdgeStruct<int>* IntStruct();
extern EdgeVariable<int>* const line_width;
extern EdgeVariable<int>* const buffer_list_context_lines;
extern EdgeVariable<int>* const margin_lines;
extern EdgeVariable<int>* const margin_columns;
extern EdgeVariable<int>* const progress;
extern EdgeVariable<int>* const analyze_content_lines_limit;

EdgeStruct<double>* DoubleStruct();
extern EdgeVariable<double>* const margin_lines_ratio;
extern EdgeVariable<double>* const beep_frequency_success;
extern EdgeVariable<double>* const beep_frequency_failure;
extern EdgeVariable<double>* const close_after_idle_seconds;

EdgeStruct<language::text::LineColumn>* LineColumnStruct();
extern EdgeVariable<language::text::LineColumn>* const view_start;

// No variables currently, but we'll likely add some later.
EdgeStruct<std::unique_ptr<vm::Value>>* ValueStruct();

}  // namespace buffer_variables
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_VARIABLES_H__

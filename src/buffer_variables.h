#ifndef __AFC_EDITOR_BUFFER_VARIABLES_H__
#define __AFC_EDITOR_BUFFER_VARIABLES_H__

#include "src/variables.h"
#include "vm/public/value.h"

namespace afc {
namespace editor {
namespace buffer_variables {

EdgeStruct<bool>* BoolStruct();
extern EdgeVariable<bool>* const pts;
extern EdgeVariable<bool>* const vm_exec;
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

EdgeStruct<std::wstring>* StringStruct();
extern EdgeVariable<std::wstring>* const name;
extern EdgeVariable<std::wstring>* const symbol_characters;
extern EdgeVariable<std::wstring>* const path_characters;
extern EdgeVariable<std::wstring>* const path;
extern EdgeVariable<std::wstring>* const pts_path;
extern EdgeVariable<std::wstring>* const children_path;
extern EdgeVariable<std::wstring>* const command;
extern EdgeVariable<std::wstring>* const editor_commands_path;
extern EdgeVariable<std::wstring>* const line_prefix_characters;
extern EdgeVariable<std::wstring>* const paragraph_line_prefix_characters;
extern EdgeVariable<std::wstring>* const line_suffix_superfluous_characters;
extern EdgeVariable<std::wstring>* const dictionary;
extern EdgeVariable<std::wstring>* const tree_parser;
extern EdgeVariable<std::wstring>* const language_keywords;
extern EdgeVariable<std::wstring>* const typos;
extern EdgeVariable<std::wstring>* const directory_noise;
extern EdgeVariable<std::wstring>* const contents_type;
extern EdgeVariable<std::wstring>* const shell_command_help_filter;
extern EdgeVariable<std::wstring>* const cpp_prompt_namespaces;
extern EdgeVariable<std::wstring>* const file_context_extensions;
extern EdgeVariable<std::wstring>* const identifier_behavior;

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

EdgeStruct<LineColumn>* LineColumnStruct();
extern EdgeVariable<LineColumn>* const view_start;

// No variables currently, but we'll likely add some later.
EdgeStruct<std::unique_ptr<vm::Value>>* ValueStruct();

}  // namespace buffer_variables
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_VARIABLES_H__

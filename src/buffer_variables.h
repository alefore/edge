#ifndef __AFC_EDITOR_BUFFER_VARIABLES_H__
#define __AFC_EDITOR_BUFFER_VARIABLES_H__

#include "vm/public/value.h"

#include "variables.h"

namespace afc {
namespace editor {
namespace buffer_variables {

EdgeStruct<bool>* BoolStruct();
EdgeVariable<bool>* pts();
EdgeVariable<bool>* vm_exec();
EdgeVariable<bool>* close_after_clean_exit();
EdgeVariable<bool>* allow_dirty_delete();
EdgeVariable<bool>* reload_after_exit();
EdgeVariable<bool>* default_reload_after_exit();
EdgeVariable<bool>* reload_on_enter();
EdgeVariable<bool>* atomic_lines();
EdgeVariable<bool>* save_on_close();
EdgeVariable<bool>* clear_on_reload();
EdgeVariable<bool>* paste_mode();
EdgeVariable<bool>* follow_end_of_file();
EdgeVariable<bool>* commands_background_mode();
EdgeVariable<bool>* reload_on_buffer_write();
EdgeVariable<bool>* contains_line_marks();
EdgeVariable<bool>* multiple_cursors();
EdgeVariable<bool>* reload_on_display();
EdgeVariable<bool>* show_in_buffers_list();
EdgeVariable<bool>* push_positions_to_history();
EdgeVariable<bool>* delete_into_paste_buffer();
EdgeVariable<bool>* scrollbar();
EdgeVariable<bool>* search_case_sensitive();
EdgeVariable<bool>* wrap_long_lines();

EdgeStruct<wstring>* StringStruct();
EdgeVariable<wstring>* word_characters();
EdgeVariable<wstring>* path_characters();
EdgeVariable<wstring>* path();
EdgeVariable<wstring>* pts_path();
EdgeVariable<wstring>* command();
EdgeVariable<wstring>* editor_commands_path();
EdgeVariable<wstring>* line_prefix_characters();
EdgeVariable<wstring>* line_suffix_superfluous_characters();
EdgeVariable<wstring>* dictionary();
EdgeVariable<wstring>* tree_parser();
EdgeVariable<wstring>* language_keywords();
EdgeVariable<wstring>* typos();

EdgeStruct<int>* IntStruct();
EdgeVariable<int>* line_width();
EdgeVariable<int>* buffer_list_context_lines();
EdgeVariable<int>* margin_lines();
EdgeVariable<int>* margin_columns();
EdgeVariable<int>* view_start_line();
EdgeVariable<int>* view_start_column();
EdgeVariable<int>* progress();

EdgeStruct<double>* DoubleStruct();
EdgeVariable<double>* margin_lines_ratio();
EdgeVariable<double>* beep_frequency_success();
EdgeVariable<double>* beep_frequency_failure();

// No variables currently, but we'll likely add some later.
EdgeStruct<unique_ptr<vm::Value>>* ValueStruct();

}  // namespace buffer_variables
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_VARIABLES_H__

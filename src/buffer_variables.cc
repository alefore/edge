#include "buffer_variables.h"

namespace afc {
namespace editor {
namespace buffer_variables {

EdgeStruct<bool>* BoolStruct() {
  static EdgeStruct<bool>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<bool>();
    // Trigger registration of all fields.
    pts();
    vm_exec();
    close_after_clean_exit();
    allow_dirty_delete();
    reload_after_exit();
    default_reload_after_exit();
    reload_on_enter();
    atomic_lines();
    save_on_close();
    clear_on_reload();
    paste_mode();
    follow_end_of_file();
    commands_background_mode();
    reload_on_buffer_write();
    contains_line_marks();
    multiple_cursors();
    reload_on_display();
    show_in_buffers_list();
    push_positions_to_history();
    delete_into_paste_buffer();
    scrollbar();
    search_case_sensitive();
  }
  return output;
}

EdgeVariable<bool>* pts() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"pts",
      L"If a command is forked that writes to this buffer, should it be run "
      L"with its own pseudoterminal?",
      false);
  return variable;
}

EdgeVariable<bool>* vm_exec() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"vm_exec", L"If set, all input read into this buffer will be executed.",
      false);
  return variable;
}

EdgeVariable<bool>* close_after_clean_exit() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"close_after_clean_exit",
      L"If a command is forked that writes to this buffer, should the buffer "
      L"be "
      L"closed when the command exits with a successful status code?",
      false);
  return variable;
}

EdgeVariable<bool>* allow_dirty_delete() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"allow_dirty_delete",
      L"Allow this buffer to be deleted even if it's dirty (i.e. if it has "
      L"unsaved changes or an underlying process that's still running).",
      false);
  return variable;
}

EdgeVariable<bool>* reload_after_exit() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"reload_after_exit",
      L"If a forked command that writes to this buffer exits, should Edge "
      L"reload the buffer?",
      false);
  return variable;
}

EdgeVariable<bool>* default_reload_after_exit() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"default_reload_after_exit",
      L"If a forked command that writes to this buffer exits and "
      L"reload_after_exit is set, what should Edge set reload_after_exit just "
      L"after reloading the buffer?",
      false);
  return variable;
}

EdgeVariable<bool>* reload_on_enter() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"reload_on_enter",
      L"Should this buffer be reloaded automatically when visited?", false);
  return variable;
}

EdgeVariable<bool>* atomic_lines() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"atomic_lines",
      L"If true, lines can't be joined (e.g. you can't delete the last "
      L"character in a line unless the line is empty).  This is used by "
      L"certain "
      L"buffers that represent lists of things (each represented as a line), "
      L"for which this is a natural behavior.",
      false);
  return variable;
}

EdgeVariable<bool>* save_on_close() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"save_on_close",
      L"Should this buffer be saved automatically when it's closed?", false);
  return variable;
}

EdgeVariable<bool>* clear_on_reload() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"clear_on_reload",
      L"Should any previous contents be discarded when this buffer is "
      L"reloaded? "
      L"If false, previous contents will be preserved and new contents will be "
      L"appended at the end.",
      true);
  return variable;
}

EdgeVariable<bool>* paste_mode() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"paste_mode",
      L"When paste_mode is enabled in a buffer, it will be displayed in a way "
      L"that makes it possible to select (with a mouse) parts of it (that are "
      L"currently shown).  It will also allow you to paste text directly into "
      L"the buffer.",
      false);
  return variable;
}

EdgeVariable<bool>* follow_end_of_file() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"follow_end_of_file", L"Should the cursor stay at the end of the file?",
      false);
  return variable;
}

EdgeVariable<bool>* commands_background_mode() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"commands_background_mode",
      L"Should new commands forked from this buffer be started in background "
      L"mode?  If false, we will switch to them automatically.",
      false);
  return variable;
}

EdgeVariable<bool>* reload_on_buffer_write() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"reload_on_buffer_write",
      L"Should the current buffer (on which this variable is set) be reloaded "
      L"when any buffer is written?  This is useful mainly for command buffers "
      L"like 'make' or 'git diff'.",
      false);
  return variable;
}

EdgeVariable<bool>* contains_line_marks() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"contains_line_marks",
      L"If set to true, this buffer will be scanned for line marks.", false);
  return variable;
}

EdgeVariable<bool>* multiple_cursors() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"multiple_cursors",
      L"If set to true, operations in this buffer apply to all cursors defined "
      L"on it.",
      false);
  return variable;
}

EdgeVariable<bool>* reload_on_display() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"reload_on_display",
      L"If set to true, a buffer will always be reloaded before being "
      L"displayed.",
      false);
  return variable;
}

EdgeVariable<bool>* show_in_buffers_list() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"show_in_buffers_list",
      L"If set to true, includes this in the list of buffers.", true);
  return variable;
}

EdgeVariable<bool>* push_positions_to_history() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"push_positions_to_history",
      L"If set to true, movement in this buffer result in positions being "
      L"pushed to the history of positions.",
      true);
  return variable;
}

EdgeVariable<bool>* delete_into_paste_buffer() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"delete_into_paste_buffer",
      L"If set to true, deletions from this buffer will go into the shared "
      L"paste buffer.",
      true);
  return variable;
}

EdgeVariable<bool>* scrollbar() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"scrollbar", L"If set to true, the scrollbar will be shown.", true);
  return variable;
}

EdgeVariable<bool>* search_case_sensitive() {
  static EdgeVariable<bool>* variable = BoolStruct()->AddVariable(
      L"search_case_sensitive",
      L"If set to true, search (through \"/\") is case sensitive.", false);
  return variable;
}

EdgeStruct<wstring>* StringStruct() {
  static EdgeStruct<wstring>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<wstring>;
    // Trigger registration of all fields.
    word_characters();
    path_characters();
    path();
    pts_path();
    command();
    editor_commands_path();
    line_prefix_characters();
    line_suffix_superfluous_characters();
    dictionary();
    tree_parser();
    language_keywords();
  }
  return output;
}

EdgeVariable<wstring>* word_characters() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"word_characters",
      L"String with all the characters that should be considered part of a "
      L"word.",
      L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_");
  return variable;
}

EdgeVariable<wstring>* path_characters() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"path_characters",
      L"String with all the characters that should be considered part of a "
      L"path.",
      L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.*:/");
  return variable;
}

EdgeVariable<wstring>* path() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"path", L"String with the path of the current file.", L"",
      FilePredictor);
  return variable;
}

EdgeVariable<wstring>* pts_path() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"pts_path",
      L"String with the path of the terminal used by the current buffer (or "
      L"empty if the user is not using a terminal).",
      L"", FilePredictor);
  return variable;
}

EdgeVariable<wstring>* command() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"command",
      L"String with the current command. Empty if the buffer is not a "
      L"sub-process (e.g. a regular file).",
      L"", FilePredictor);
  return variable;
}

EdgeVariable<wstring>* editor_commands_path() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"editor_commands_path",
      L"String with the path to the initial directory for editor commands.",
      L"", FilePredictor);
  return variable;
}

EdgeVariable<wstring>* line_prefix_characters() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"line_prefix_characters",
      L"String with all the characters that should be considered the prefix of "
      L"the actual contents of a line.  When a new line is created, the prefix "
      L"of the previous line (the sequence of all characters at the start of "
      L"the previous line that are listed in line_prefix_characters) is copied "
      L"to the new line.  The order of characters in line_prefix_characters "
      L"has "
      L"no effect.",
      L" ");
  return variable;
}

EdgeVariable<wstring>* line_suffix_superfluous_characters() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"line_suffix_superfluous_characters",
      L"String with all the characters that should be removed from the suffix "
      L"of a line (after editing it).  The order of characters in "
      L"line_suffix_superfluous_characters has no effect.",
      L" ");
  return variable;
}

EdgeVariable<wstring>* dictionary() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"dictionary",
      L"Path to a dictionary file used for autocompletion. If empty, pressing "
      L"TAB (in insert mode) just inserts a tab character into the file; "
      L"otherwise, it triggers completion to the first string from the "
      L"dictionary that matches the prefix of the current word. Pressing TAB "
      L"again iterates through all completions.",
      L"");
  return variable;
}

EdgeVariable<wstring>* tree_parser() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"tree_parser",
      L"Name of the parser to use to extract the tree structure from the "
      L"current file. Valid values are: \"text\" (normal text), and \"cpp\". "
      L"Any other value disables the tree logic.",
      L"");
  return variable;
}

EdgeVariable<wstring>* language_keywords() {
  static EdgeVariable<wstring>* variable = StringStruct()->AddVariable(
      L"language_keywords",
      L"Space separated list of keywords that should be highlighted by the "
      L"\"cpp\" tree parser (see variable tree_parser).",
      L"");
  return variable;
}

EdgeStruct<int>* IntStruct() {
  static EdgeStruct<int>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<int>;
    // Trigger registration of all fields.
    line_width();
    buffer_list_context_lines();
    margin_lines();
    margin_columns();
    view_start_line();
    view_start_column();
    progress();
  }
  return output;
}

EdgeVariable<int>* line_width() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"line_width", L"Desired maximum width of a line.", 80);
  return variable;
}

EdgeVariable<int>* buffer_list_context_lines() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"buffer_list_context_lines",
      L"Number of lines of context from this buffer to show in the list of "
      L"buffers.",
      0);
  return variable;
}

EdgeVariable<int>* margin_lines() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"margin_lines",
      L"Number of lines of context to display at the top/bottom of the current "
      L"position.",
      2);
  return variable;
}

EdgeVariable<int>* margin_columns() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"margin_columns",
      L"Number of characters of context to display at the left/right of the "
      L"current position.",
      2);
  return variable;
}

EdgeVariable<int>* view_start_line() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"view_start_line",
      L"The desired line to show at the beginning of the screen (at the "
      L"top-most position). This is adjusted automatically as the cursor moves "
      L"around in the buffer.",
      0);
  return variable;
}

EdgeVariable<int>* view_start_column() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"view_start_column",
      L"The desired column to show at the left-most part of the screen. This "
      L"is adjusted automatically as the cursor moves around in the buffer.",
      0);
  return variable;
}

EdgeVariable<int>* progress() {
  static EdgeVariable<int>* variable = IntStruct()->AddVariable(
      L"progress",
      L"Counter of the number of times this buffer has made progress. This is "
      L"defined somewhat ambiguously, but roughly consists of new information "
      L"being read into the buffer. This is used to display progress for the "
      L"buffer.",
      0);
  return variable;
}

EdgeStruct<double>* DoubleStruct() {
  static EdgeStruct<double>* output = nullptr;
  if (output == nullptr) {
    output = new EdgeStruct<double>;
    // Trigger registration of all fields.
    margin_lines_ratio();
    beep_frequency_success();
    beep_frequency_failure();
  }
  return output;
}

EdgeVariable<double>* margin_lines_ratio() {
  static EdgeVariable<double>* variable = DoubleStruct()->AddVariable(
      L"margin_lines_ratio",
      L"Ratio of the number of lines in the screen reserved to display context "
      L"around the current position in the current buffer at the top/bottom of "
      L"the screen. See also variable `margin_lines`.",
      0.07);
  return variable;
}

EdgeVariable<double>* beep_frequency_success() {
  static EdgeVariable<double>* variable = DoubleStruct()->AddVariable(
      L"beep_frequency_success",
      L"Frequency of the beep to play when a command buffer exits "
      L"successfully. If 0, disables the beep.",
      880.0);
  return variable;
}

EdgeVariable<double>* beep_frequency_failure() {
  static EdgeVariable<double>* variable = DoubleStruct()->AddVariable(
      L"beep_frequency_failure",
      L"Frequency of the beep to play when a command buffer exits with an "
      L"error. If 0, disables the beep.",
      440.0);
  return variable;
}

}  // namespace buffer_variables
}  // namespace editor
}  // namespace afc

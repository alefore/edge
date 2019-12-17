#include "src/buffer_variables.h"

namespace afc {
namespace editor {
namespace buffer_variables {

EdgeStruct<bool>* BoolStruct() {
  static EdgeStruct<bool>* output = new EdgeStruct<bool>();
  return output;
}

EdgeVariable<bool>* const pts = BoolStruct()->AddVariable(
    L"pts",
    L"If a command is forked that writes to this buffer, should it be run "
    L"with its own pseudoterminal?",
    false);

EdgeVariable<bool>* const vm_exec = BoolStruct()->AddVariable(
    L"vm_exec", L"If set, all input read into this buffer will be executed.",
    false);

EdgeVariable<bool>* const close_after_clean_exit = BoolStruct()->AddVariable(
    L"close_after_clean_exit",
    L"If a command is forked that writes to this buffer, should the buffer "
    L"be closed when the command exits with a successful status code?\n\n"
    L"This can be used to fork commands that you expect to succeed and where "
    L"you don't care for their output unless they fail.",
    false);

EdgeVariable<bool>* const allow_dirty_delete = BoolStruct()->AddVariable(
    L"allow_dirty_delete",
    L"Allow this buffer to be deleted even if it's dirty (i.e. if it has "
    L"unsaved changes or an underlying process that's still running).\n\n"
    L"This applies both if the buffer is closed explicitly or implicitly "
    L"when Edge exits.",
    false);

EdgeVariable<bool>* const reload_after_exit = BoolStruct()->AddVariable(
    L"reload_after_exit",
    L"If a forked command that writes to this buffer exits, should Edge "
    L"reload the buffer automatically?\n\n"
    L"When the buffer is reloaded, this variable is automatically set to the "
    L"value of `default_reload_after_exit`.",
    false);

EdgeVariable<bool>* const default_reload_after_exit = BoolStruct()->AddVariable(
    L"default_reload_after_exit",
    L"If a forked command that writes to this buffer exits and "
    L"reload_after_exit is set, what should Edge set reload_after_exit just "
    L"after reloading the buffer?",
    false);

EdgeVariable<bool>* const reload_on_enter = BoolStruct()->AddVariable(
    L"reload_on_enter",
    L"Should this buffer be reloaded automatically when visited?", false);

EdgeVariable<bool>* const atomic_lines = BoolStruct()->AddVariable(
    L"atomic_lines",
    L"If true, lines can't be joined (e.g. you can't delete the last "
    L"character in a line unless the line is empty). In this case, instead "
    L"of displaying the cursors, Edge will show the currently selected "
    L"line.\n\n"
    "This is used by certain buffers (such as the list of buffers or a view "
    L"of the contents of a directory) that represent lists of things (each "
    L"represented as a line), for which this is a natural behavior.",
    false);

EdgeVariable<bool>* const term_on_close = BoolStruct()->AddVariable(
    L"term_on_close",
    L"If this buffer has a child process, should Edge send a SIGTERM signal to "
    L"the child process when the buffer is closed?",
    false);

EdgeVariable<bool>* const save_on_close = BoolStruct()->AddVariable(
    L"save_on_close",
    L"Should this buffer be saved automatically when it's closed?\n\n"
    L"This applies both if the buffer is closed explicitly or implicitly "
    L"when Edge exits.",
    false);

EdgeVariable<bool>* const clear_on_reload = BoolStruct()->AddVariable(
    L"clear_on_reload",
    L"Should any previous contents be discarded when this buffer is "
    L"reloaded? "
    L"If false, previous contents will be preserved and new contents will be "
    L"appended at the end.\n\n"
    L"This is useful mainly for buffers with the output of commands, where "
    L"you don't want to discard the output of previous runs as you reload "
    L"the buffer.",
    true);

EdgeVariable<bool>* const paste_mode = BoolStruct()->AddVariable(
    L"paste_mode",
    L"When paste_mode is enabled in a buffer, it will be displayed in a way "
    L"that makes it possible to select (with a mouse) parts of it (that are "
    L"currently shown).  It will also allow you to paste text directly into "
    L"the buffer (i.e., it will disable any smart indenting).",
    false);

EdgeVariable<bool>* const follow_end_of_file = BoolStruct()->AddVariable(
    L"follow_end_of_file", L"Should the cursor stay at the end of the file?",
    false);

EdgeVariable<bool>* const commands_background_mode = BoolStruct()->AddVariable(
    L"commands_background_mode",
    L"Should new commands forked from this buffer be started in background "
    L"mode?  If false, we will switch to them automatically.\n\n"
    L"This just affects whether we switch the currently selected Edge buffer "
    L"to the new buffer; it has no effect whatsoever in the command.",
    false);

EdgeVariable<bool>* const reload_on_buffer_write = BoolStruct()->AddVariable(
    L"reload_on_buffer_write",
    L"Should the current buffer (on which this variable is set) be reloaded "
    L"when any buffer is written?\n\n"
    L"This is useful mainly for command buffers like `make` or `git "
    L"diff`.\n\n"
    L"If you set this, you may also want to set `contains_line_marks`.",
    false);

EdgeVariable<bool>* const trigger_reload_on_buffer_write =
    BoolStruct()->AddVariable(
        L"trigger_reload_on_buffer_write",
        L"Does a write of this buffer trigger a reload of other buffers that "
        L"have variable `reload_on_buffer_write` set? This is mainly useful to "
        L"ensure that *internal* buffers (such as prompt history) don't "
        L"trigger "
        L"reload of user-visible buffers (such as compilers) on quit.",
        true);

EdgeVariable<bool>* const contains_line_marks = BoolStruct()->AddVariable(
    L"contains_line_marks",
    L"Indicates whether the current buffer should be scanned for \"marks\": "
    L"lines that start with a prefix of the form \"path:line\" (e.g. "
    L"`src/test.cc:23`). For any such marks found, the corresponding lines "
    L"in the corresponding buffers (i.e., buffers for the corresponding "
    L"files) will be highlighted.\n\n"
    L"This is useful for *compiler* commands like `make` that output lines "
    L"with compilation errors.\n\n"
    L"Unfortunately, we don't currently support any fancy formats: the lines "
    L"need to start with the marks. This, however, is good enough for many "
    L"compilers. But if your commands output lines in a format such as "
    L"`Error in src/test.cc:23:` this won't be very useful.\n\n"
    L"If you set this on a buffer, you may want to also set variable "
    L"`reload_on_buffer_write`.",
    false);

EdgeVariable<bool>* const multiple_cursors = BoolStruct()->AddVariable(
    L"multiple_cursors",
    L"If `true`, all commands apply to all cursors in the current buffer. "
    L"Otherwise, they only apply to the active cursor.",
    false);

EdgeVariable<bool>* const reload_on_display = BoolStruct()->AddVariable(
    L"reload_on_display",
    L"If set to true, a buffer will always be reloaded before being "
    L"displayed.",
    false);

EdgeVariable<bool>* const show_in_buffers_list = BoolStruct()->AddVariable(
    L"show_in_buffers_list",
    L"If set to true, includes this in the list of buffers.", true);

EdgeVariable<bool>* const push_positions_to_history = BoolStruct()->AddVariable(
    L"push_positions_to_history",
    L"If set to true, movement in this buffer causes new positions to be "
    L"pushed to the history of positions.\n\n"
    L"A few buffers default this to `false`, to avoid pushing their "
    L"positions to the history.",
    true);

EdgeVariable<bool>* const delete_into_paste_buffer = BoolStruct()->AddVariable(
    L"delete_into_paste_buffer",
    L"If set to true, deletions from this buffer go into the shared paste "
    L"buffer.\n\n"
    L"A few buffers, such as prompt buffers, default this to `false`.",
    true);

EdgeVariable<bool>* const scrollbar = BoolStruct()->AddVariable(
    L"scrollbar", L"If set to true, the scrollbar will be shown.", true);

EdgeVariable<bool>* const search_case_sensitive = BoolStruct()->AddVariable(
    L"search_case_sensitive",
    L"If set to true, search (through \"/\") is case sensitive.", false);

EdgeVariable<bool>* const wrap_from_content = BoolStruct()->AddVariable(
    L"wrap_from_content",
    L"If true, lines will be wrapped (either at the end of the screen or after "
    L"`line_width` characters) based on spaces, avoiding breaking words when "
    L"feasible.",
    false);

EdgeVariable<bool>* const wrap_long_lines = BoolStruct()->AddVariable(
    L"wrap_long_lines",
    L"If set to true, long lines will be wrapped (only for displaying). "
    L"Otherwise, they get trimmed at the end.",
    true);

EdgeVariable<bool>* const extend_lines = BoolStruct()->AddVariable(
    L"extend_lines",
    L"If set to true, lines should be extended automatically as the cursor "
    L"advances past their end.",
    false);

EdgeVariable<bool>* const display_progress = BoolStruct()->AddVariable(
    L"display_progress",
    L"If set to true, if this buffer is reading input (either from a regular "
    L"file or a process), it'll be shown in the status line.",
    true);

EdgeVariable<bool>* const persist_state = BoolStruct()->AddVariable(
    L"persist_state",
    L"Should we aim to persist information for this buffer (in "
    L"$EDGE_PATH/state/)?",
    false);

EdgeStruct<wstring>* StringStruct() {
  static EdgeStruct<wstring>* output = new EdgeStruct<wstring>();
  return output;
}

EdgeVariable<wstring>* const name =
    StringStruct()->AddVariable(L"name", L"Name of the current buffer.", L"");

EdgeVariable<wstring>* const symbol_characters = StringStruct()->AddVariable(
    L"symbol_characters",
    L"String with all the characters that should be considered part of a "
    L"symbol. This affects commands such as `dW` (delete symbol).",
    L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_");

EdgeVariable<wstring>* const path_characters = StringStruct()->AddVariable(
    L"path_characters",
    L"String with all the characters that should be considered part of a "
    L"path.",
    L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.*:/");

EdgeVariable<wstring>* const path = StringStruct()->AddVariable(
    L"path", L"String with the path of the current file.", L"", FilePredictor);

EdgeVariable<wstring>* const pts_path = StringStruct()->AddVariable(
    L"pts_path",
    L"String with the path of the terminal used by the current buffer (or "
    L"empty if the user is not using a terminal).",
    L"", FilePredictor);

EdgeVariable<wstring>* const children_path = StringStruct()->AddVariable(
    L"children_path",
    L"If non-empty, string with the path of the directory used when forking "
    L"a new command from the current buffer. If empty, the new command will "
    L"inherit the current working directory that Edge was run in.",
    L"", FilePredictor);

EdgeVariable<wstring>* const command = StringStruct()->AddVariable(
    L"command",
    L"String with the current command. Empty if the buffer is not a "
    L"sub-process (e.g. a regular file).",
    L"", FilePredictor);

EdgeVariable<wstring>* const editor_commands_path = StringStruct()->AddVariable(
    L"editor_commands_path",
    L"String with the path to the initial directory used when prompting the "
    L"user for an editor command to run. It does not affect in any way the "
    L"execution of these commands (simply the prompting).",
    L"", FilePredictor);

EdgeVariable<wstring>* const line_prefix_characters =
    StringStruct()->AddVariable(
        L"line_prefix_characters",
        L"String with all the characters that should be considered the prefix "
        L"of "
        L"the actual contents of a line.  When a new line is created, the "
        L"prefix "
        L"of the previous line (the sequence of all characters at the start of "
        L"the previous line that are listed in line_prefix_characters) is "
        L"copied "
        L"to the new line.  The order of characters in line_prefix_characters "
        L"has no effect.",
        L" ");

EdgeVariable<wstring>* const paragraph_line_prefix_characters =
    StringStruct()->AddVariable(
        L"paragraph_line_prefix_characters",
        L"Similar to line_prefix_characters, but contains additional "
        L"characters "
        L"that are allowed in the prefix of the first line of a paragraph (but "
        L"wouldn't be allowed in continuation lines).",
        L" ");

EdgeVariable<wstring>* const line_suffix_superfluous_characters =
    StringStruct()->AddVariable(
        L"line_suffix_superfluous_characters",
        L"String with all the characters that should be removed from the "
        L"suffix "
        L"of a line (after editing it).  The order of characters in "
        L"line_suffix_superfluous_characters has no effect.",
        L" ");

EdgeVariable<wstring>* const dictionary = StringStruct()->AddVariable(
    L"dictionary",
    L"Path to a dictionary file used for autocompletion. If empty, pressing "
    L"TAB (in insert mode) just inserts a tab character into the file; "
    L"otherwise, it triggers completion to the first string from this file "
    L"that matches the prefix of the current word. Pressing TAB again "
    L"iterates through all completions.\n\n"
    L"The dictionary file must be a text file containing one word per line "
    L"and sorted alphabetically.",
    L"");

EdgeVariable<wstring>* const tree_parser = StringStruct()->AddVariable(
    L"tree_parser",
    L"Name of the parser to use to extract the tree structure from the "
    L"current file. Valid values are: \"text\" (normal text), and \"cpp\". "
    L"Any other value disables the tree logic.",
    L"");

EdgeVariable<wstring>* const language_keywords = StringStruct()->AddVariable(
    L"language_keywords",
    L"Space separated list of keywords that should be highlighted by the "
    L"\"cpp\" tree parser (see variable tree_parser).",
    L"");

EdgeVariable<wstring>* const typos = StringStruct()->AddVariable(
    L"typos",
    L"Space separated list of keywords that should be highlighted by the "
    L"tree parser as errors. This is only honored by a few tree parser types "
    L"(see variable tree_parser).",
    L"");

EdgeVariable<wstring>* const directory_noise = StringStruct()->AddVariable(
    L"directory_noise",
    L"Regular expression to use in a buffer showing the contents of a "
    L"directory to identify files that should be considered as noise: they "
    L"are less important than most files.",
    L".*(\\.o|~)|\\.(?!\\.$).*");

EdgeVariable<wstring>* const contents_type = StringStruct()->AddVariable(
    L"contents_type",
    L"String identifying the type of contents in the buffer. Known values are "
    L"`path` for buffers that contain paths and the empty string. This can be "
    L"used to customize certain behaviors.",
    L"");

EdgeVariable<wstring>* const shell_command_help_filter =
    StringStruct()->AddVariable(
        L"shell_command_help_filter",
        L"Regular expression that matches commands for which a help buffer "
        L"(based "
        L"on running the command with `--help`) should be shown.",
        L"^ *"
        L"(blaze|cat|date|edge|find|gcc|git|grep|ls|locate|make|python|rm|"
        L"sleep)"
        L"[^|;]*$");

EdgeStruct<int>* IntStruct() {
  static EdgeStruct<int>* output = new EdgeStruct<int>();
  return output;
}

EdgeVariable<int>* const line_width = IntStruct()->AddVariable(
    L"line_width",
    L"Desired maximum width of a line. The syntax information, scroll bar, and "
    L"other relevant information (when available) will be displayed after this "
    L"number of characters. Lines will also be wrapped (see variables "
    L"`wrap_from_content` and `wrap_long_lines`) based on this value. If set "
    L"to 1, the value will be taken from the size of the screen (i.e., use as "
    L"many columns as are currently available.",
    80);

EdgeVariable<int>* const buffer_list_context_lines = IntStruct()->AddVariable(
    L"buffer_list_context_lines",
    L"Number of lines of context from this buffer to show in the list of "
    L"buffers.",
    0);

EdgeVariable<int>* const margin_lines = IntStruct()->AddVariable(
    L"margin_lines",
    L"Number of lines of context to display at the top/bottom of the current "
    L"position.",
    2);

EdgeVariable<int>* const margin_columns = IntStruct()->AddVariable(
    L"margin_columns",
    L"Number of characters of context to display at the left/right of the "
    L"current position.",
    2);

EdgeVariable<int>* const progress = IntStruct()->AddVariable(
    L"progress",
    L"Counter of the number of times this buffer has made progress. This is "
    L"defined somewhat ambiguously, but roughly consists of new information "
    L"being read into the buffer. This is used to display progress for the "
    L"buffer.",
    0);

EdgeStruct<double>* DoubleStruct() {
  static EdgeStruct<double>* output = new EdgeStruct<double>();
  return output;
}

EdgeVariable<double>* const margin_lines_ratio = DoubleStruct()->AddVariable(
    L"margin_lines_ratio",
    L"Ratio of the number of lines in the screen reserved to display context "
    L"around the current position in the current buffer at the top/bottom of "
    L"the screen. See also variable `margin_lines`.",
    0.07);

EdgeVariable<double>* const beep_frequency_success =
    DoubleStruct()->AddVariable(
        L"beep_frequency_success",
        L"Frequency of the beep to play when a command buffer exits "
        L"successfully. If 0, disables the beep.",
        880.0);

EdgeVariable<double>* const beep_frequency_failure =
    DoubleStruct()->AddVariable(
        L"beep_frequency_failure",
        L"Frequency of the beep to play when a command buffer exits with an "
        L"error. If 0, disables the beep.",
        440.0);

}  // namespace buffer_variables
}  // namespace editor
}  // namespace afc

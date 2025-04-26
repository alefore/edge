#include "src/buffer_variables.h"

using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;

namespace afc::editor::buffer_variables {

EdgeStruct<bool>* BoolStruct() {
  static EdgeStruct<bool>* output = new EdgeStruct<bool>();
  return output;
}

EdgeVariable<bool>* const pts =
    &BoolStruct()
         ->Add()
         .Name(L"pts")
         .Description(
             L"If a command is forked that writes to this buffer, should it be "
             L"run with its own pseudoterminal?")
         .Build();

EdgeVariable<bool>* const vm_exec =
    &BoolStruct()
         ->Add()
         .Name(L"vm_exec")
         .Description(
             L"If set, all input read into this buffer will be executed.")
         .Build();

EdgeVariable<bool>* const close_after_clean_exit =
    &BoolStruct()
         ->Add()
         .Name(L"close_after_clean_exit")
         .Description(
             L"If a command is forked that writes to this buffer, should the "
             L"buffer be closed when the command exits with a successful "
             L"status "
             L"code?\n\n"
             L"This can be used to fork commands that you expect to succeed "
             L"and "
             L"where you don't care for their output unless they fail.")
         .Build();

EdgeVariable<bool>* const allow_dirty_delete =
    &BoolStruct()
         ->Add()
         .Name(L"allow_dirty_delete")
         .Description(
             L"Allow this buffer to be deleted even if it's dirty (i.e. if it "
             L"has unsaved changes or an underlying process that's still "
             L"running).\n\n"
             L"This applies both if the buffer is closed explicitly or "
             L"implicitly when Edge exits.")
         .Build();

EdgeVariable<bool>* const reload_after_exit =
    &BoolStruct()
         ->Add()
         .Name(L"reload_after_exit")
         .Description(
             L"If a forked command that writes to this buffer exits, should "
             L"Edge reload the buffer automatically?\n\n"
             L"When the buffer is reloaded, this variable is automatically set "
             L"to the value of `default_reload_after_exit`.")
         .Build();

EdgeVariable<bool>* const default_reload_after_exit =
    &BoolStruct()
         ->Add()
         .Name(L"default_reload_after_exit")
         .Description(
             L"If a forked command that writes to this buffer exits and "
             L"reload_after_exit is set, what should Edge set "
             L"reload_after_exit "
             L"just "
             L"after reloading the buffer?")
         .Build();

EdgeVariable<bool>* const reload_on_enter =
    &BoolStruct()
         ->Add()
         .Name(L"reload_on_enter")
         .Description(
             L"Should this buffer be reloaded automatically when visited?")
         .Build();

EdgeVariable<bool>* const atomic_lines =
    &BoolStruct()
         ->Add()
         .Name(L"atomic_lines")
         .Key(L"a")
         .Description(
             L"If true, lines can't be joined (e.g. you can't delete the last "
             L"character in a line unless the line is empty). In this case, "
             L"instead of displaying the cursors, Edge will show the currently "
             L"selected line.\n\n"
             L"This is used by certain buffers (such as the list of buffers or "
             L"a view of the contents of a directory) that represent lists of "
             L"things (each represented as a line), for which this is a "
             L"natural "
             L"behavior.")
         .Build();

EdgeVariable<bool>* const term_on_close =
    &BoolStruct()
         ->Add()
         .Name(L"term_on_close")
         .Description(
             L"If this buffer has a child process, should Edge send a SIGTERM "
             L"signal to the child process when the buffer is closed?")
         .Build();

EdgeVariable<bool>* const save_on_close =
    &BoolStruct()
         ->Add()
         .Name(L"save_on_close")
         .Description(
             L"Should this buffer be saved automatically when it's closed?\n\n"
             L"This applies both if the buffer is closed explicitly or "
             L"implicitly when Edge exits.")
         .Build();

EdgeVariable<bool>* const clear_on_reload =
    &BoolStruct()
         ->Add()
         .Name(L"clear_on_reload")
         .Description(
             L"Should any previous contents be discarded when this buffer is "
             L"reloaded? If false, previous contents will be preserved and new "
             L"contents will be appended at the end.\n\n"
             L"This is useful mainly for buffers with the output of commands, "
             L"where you don't want to discard the output of previous runs as "
             L"you reload the buffer.")
         .DefaultValue(true)
         .Build();

EdgeVariable<bool>* const paste_mode =
    &BoolStruct()
         ->Add()
         .Name(L"paste_mode")
         .Key(L"p")
         .Description(
             L"When paste_mode is enabled in a buffer, it will be displayed in "
             L"a way that makes it possible to select (with a mouse) parts of "
             L"it  (that are currently shown).  It will also allow you to "
             L"paste "
             L"text directly into the buffer (i.e., it will disable any smart "
             L"indenting).")
         .Build();

EdgeVariable<bool>* const follow_end_of_file =
    &BoolStruct()
         ->Add()
         .Name(L"follow_end_of_file")
         .Key(L"f")
         .Description(L"Should the cursor stay at the end of the file?")
         .Build();

EdgeVariable<bool>* const commands_background_mode =
    &BoolStruct()
         ->Add()
         .Name(L"commands_background_mode")
         .Description(
             L"Should new commands forked from this buffer be started in "
             L"background mode?  If false, we will switch to them "
             L"automatically.\n\n"
             L"This just affects whether we switch the currently selected Edge "
             L"buffer to the new buffer; it has no effect whatsoever in the "
             L"command.")
         .Build();

EdgeVariable<bool>* const reload_on_buffer_write =
    &BoolStruct()
         ->Add()
         .Name(L"reload_on_buffer_write")
         .Description(
             L"Should the current buffer (on which this variable is set) be "
             L"reloaded when any buffer is written?\n\n"
             L"This is useful mainly for command buffers like `make` or `git "
             L"diff`.\n\n"
             L"If you set this, you may also want to set "
             L"`contains_line_marks`.")
         .Build();

EdgeVariable<bool>* const trigger_reload_on_buffer_write =
    &BoolStruct()
         ->Add()
         .Name(L"trigger_reload_on_buffer_write")
         .Description(
             L"Does a write of this buffer trigger a reload of other buffers "
             L"that have variable `reload_on_buffer_write` set? This is mainly "
             L"useful to ensure that *internal* buffers (such as prompt "
             L"history) don't trigger reload of user-visible buffers (such as "
             L"compilers) on quit.")
         .DefaultValue(true)
         .Build();

EdgeVariable<bool>* const contains_line_marks =
    &BoolStruct()
         ->Add()
         .Name(L"contains_line_marks")
         .Description(
             L"Indicates whether the current buffer should be scanned for "
             L"\"marks\": lines that start with a prefix of the form "
             L"\"path:line\" (e.g. `src/test.cc:23`). For any such marks "
             L"found, "
             L"the corresponding lines in the corresponding buffers (i.e., "
             L"buffers for the corresponding files) will be highlighted.\n\n"
             L"This is useful for *compiler* commands like `make` that output "
             L"lines with compilation errors.\n\n"
             L"Unfortunately, we don't currently support any fancy formats: "
             L"the "
             L"lines need to start with the marks. This, however, is good "
             L"enough for many compilers. But if your commands output lines in "
             L"a format such as `Error in src/test.cc:23:` this won't be very "
             L"useful.\n\n"
             L"If you set this on a buffer, you may want to also set variable "
             L"`reload_on_buffer_write`.")
         .Build();

EdgeVariable<bool>* const multiple_cursors =
    &BoolStruct()
         ->Add()
         .Name(L"multiple_cursors")
         .Description(
             L"If `true`, all commands apply to all cursors in the current "
             L"buffer. Otherwise, they only apply to the active cursor.")
         .Build();

EdgeVariable<bool>* const reload_on_display =
    &BoolStruct()
         ->Add()
         .Name(L"reload_on_display")
         .Description(
             L"If set to true, a buffer will always be reloaded before being "
             L"displayed.")
         .Build();

EdgeVariable<bool>* const show_in_buffers_list =
    &BoolStruct()
         ->Add()
         .Name(L"show_in_buffers_list")
         .Key(L"s")
         .Description(L"If set to true, includes this in the list of buffers.")
         .DefaultValue(true)
         .Build();

EdgeVariable<bool>* const push_positions_to_history =
    &BoolStruct()
         ->Add()
         .Name(L"push_positions_to_history")
         .Description(
             L"If set to true, movement in this buffer causes new positions to "
             L"be pushed to the history of positions.\n\n"
             L"A few buffers default this to `false`, to avoid pushing their "
             L"positions to the history.")
         .DefaultValue(true)
         .Build();

EdgeVariable<bool>* const delete_into_paste_buffer =
    &BoolStruct()
         ->Add()
         .Name(L"delete_into_paste_buffer")
         .Description(
             L"If set to true, deletions from this buffer go into the shared "
             L"paste buffer.\n\n"
             L"A few buffers, such as prompt buffers, default this to `false`.")
         .DefaultValue(true)
         .Build();

EdgeVariable<bool>* const scrollbar =
    &BoolStruct()
         ->Add()
         .Name(L"scrollbar")
         .Key(L"S")
         .Description(L"If set to true, the scrollbar will be shown.")
         .DefaultValue(true)
         .Build();

EdgeVariable<bool>* const search_case_sensitive =
    &BoolStruct()
         ->Add()
         .Name(L"search_case_sensitive")
         .Key(L"/c")
         .Description(
             L"Should search (through `/`) always be case sensitive? If false, "
             L"the case will still be case sensitive if the filter has "
             L"upper-case characters.")
         .Build();

EdgeVariable<bool>* const search_filter_buffer =
    &BoolStruct()
         ->Add()
         .Name(L"search_filter_buffer")
         .Key(L"/d")
         .Description(
             L"Should search delete this buffer if it fails to find any "
             L"matches?")
         .Build();

EdgeVariable<bool>* const wrap_from_content =
    &BoolStruct()
         ->Add()
         .Name(L"wrap_from_content")
         .Description(
             L"If true, lines will be wrapped (either at the end of the screen "
             L"or after `line_width` characters) based on spaces, avoiding "
             L"breaking words when feasible.")
         .Build();

EdgeVariable<bool>* const extend_lines =
    &BoolStruct()
         ->Add()
         .Name(L"extend_lines")
         .Description(
             L"If set to true, lines should be extended automatically as the "
             L"cursor advances past their end.")
         .Build();

EdgeVariable<bool>* const display_progress =
    &BoolStruct()
         ->Add()
         .Name(L"display_progress")
         .Description(
             L"If set to true, if this buffer is reading input (either from a "
             L"regular file or a process), it'll be shown in the status line.")
         .DefaultValue(true)
         .Build();

EdgeVariable<bool>* const persist_state =
    &BoolStruct()
         ->Add()
         .Name(L"persist_state")
         .Description(
             L"Should we aim to persist information for this buffer (in "
             L"$EDGE_PATH/state/)?")
         .Build();

EdgeVariable<bool>* const pin =
    &BoolStruct()
         ->Add()
         .Name(L"pin")
         .Key(L"P")
         .Description(
             L"If true, this buffer will be pinned: Edge will try hard to "
             L"display it in the screen (while honoring other variables that "
             L"affect which buffers are displayed).")
         .Build();

EdgeVariable<bool>* const vm_lines_evaluation =
    &BoolStruct()
         ->Add()
         .Name(L"vm_lines_evaluation")
         .Key(L"v")
         .Description(
             L"If true, all lines in this buffer will be compiled; if they "
             L"compile successfully, their type will be shown as metadata. If "
             L"they are pure expressions, they will be evaluated and the "
             L"results of the evaluation will be shown.")
         .DefaultValue(true)
         .Build();

EdgeVariable<bool>* const view_center_lines =
    &BoolStruct()
         ->Add()
         .Name(L"view_center_lines")
         .Key(L"c")
         .Description(
             L"If true, lines will be shown centered on the screen. Ignored if "
             L"`paste_mode` is true.")
         .Build();

EdgeStruct<LazyString>* StringStruct() {
  static EdgeStruct<LazyString>* output = new EdgeStruct<LazyString>();
  return output;
}

EdgeVariable<LazyString>* const name =
    &StringStruct()
         ->Add()
         .Name(L"name")
         .Description(L"Name of the current buffer.")
         .Build();

EdgeVariable<LazyString>* const symbol_characters =
    &StringStruct()
         ->Add()
         .Name(L"symbol_characters")
         .Description(
             L"String with all the characters that should be considered part "
             L"of "
             L"a symbol. This affects commands such as `dW` (delete symbol).")
         .DefaultValue(LazyString{L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopq"
                                  L"rstuvwxyz0123456789_"})
         .Build();

EdgeVariable<LazyString>* const path_characters =
    &StringStruct()
         ->Add()
         .Name(L"path_characters")
         .Description(
             L"String with all the characters that should be considered part "
             L"of "
             L"a path.")
         .DefaultValue(LazyString{L"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  L"abcdefghijklmnopqrstuvwxyz"
                                  L"0123456789"
                                  L"_-.*:/"})
         .Build();

EdgeVariable<LazyString>* const path =
    &StringStruct()
         ->Add()
         .Name(L"path")
         .Description(L"String with the path of the current file.")
         .Predictor(FilePredictor)
         .Build();

EdgeVariable<LazyString>* const pts_path =
    &StringStruct()
         ->Add()
         .Name(L"pts_path")
         .Description(
             L"String with the path of the terminal used by the current buffer "
             L"(or empty if the user is not using a terminal).")
         .Predictor(FilePredictor)
         .Build();

EdgeVariable<LazyString>* const children_path =
    &StringStruct()
         ->Add()
         .Name(L"children_path")
         .Description(
             L"If non-empty, string with the path of the directory used when "
             L"forking a new command from the current buffer. If empty, the "
             L"new "
             L"command will inherit the current working directory that Edge "
             L"was "
             L"run in.")
         .Predictor(FilePredictor)
         .Build();

EdgeVariable<LazyString>* const command =
    &StringStruct()
         ->Add()
         .Name(L"command")
         .Description(
             L"String with the current command. Empty if the buffer is not a "
             L"sub-process (e.g. a regular file).")
         .Predictor(FilePredictor)
         .Build();

EdgeVariable<LazyString>* const editor_commands_path =
    &StringStruct()
         ->Add()
         .Name(L"editor_commands_path")
         .Description(
             L"String with the path to the initial directory used when "
             L"prompting the user for an editor command to run. It does not "
             L"affect in any way the execution of these commands (simply the "
             L"prompting).")
         .Predictor(FilePredictor)
         .Build();

EdgeVariable<LazyString>* const line_prefix_characters =
    &StringStruct()
         ->Add()
         .Name(L"line_prefix_characters")
         .Description(
             L"String with all the characters that should be considered the "
             L"prefix of the actual contents of a line.  When a new line is "
             L"created, the prefix of the previous line (the sequence of all "
             L"characters at the start of the previous line that are listed in "
             L"line_prefix_characters) is copied to the new line.  The order "
             L"of "
             L"characters has no effect.")
         .DefaultValue(LazyString{L" "})
         .Build();

EdgeVariable<LazyString>* const paragraph_line_prefix_characters =
    &StringStruct()
         ->Add()
         .Name(L"paragraph_line_prefix_characters")
         .Description(
             L"Similar to line_prefix_characters, but contains additional "
             L"characters that are allowed in the prefix of the first line of "
             L"a "
             L"paragraph (but wouldn't be allowed in continuation lines).")
         .DefaultValue(LazyString{L" "})
         .Build();

EdgeVariable<LazyString>* const line_suffix_superfluous_characters =
    &StringStruct()
         ->Add()
         .Name(L"line_suffix_superfluous_characters")
         .Description(
             L"String with all the characters that should be removed from the "
             L"suffix of a line (after editing it).  The order of characters "
             L"in "
             L"has no effect.")
         .DefaultValue(LazyString{L" "})
         .Build();

EdgeVariable<LazyString>* const dictionary =
    &StringStruct()
         ->Add()
         .Name(L"dictionary")
         .Description(
             L"Path to a dictionary file used for autocompletion. If empty, "
             L"pressing TAB (in insert mode) just inserts a tab character into "
             L"the file; otherwise, it triggers completion to the first string "
             L"from this file that matches the prefix of the current word. "
             L"Pressing TAB again iterates through all completions.\n\n"
             L"The dictionary file must be a text file containing one word per "
             L"line and sorted alphabetically.")
         .Predictor(FilePredictor)
         .Build();

// TODO(easy, 2023-09-16): Maintain a registry of available parsers, so that the
// following "Valid values ..." string doesn't become outdated.
EdgeVariable<LazyString>* const tree_parser =
    &StringStruct()
         ->Add()
         .Name(L"tree_parser")
         .Description(
             L"Name of the parser to use to extract the tree structure from "
             L"the "
             L"current file. Valid values are: \"text\" (normal text), and "
             L"\"cpp\". Any other value disables the tree logic.")
         .Build();

EdgeVariable<LazyString>* const language_keywords =
    &StringStruct()
         ->Add()
         .Name(L"language_keywords")
         .Description(
             L"Space separated list of keywords that should be highlighted by "
             L"the \"cpp\" tree parser (see variable tree_parser).")
         .Build();

EdgeVariable<LazyString>* const typos =
    &StringStruct()
         ->Add()
         .Name(L"typos")
         .Description(
             L"Space separated list of keywords that should be highlighted by "
             L"the tree parser as errors. This is only honored by a few tree "
             L"parser types (see variable tree_parser).")
         .Build();

EdgeVariable<LazyString>* const directory_noise =
    &StringStruct()
         ->Add()
         .Name(L"directory_noise")
         .Description(
             L"Regular expression to use in a buffer showing the contents of a "
             L"directory to identify files that should be considered as noise: "
             L"they are less important than most files.")
         .DefaultValue(LazyString{L".*(\\.o|~)|\\.(?!\\.$).*"})
         .Build();

EdgeVariable<LazyString>* const contents_type =
    &StringStruct()
         ->Add()
         .Name(L"contents_type")
         .Description(
             L"String identifying the type of contents in the buffer. Known "
             L"values are `path` for buffers that contain paths and the empty "
             L"string. This can be used to customize certain behaviors.")
         .Build();

EdgeVariable<LazyString>* const shell_command =
    &StringStruct()
         ->Add()
         .Name(L"shell_command")
         .Description(
             L"Command to run for an execution operation. Should use the "
             L"environment variable EDGE_INPUT to load the contents that the "
             L"user has selected.")
         .DefaultValue(LazyString{L"/usr/bin/bash $EDGE_INPUT"})
         .Build();

EdgeVariable<LazyString>* const cpp_prompt_namespaces =
    &StringStruct()
         ->Add()
         .Name(L"cpp_prompt_namespaces")
         .Key(L"n")
         .Description(
             L"Space-separated list of identifiers for namespaces to search "
             L"for "
             L"commands (functions) given to the CPP prompt (`:`).")
         .Build();

EdgeVariable<LazyString>* const file_context_extensions =
    &StringStruct()
         ->Add()
         .Name(L"file_context_extensions")
         .Key(L"E")
         .Description(
             L"Space-separated list of extensions to look for files based on "
             L"the identifier under the cursor.")
         .Build();

EdgeVariable<LazyString>* const identifier_behavior =
    &StringStruct()
         ->Add()
         .Name(L"identifier_behavior")
         .Key(L"I")
         .Description(
             L"What behavior should we use to colorize identifiers? This is "
             L"currently only used by cpp mode. Valid values are "
             L"\"color-by-hash\" and empty string.")
         .Build();

EdgeVariable<LazyString>* const completion_model_paths =
    &StringStruct()
         ->Add()
         .Name(L"completion_model_paths")
         .Key(L"t")
         .Description(
             L"Paths to completion model files. See doc/completion_model.md "
             L"for details.")
         .DefaultValue(LazyString{L"iso-639/en"})
         .Build();

EdgeStruct<int>* IntStruct() {
  static EdgeStruct<int>* output = new EdgeStruct<int>();
  return output;
}

EdgeVariable<int>* const line_width =
    &IntStruct()
         ->Add()
         .Name(L"line_width")
         .Key(L"w")
         .Description(
             L"Desired maximum width of a line. The syntax information, scroll "
             L"bar, and other relevant information (when available) will be "
             L"displayed after this number of characters. Lines will also be "
             L"wrapped (see variable `wrap_from_content`) "
             L"based on this value. If set to 1, the value "
             L"will be taken from the size of the screen (i.e., use as many "
             L"columns as are currently available.")
         .DefaultValue(80)
         .Build();

EdgeVariable<int>* const buffer_list_context_lines =
    &IntStruct()
         ->Add()
         .Name(L"buffer_list_context_lines")
         .Description(
             L"Number of lines of context from this buffer to show in the list "
             L"of buffers.")
         .DefaultValue(5)
         .Build();

EdgeVariable<int>* const margin_lines =
    &IntStruct()
         ->Add()
         .Name(L"margin_lines")
         .Description(
             L"Number of lines of context to display at the top/bottom of the "
             L"current position.")
         .DefaultValue(2)
         .Build();

EdgeVariable<int>* const margin_columns =
    &IntStruct()
         ->Add()
         .Name(L"margin_columns")
         .Description(
             L"Number of characters of context to display at the left/right of "
             L"the current position.")
         .DefaultValue(2)
         .Build();

EdgeVariable<int>* const progress =
    &IntStruct()
         ->Add()
         .Name(L"progress")
         .Description(
             L"Counter of the number of times this buffer has made progress. "
             L"This is defined somewhat ambiguously, but roughly consists of "
             L"new information being read into the buffer. This is used to "
             L"display progress for the buffer.")
         .DefaultValue(0)
         .Build();

EdgeVariable<int>* const analyze_content_lines_limit =
    &IntStruct()
         ->Add()
         .Name(L"analyze_content_lines_limit")
         .Description(
             L"Maximum number distance we can navigate away from the current "
             L"position before disabling the content analysis (that counts "
             L"words, alnums, etc.) for performance reasons.")
         .DefaultValue(50)
         .Build();

EdgeStruct<double>* DoubleStruct() {
  static EdgeStruct<double>* output = new EdgeStruct<double>();
  return output;
}

EdgeVariable<double>* const margin_lines_ratio =
    &DoubleStruct()
         ->Add()
         .Name(L"margin_lines_ratio")
         .Description(
             L"Ratio of the number of lines in the screen reserved to display "
             L"context around the current position in the current buffer at "
             L"the "
             L"top/bottom of the screen. See also variable `margin_lines`.")
         .DefaultValue(0.07)
         .Build();

EdgeVariable<double>* const beep_frequency_success =
    &DoubleStruct()
         ->Add()
         .Name(L"beep_frequency_success")
         .Description(
             L"Frequency of the beep to play when a command buffer exits "
             L"successfully. If 0, disables the beep.")
         .DefaultValue(440.0)
         .Build();

EdgeVariable<double>* const beep_frequency_failure =
    &DoubleStruct()
         ->Add()
         .Name(L"beep_frequency_failure")
         .Description(
             L"Frequency of the beep to play when a command buffer exits with "
             L"an error. If 0, disables the beep.")
         .DefaultValue(880.0)
         .Build();

EdgeVariable<double>* const close_after_idle_seconds =
    &DoubleStruct()
         ->Add()
         .Name(L"close_after_idle_seconds")
         .Description(
             L"If non-negative, close the buffer after it has been idle for "
             L"this number of seconds.")
         .DefaultValue(-1.0)
         .Build();

EdgeStruct<LineColumn>* LineColumnStruct() {
  static EdgeStruct<LineColumn>* output = new EdgeStruct<LineColumn>();
  return output;
}

EdgeVariable<LineColumn>* const view_start =
    &LineColumnStruct()->Add().Name(L"view_start").Description(L"...").Build();

}  // namespace afc::editor::buffer_variables

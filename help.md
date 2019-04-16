# Edge - Help

## Commands

The following is a list of all commands available in your buffer, grouped by category.

### Buffers

a. - opens a view of the current directory
ad - closes the current buffer
af - Prompts for a command and creates a new buffer running it.
al - lists all open buffers
ar -  Reload the current buffer.
aw -  Save the current buffer.

### C++ Functions (Extensions)

​# - Load file: ~/.edge/editor_commands/reflow
$ - Go to the end of the current line
J - Load file: ~/.edge/editor_commands/fold-next-line
K - Delete the current line
M - Center the screen around the current line.
^ - Go to the beginning of the current line
sC - clang_format = !clang_format
sI - Load file: ~/.edge/editor_commands/include
sh - Load file: ~/.edge/editor_commands/header
si - Load file: ~/.edge/editor_commands/indent

### Cursors

+ -  Create a new cursor at the current position.
- -  Destroy current cursor(s) and jump to next.
= -  Destroy cursors other than the current one.
C! -  Set active cursors to the marks on this buffer.
C+ -  Pushes the active cursors to the stack.
C- -  Pops active cursors from the stack.
Ct -  Toggles the active cursors with the previous set.
_ -  Toggles whether operations apply to all cursors.

### Edit

^d -  Delete current character.
^k -  Delete to end of line.
^u -  Delete to the beginning of line.
← Backspace -  Delete previous character.
Tab - Autocompletes the current word.
. -  Repeats the last command.
D - deletes the current item (char, word, line...)
d - starts a new delete command
i - enters insert mode
p - pastes the last deleted text
sr - starts/stops recording a transformation
u - undoes the last change to the current buffer
~ - Switches the case of the current character.

### Editor

Esc - Resets the state of the editor.
? - Shows documentation.
aQ - Quits Edge (with an exit value of 1).
aq - Quits Edge (with an exit value of 0).

### Extensions

aC - prompts for a command (a C string) and runs it
ac - runs a command from a file

### Modifiers

! - sets the structure: mark
* - Toggles the strength.
0 - 
1 - 
2 - 
3 - 
4 - 
5 - 
6 - 
7 - 
8 - 
9 - 
B - sets the structure: buffer
E - sets the structure: page
F - sets the structure: search
R - activates replace modifier (overwrites text on insertions)
W - sets the structure: symbol
[ - sets the structure modifier: from the beggining to the current position
] - sets the structure modifier: from the current position to the end
c - sets the structure: cursor
e - sets the structure: line
r - reverses the direction of the next command
t - sets the structure: tree
w - sets the structure: word

### Navigate

^e -  Move to the end of line.
^a -  Move to the beginning of line.
PgUp - moves up one page
PgDn - moves down one page
→ - moves forwards
← - moves backwards
↑ - moves up one line
↓ - moves down one line
↩ - activates the current link (if any)
% - Navigates to the start/end of the current children of the syntax tree
/ - Searches for a string.
N - displays a navigation view of the current buffer
b - go back to previous position
f - Waits for a character to be typed and moves the cursor to its next occurrence in the current line.
g - goes to Rth structure from the beginning
h - moves backwards
j - moves down one line
k - moves up one line
l - moves forwards
n - activates navigate mode.

### Prompt

aF - forks a command for each line in the current buffer
ao - loads a file
av - assigns to a variable

### Subprocess

ae - stops writing to a subprocess (effectively sending EOF).

### Variables

v/c -  Toggle buffer variable: search_case_sensitive
vS -  Toggle buffer variable: scrollbar
vc -  Toggle buffer variable: buffer_list_context_lines
vf -  Toggle buffer variable: follow_end_of_file
vp -  Toggle buffer variable: paste_mode
vs -  Toggle buffer variable: show_in_buffers_list
vw -  Toggle buffer variable: wrap_long_lines

### View

^l - Redraws the screen

## Environment

### Types & methods

This section contains a list of all types available to Edge extensions running in your buffer. For each, a list of all their available methods is given.

#### SetInt

contains                                function<bool(SetInt, int)>
empty                                   function<bool(SetInt)>
erase                                   function<void(SetInt, int)>
get                                     function<int(SetInt, int)>
insert                                  function<void(SetInt, int)>
size                                    function<int(SetInt)>

#### SetString

contains                                function<bool(SetString, string)>
empty                                   function<bool(SetString)>
erase                                   function<void(SetString, string)>
get                                     function<string(SetString, int)>
insert                                  function<void(SetString, string)>
size                                    function<int(SetString)>

#### VectorInt

erase                                   function<void(VectorInt, int)>
get                                     function<int(VectorInt, int)>
push_back                               function<void(VectorInt, int)>
size                                    function<int(VectorInt)>

#### VectorString

erase                                   function<void(VectorString, int)>
get                                     function<string(VectorString, int)>
push_back                               function<void(VectorString, string)>
size                                    function<int(VectorString)>

#### bool

tostring                                function<string(bool)>

#### double

round                                   function<int(double)>
tostring                                function<string(double)>

#### int

tostring                                function<string(int)>

#### string

empty                                   function<bool(string)>
find                                    function<int(string, string, int)>
find_first_not_of                       function<int(string, string, int)>
find_first_of                           function<int(string, string, int)>
find_last_not_of                        function<int(string, string, int)>
find_last_of                            function<int(string, string, int)>
shell_escape                            function<string(string)>
size                                    function<int(string)>
starts_with                             function<bool(string, string)>
substr                                  function<string(string, int, int)>
tolower                                 function<string(string)>
toupper                                 function<string(string)>

#### Buffer

AddBinding                              function<void(Buffer, string, string, function<void()>)>
AddBindingToFile                        function<void(Buffer, string, string)>
AddKeyboardTextTransformer              function<bool(Buffer, function<string(string)>)>
ApplyTransformation                     function<void(Buffer, Transformation)>
DeleteCharacters                        function<void(Buffer, int)>
EvaluateFile                            function<void(Buffer, string)>
Filter                                  function<void(Buffer, function<bool(string)>)>
InsertText                              function<void(Buffer, string)>
Map                                     function<void(Buffer, function<string(string)>)>
PopTransformationStack                  function<void(Buffer)>
PushTransformationStack                 function<void(Buffer)>
Reload                                  function<void(Buffer)>
Save                                    function<void(Buffer)>
allow_dirty_delete                      function<bool(Buffer)>
atomic_lines                            function<bool(Buffer)>
beep_frequency_failure                  function<double(Buffer)>
beep_frequency_success                  function<double(Buffer)>
buffer_list_context_lines               function<int(Buffer)>
children_path                           function<string(Buffer)>
clear_on_reload                         function<bool(Buffer)>
close_after_clean_exit                  function<bool(Buffer)>
command                                 function<string(Buffer)>
commands_background_mode                function<bool(Buffer)>
contains_line_marks                     function<bool(Buffer)>
default_reload_after_exit               function<bool(Buffer)>
delete_into_paste_buffer                function<bool(Buffer)>
dictionary                              function<string(Buffer)>
directory_noise                         function<string(Buffer)>
display_progress                        function<bool(Buffer)>
editor_commands_path                    function<string(Buffer)>
extend_lines                            function<bool(Buffer)>
follow_end_of_file                      function<bool(Buffer)>
language_keywords                       function<string(Buffer)>
line                                    function<string(Buffer, int)>
line_count                              function<int(Buffer)>
line_prefix_characters                  function<string(Buffer)>
line_suffix_superfluous_characters      function<string(Buffer)>
line_width                              function<int(Buffer)>
margin_columns                          function<int(Buffer)>
margin_lines                            function<int(Buffer)>
margin_lines_ratio                      function<double(Buffer)>
multiple_cursors                        function<bool(Buffer)>
name                                    function<string(Buffer)>
paragraph_line_prefix_characters        function<string(Buffer)>
paste_mode                              function<bool(Buffer)>
path                                    function<string(Buffer)>
path_characters                         function<string(Buffer)>
persist_state                           function<bool(Buffer)>
position                                function<LineColumn(Buffer)>
progress                                function<int(Buffer)>
pts                                     function<bool(Buffer)>
pts_path                                function<string(Buffer)>
push_positions_to_history               function<bool(Buffer)>
reload_after_exit                       function<bool(Buffer)>
reload_on_buffer_write                  function<bool(Buffer)>
reload_on_display                       function<bool(Buffer)>
reload_on_enter                         function<bool(Buffer)>
save_on_close                           function<bool(Buffer)>
scrollbar                               function<bool(Buffer)>
search_case_sensitive                   function<bool(Buffer)>
set_allow_dirty_delete                  function<void(Buffer, bool)>
set_atomic_lines                        function<void(Buffer, bool)>
set_beep_frequency_failure              function<void(Buffer, double)>
set_beep_frequency_success              function<void(Buffer, double)>
set_buffer_list_context_lines           function<void(Buffer, int)>
set_children_path                       function<void(Buffer, string)>
set_clear_on_reload                     function<void(Buffer, bool)>
set_close_after_clean_exit              function<void(Buffer, bool)>
set_command                             function<void(Buffer, string)>
set_commands_background_mode            function<void(Buffer, bool)>
set_contains_line_marks                 function<void(Buffer, bool)>
set_default_reload_after_exit           function<void(Buffer, bool)>
set_delete_into_paste_buffer            function<void(Buffer, bool)>
set_dictionary                          function<void(Buffer, string)>
set_directory_noise                     function<void(Buffer, string)>
set_display_progress                    function<void(Buffer, bool)>
set_editor_commands_path                function<void(Buffer, string)>
set_extend_lines                        function<void(Buffer, bool)>
set_follow_end_of_file                  function<void(Buffer, bool)>
set_language_keywords                   function<void(Buffer, string)>
set_line_prefix_characters              function<void(Buffer, string)>
set_line_suffix_superfluous_characters  function<void(Buffer, string)>
set_line_width                          function<void(Buffer, int)>
set_margin_columns                      function<void(Buffer, int)>
set_margin_lines                        function<void(Buffer, int)>
set_margin_lines_ratio                  function<void(Buffer, double)>
set_multiple_cursors                    function<void(Buffer, bool)>
set_name                                function<void(Buffer, string)>
set_paragraph_line_prefix_characters    function<void(Buffer, string)>
set_paste_mode                          function<void(Buffer, bool)>
set_path                                function<void(Buffer, string)>
set_path_characters                     function<void(Buffer, string)>
set_persist_state                       function<void(Buffer, bool)>
set_position                            function<void(Buffer, LineColumn)>
set_progress                            function<void(Buffer, int)>
set_pts                                 function<void(Buffer, bool)>
set_pts_path                            function<void(Buffer, string)>
set_push_positions_to_history           function<void(Buffer, bool)>
set_reload_after_exit                   function<void(Buffer, bool)>
set_reload_on_buffer_write              function<void(Buffer, bool)>
set_reload_on_display                   function<void(Buffer, bool)>
set_reload_on_enter                     function<void(Buffer, bool)>
set_save_on_close                       function<void(Buffer, bool)>
set_scrollbar                           function<void(Buffer, bool)>
set_search_case_sensitive               function<void(Buffer, bool)>
set_show_in_buffers_list                function<void(Buffer, bool)>
set_symbol_characters                   function<void(Buffer, string)>
set_tree_parser                         function<void(Buffer, string)>
set_trigger_reload_on_buffer_write      function<void(Buffer, bool)>
set_typos                               function<void(Buffer, string)>
set_view_start_column                   function<void(Buffer, int)>
set_view_start_line                     function<void(Buffer, int)>
set_vm_exec                             function<void(Buffer, bool)>
set_wrap_long_lines                     function<void(Buffer, bool)>
show_in_buffers_list                    function<bool(Buffer)>
symbol_characters                       function<string(Buffer)>
tree_parser                             function<string(Buffer)>
trigger_reload_on_buffer_write          function<bool(Buffer)>
typos                                   function<string(Buffer)>
view_start_column                       function<int(Buffer)>
view_start_line                         function<int(Buffer)>
vm_exec                                 function<bool(Buffer)>
wrap_long_lines                         function<bool(Buffer)>

#### Editor

CreateCursor                            function<void(Editor)>
DestroyCursor                           function<void(Editor)>
DestroyOtherCursors                     function<void(Editor)>
PopActiveCursors                        function<void(Editor)>
PushActiveCursors                       function<void(Editor)>
ReloadCurrentBuffer                     function<void(Editor)>
RepeatLastTransformation                function<void(Editor)>
SaveCurrentBuffer                       function<void(Editor)>
SetActiveCursorsToMarks                 function<void(Editor)>
ToggleActiveCursors                     function<void(Editor)>
home                                    function<string(Editor)>

#### LineColumn

column                                  function<int(LineColumn)>
line                                    function<int(LineColumn)>
tostring                                function<string(LineColumn)>

#### Modifiers

set_backwards                           function<void(Modifiers)>
set_boundary_end_neighbor               function<void(Modifiers)>
set_line                                function<void(Modifiers)>
set_repetitions                         function<void(Modifiers, int)>

#### Range

begin                                   function<LineColumn(Range)>
end                                     function<LineColumn(Range)>

#### Screen

Clear                                   function<void(Screen)>
Flush                                   function<void(Screen)>
HardRefresh                             function<void(Screen)>
Move                                    function<void(Screen, int, int)>
Refresh                                 function<void(Screen)>
SetCursorVisibility                     function<void(Screen, string)>
SetModifier                             function<void(Screen, string)>
WriteString                             function<void(Screen, string)>
columns                                 function<int(Screen)>
lines                                   function<int(Screen)>
set_size                                function<void(Screen, int, int)>

#### SetLineColumn

contains                                function<bool(SetLineColumn, LineColumn)>
empty                                   function<bool(SetLineColumn)>
erase                                   function<void(SetLineColumn, LineColumn)>
get                                     function<LineColumn(SetLineColumn, int)>
insert                                  function<void(SetLineColumn, LineColumn)>
size                                    function<int(SetLineColumn)>

#### Transformation


#### VectorLineColumn

erase                                   function<void(VectorLineColumn, int)>
get                                     function<LineColumn(VectorLineColumn, int)>
push_back                               function<void(VectorLineColumn, LineColumn)>
size                                    function<int(VectorLineColumn)>


### Variables

The following are all variables defined in the environment associated with your buffer, and thus available to extensions.

  SetInt                                  function<SetInt()>
  SetString                               function<SetString()>
  VectorInt                               function<VectorInt()>
  VectorString                            function<VectorString()>
  ConnectTo                               function<void(string)>
  CurrentBuffer                           function<Buffer()>
  Exit                                    function<void(int)>
  FindBoundariesLine                      function<void(LineColumn, LineColumn, SetLineColumn, SetLineColumn)>
  ForkCommand                             function<Buffer(string, bool)>
  Line                                    function<string()>
  LineColumn                              function<LineColumn(int, int)>
  Modifiers                               function<Modifiers()>
  OpenFile                                function<Buffer(string, bool)>
  ProcessInput                            function<void(int)>
  Range                                   function<Range(LineColumn, LineColumn)>
  RemoteScreen                            function<Screen(string)>
  ScheduleRedraw                          function<void()>
  SendExitTo                              function<void(string)>
  SetLineColumn                           function<SetLineColumn()>
  SetPositionColumn                       function<void(int)>
  SetStatus                               function<void(string)>
  ShapesReflow                            function<VectorString(VectorString, int)>
  TransformationDelete                    function<Transformation(Modifiers)>
  TransformationGoToColumn                function<Transformation(int)>
  VectorLineColumn                        function<VectorLineColumn()>
  WaitForClose                            function<void(SetString)>
  editor                                  Editor
  modifiers                               Modifiers
  repetitions                             function<int()>
  screen                                  Screen
  set_exit_value                          function<void(int)>
  set_repetitions                         function<void(int)>
  set_screen_needs_hard_redraw            function<void(bool)>
  tmp_buffer                              Buffer
  BaseCommand                             function<string(string)>
  Basename                                function<string(string)>
  BreakWords                              function<VectorString(string)>
  CenterScreenAroundCurrentLine           function<void()>
  ClangFormatOnSave                       function<void()>
  ClangFormatToggle                       function<void()>
  CppMode                                 function<void()>
  DeleteCurrentLine                       function<void()>
  DiffMode                                function<void()>
  GetPrefix                               function<string(string, string)>
  GoToBeginningOfLine                     function<void()>
  GoToEndOfLine                           function<void()>
  HandleFileTypes                         function<void(string, string)>
  JavaMode                                function<void()>
  LineHasPrefix                           function<bool(string, int)>
  LineIsInParagraph                       function<bool(string, int)>
  ScrollBackToBeginningOfParagraph        function<void(string)>
  SkipFinalSpaces                         function<string(string)>
  SkipInitialSpaces                       function<string(string)>
  SkipSpaces                              function<string(string)>
  base_command                            ""
  basename                                "main.cc"
  buffer                                  Buffer
  clang_format                            1
  command                                 ""
  dot                                     25
  extension                               "cc"
  next                                    ""
  path                                    "/home/alejo/edge/src/main.cc"

## Buffer Variables

The following are all the buffer variables defined for your buffer.

### bool

#### allow_dirty_delete

Allow this buffer to be deleted even if it's dirty (i.e. if it has unsaved changes or an underlying process that's still running).

* Value: false
* Default: false

#### atomic_lines

If true, lines can't be joined (e.g. you can't delete the last character in a line unless the line is empty).  This is used by certain buffers that represent lists of things (each represented as a line), for which this is a natural behavior.

* Value: false
* Default: false

#### clear_on_reload

Should any previous contents be discarded when this buffer is reloaded? If false, previous contents will be preserved and new contents will be appended at the end.

* Value: true
* Default: true

#### close_after_clean_exit

If a command is forked that writes to this buffer, should the buffer be closed when the command exits with a successful status code?

* Value: false
* Default: false

#### commands_background_mode

Should new commands forked from this buffer be started in background mode?  If false, we will switch to them automatically.

* Value: false
* Default: false

#### contains_line_marks

If set to true, this buffer will be scanned for line marks.

* Value: false
* Default: false

#### default_reload_after_exit

If a forked command that writes to this buffer exits and reload_after_exit is set, what should Edge set reload_after_exit just after reloading the buffer?

* Value: false
* Default: false

#### delete_into_paste_buffer

If set to true, deletions from this buffer go into the shared paste buffer.

A few buffers, such as prompt buffers, default this to `false`.

* Value: true
* Default: true

#### display_progress

If set to true, if this buffer is reading input (either from a regular file or a process), it'll be shown in the status line.

* Value: true
* Default: true

#### extend_lines

If set to true, lines should be extended automatically as the cursor advances past their end.

* Value: false
* Default: false

#### follow_end_of_file

Should the cursor stay at the end of the file?

* Value: false
* Default: false

#### multiple_cursors

If `true`, all commands apply to all cursors in the current buffer. Otherwise, they only apply to the active cursor.

* Value: false
* Default: false

#### paste_mode

When paste_mode is enabled in a buffer, it will be displayed in a way that makes it possible to select (with a mouse) parts of it (that are currently shown).  It will also allow you to paste text directly into the buffer.

* Value: false
* Default: false

#### persist_state

Should we aim to persist information for this buffer (in $EDGE_PATH/state/)?

* Value: true
* Default: false

#### pts

If a command is forked that writes to this buffer, should it be run with its own pseudoterminal?

* Value: false
* Default: false

#### push_positions_to_history

If set to true, movement in this buffer causes new positions to be pushed to the history of positions.

A few buffers default this to `false`, to avoid pushing their positions to the history.

* Value: true
* Default: true

#### reload_after_exit

If a forked command that writes to this buffer exits, should Edge reload the buffer?

* Value: false
* Default: false

#### reload_on_buffer_write

Should the current buffer (on which this variable is set) be reloaded when any buffer is written?  This is useful mainly for command buffers like 'make' or 'git diff'.

* Value: false
* Default: false

#### reload_on_display

If set to true, a buffer will always be reloaded before being displayed.

* Value: false
* Default: false

#### reload_on_enter

Should this buffer be reloaded automatically when visited?

* Value: false
* Default: false

#### save_on_close

Should this buffer be saved automatically when it's closed?

* Value: false
* Default: false

#### scrollbar

If set to true, the scrollbar will be shown.

* Value: true
* Default: true

#### search_case_sensitive

If set to true, search (through "/") is case sensitive.

* Value: false
* Default: false

#### show_in_buffers_list

If set to true, includes this in the list of buffers.

* Value: true
* Default: true

#### trigger_reload_on_buffer_write

Does a write of this buffer trigger a reload of other buffers that have variable `reload_on_buffer_write` set? This is mainly useful to ensure that *internal* buffers (such as prompt history) don't trigger reload of user-visible buffers (such as compilers) on quit.

* Value: true
* Default: true

#### vm_exec

If set, all input read into this buffer will be executed.

* Value: false
* Default: false

#### wrap_long_lines

If set to true, long lines will be wrapped (only for displaying). Otherwise, they get trimmed at the end.

* Value: true
* Default: true


### string

#### children_path

If non-empty, string with the path of the directory used when forking a new command from the current buffer. If empty, the new command will inherit the current working directory that Edge was run in.

* Value: ``
* Default: ``

#### command

String with the current command. Empty if the buffer is not a sub-process (e.g. a regular file).

* Value: ``
* Default: ``

#### dictionary

Path to a dictionary file used for autocompletion. If empty, pressing TAB (in insert mode) just inserts a tab character into the file; otherwise, it triggers completion to the first string from the dictionary that matches the prefix of the current word. Pressing TAB again iterates through all completions.

* Value: ``
* Default: ``

#### directory_noise

Regular expression to use in a buffer showing the contents of a directory to identify files that should be considered as noise: they are less important than most files.

* Value: `.*(\.o|~)|\.(?!\.$).*`
* Default: `.*(\.o|~)|\.(?!\.$).*`

#### editor_commands_path

String with the path to the initial directory for editor commands.

* Value: `~/.edge/editor_commands/`
* Default: ``

#### language_keywords

Space separated list of keywords that should be highlighted by the "cpp" tree parser (see variable tree_parser).

* Value: `static extern override virtual class struct private public protected using typedef namespace sizeof static_cast dynamic_cast delete new switch case default if else for while do break continue return void const mutable auto unique_ptr shared_ptr std function vector list map unordered_map set unordered_set int double float string wstring bool char size_t true false nullptr NULL`
* Default: ``

#### line_prefix_characters

String with all the characters that should be considered the prefix of the actual contents of a line.  When a new line is created, the prefix of the previous line (the sequence of all characters at the start of the previous line that are listed in line_prefix_characters) is copied to the new line.  The order of characters in line_prefix_characters has no effect.

* Value: ` /*`
* Default: ` `

#### line_suffix_superfluous_characters

String with all the characters that should be removed from the suffix of a line (after editing it).  The order of characters in line_suffix_superfluous_characters has no effect.

* Value: ` `
* Default: ` `

#### name

Name of the current buffer.

* Value: `/home/alejo/edge/src/main.cc`
* Default: ``

#### paragraph_line_prefix_characters

Similar to line_prefix_characters, but contains additional characters that are allowed in the prefix of the first line of a paragraph (but wouldn't be allowed in continuation lines).

* Value: ` /*`
* Default: ` `

#### path

String with the path of the current file.

* Value: `/home/alejo/edge/src/main.cc`
* Default: ``

#### path_characters

String with all the characters that should be considered part of a path.

* Value: `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.*:/`
* Default: `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.*:/`

#### pts_path

String with the path of the terminal used by the current buffer (or empty if the user is not using a terminal).

* Value: ``
* Default: ``

#### symbol_characters

String with all the characters that should be considered part of a symbol.

* Value: `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_`
* Default: `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_`

#### tree_parser

Name of the parser to use to extract the tree structure from the current file. Valid values are: "text" (normal text), and "cpp". Any other value disables the tree logic.

* Value: `cpp`
* Default: ``

#### typos

Space separated list of keywords that should be highlighted by the tree parser as errors. This is only honored by a few tree parser types (see variable tree_parser).

* Value: `overriden`
* Default: ``


### int

#### buffer_list_context_lines

Number of lines of context from this buffer to show in the list of buffers.

* Value: 0
* Default: 0

#### line_width

Desired maximum width of a line.

* Value: 80
* Default: 80

#### margin_columns

Number of characters of context to display at the left/right of the current position.

* Value: 2
* Default: 2

#### margin_lines

Number of lines of context to display at the top/bottom of the current position.

* Value: 2
* Default: 2

#### progress

Counter of the number of times this buffer has made progress. This is defined somewhat ambiguously, but roughly consists of new information being read into the buffer. This is used to display progress for the buffer.

* Value: 21
* Default: 0

#### view_start_column

The desired column to show at the left-most part of the screen. This is adjusted automatically as the cursor moves around in the buffer.

* Value: 0
* Default: 0

#### view_start_line

The desired line to show at the beginning of the screen (at the top-most position). This is adjusted automatically as the cursor moves around in the buffer.

* Value: 0
* Default: 0


## Command line arguments

### help

The --help command-line argument displays a brief overview of the available command line arguments and exits.

### fork

Required argument: shellcmd: Shell command to run

The --fork command-line argument must be followed by a shell command. Edge will create a buffer running that command.

Example:

    edge --fork "ls -lR /tmp" --fork "make"

If Edge is running nested (inside an existing Edge), it will cause the parent instance to open those buffers.

### run

Required argument: vmcmd: VM command to run

The --run command-line argument must be followed by a string with a VM command to run.

Example:

    edge --run 'string flags = "-R"; ForkCommand("ls " + flags, true);'



### load

Required argument: path: Path to file containing VM commands to run

Load a file with VM commands

### server

Optional argument: path: Path to the pipe in which to run the server

The --server command-line argument causes Edge to run in *background* mode: without reading any input from stdin nor producing any output to stdout. Instead, Edge will wait for connections to the path given.

If you pass an empty string (or no argument), Edge generates a temporary file. Otherwise, the path given must not currently exist.

Edge always runs with a server, even when this flag is not used. Passing this flag merely causes Edge to daemonize itself and not use the current terminal. Technically, it's more correct to say that this is "background" or "headless" mode than to say that this is "server" mode. However, we decided to use "--server" (instead of some other flag) for symmetry with "--client".

For example, you'd start the server thus:

    edge --server /tmp/edge-server-blah

You can then connect a client:

    edge --client /tmp/edge-server-blahIf your session is terminated (e.g. your SSH connection dies), you can run the client command again.

### client

Required argument: path: Path to the pipe in which the daemon is listening

Connect to daemon at a given path

### mute

Disable audio output

### bg

Open buffers given to -f in background

### X

When `edge` runs nested (i.e., under a parent instance), the child instance will not create any buffers for any files that the user may have passed as command-line arguments nor any commands (passed with `--fork`). Instead, it will connect to the parent and request that the parent itself creates the corresponding buffers.

The `-X` command-line argument controls when the child instance will exit. By default, it will wait until any buffers that it requests are deleted by the user (with `ad`). This is suitable for commands such as `git commit` that may run a nested instance of Edge. However, when `-X` is given, the child instance will exit as soon as it has successfully communicated with the parent (without waiting for the user to delete corresponding buffers.

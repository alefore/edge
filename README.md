# Edge - A text editor.


## 1. Introduction

Edge is a terminal-based text editor for GNU/Linux.

This document describes the use of Edge. In Edge, key sequences are bond to
specific commands. These sequences are given in this document between quotes.

Edge uses *buffers* to represent an open file or a process, which might still be
running and which may or may not have a full terminal (pts).

* Extensibility:

  * Edge uses a simplified verison of C++ as its extension language. Extensions
    are interpreted (type errors are detected statically) and the language uses
    garbage collection. See
    [buffer-reload.cc](https://github.com/alefore/edge/blob/master/rc/hooks/buffer-reload.cc)
    for an example extension.

  * All buffers have *variables* that control their behavior. For
    example, variable `scrollbar` controls whether the scrollbar should be shown
    in the current buffer. In the extension language, this can be set with
    `buffer.set_scrollbar(!buffer.scrollbar());` (and the binding `vS` will
    toggle its value).

* Shell commands (external processes):

  * Supports running external commands (e.g. `ls -lR`), capturing their output
    into a buffer. The user can interact with these buffers just as with the
    regular "text" buffers. To do this, the user would type `af` (short for
    "advanced" and "fork") to be prompted for a shell command to run.

  * Use of a pts can be enabled or disabled for buffers with underlying commands
    (this is controlled by buffer's variable `pts`). For example, one can run a
    shell process (or even some other text editor) inside Edge.

* Always responsive. Edge is designed with the philosophy that the user should
  not be forced to wait for the completion of operations they initiate (such as
  loading a file, running a shell command and collecting its output, or
  compiling the program in the current directory). More accurately, the editor
  should never cease to respond to user commands simply because it is executing
  an action. Buffers in Edge are read asynchronously and Edge never blocks while
  performing IO operations (there are a few exceptional situations to this that
  we would like to fix).

* Editing:

  * Supports multiple cursors. For example, searching creates a cursor at every
    match of the query string (and jumps to the one following the previous
    cursor position). One can toggle whether edit commands apply to all cursors
    or just to the "current" one.

  * Supports syntax highlighting for a few programming languages (C++, Java) and
    file formats (Markdown, diff/patch).


### 1.1. Screenshots

The screenshot shows Edge (running under gnome-terminal) editing a C++ file
(part of its own source code):

![Edge Screenshot](/screenshots/shot.png?raw=true "Edge Screenshot")

There are multiple cursors in lines 2, 5, 7, 9, 19, 25, and 27 (among others),
after a regular-expression search for the word `buffer` (which creates a cursor
in each match). The cursor in line 19 is currently the only active cursor (so
commands would currently not affect the remaining cursors).

The right-most column shows the scrollbar besides the tree (that corresponds to
the syntax tree of the contents being edited).


## 2. Getting started

### 2.1. Running Edge

By default, Edge will create a buffer running a nested shell. If you run Edge on
a file, it will open it and display it:

    $ edge README.md

You can see the documentation for command line arguments if you run Edge with
`--help`:

    $ edge --help

Once in Edge, you can press `?` to get [help](/help.md) about your current
buffer.


### 1.1. Installing

The following commands download, configure, and build Edge:

    $ git clone https://github.com/alefore/edge.git
    $ cd edge
    $ ./autogen.sh && ./glog-0.4.0/autogen.sh && ./configure
    $ make

TODO: Document the list of dependencies.


### 2.1. Example commands

The following are a few example commands:

* `?` - Shows the help information for the current buffer (it looks something
  [like this](/help.md)).

* `aq` - Quit (short for "Advanced > Quit"). At the beginning of the execution
  of Edge, Ctrl+c will also quit (until the moment when you start editing any
  buffer).

* `?` - Shows help.

* `i` - Insert (type to the buffer), until Escape is pressed.  See section 4.1.

* `d\n` - Delete the current character.

  * `d5\n` - Delete 5 characters.  Delete is covered in section 4.2.

  * `de\n` - Delete until the end of line (Delete; Line).  Section 6.1 explains
    the Line modifier (and other structure modifiers).

  * `dwj\n` - Delete until the end of the word (Word; from beginning to end).
    The word modifier is covered in section 6.1.

  * `d5wj\n` - Delete five words (starting at the current position).

* `p` - Paste the previously deleted sequence.  See section 4.3.

* `u` - Undo the last command.  See section 4.4.

* `af make\n` - Open a new buffer with the output of a given command. `af` is
  short for "Advanced > Fork" and is covered in section 5.3.

* `ar` - Reload the current buffer.  See section 5.2.

* `al` - Shows the list of buffers currently open.  See section 5.1.

* `a.` - Show the list of files in the current directory.  See section 5.5.

* `+` - Creates a new cursor (at the same position as the current one). See
  section 8.1.

* `_` - Toggles whether all cursors apply transformations (or just the current
  one). See section 8.3.


## 3. Navigating a buffer


### 3.1 Basic navigation

The cursor will start at the beginning of the file.  You can use "h", "j", "k",
"l" or the arrow keys to move it around.

The current structure alters the behavior of "h" and "l" thus:

*   Word: Goes to the next (previous) word. If advancing, the cursor is left at
    the start of the next word; if going backwards, at the end.
*   Line: Goes to the next (previous) line.
*   Mark: Goes to the next (previous) mark in the current buffer. See section
    7.2.23 for details about line marks.


### 3.2 Search for a regular expression

You can search for a string with "/": Edge will prompt you for a regular
expression to search for and pressing Enter will take you to its first match
after the current cursor position.

Search supports autocompletion, where hitting Tab will replace the current
regular expression by the longest common prefix of its next 10 matches
(configurable through variable xxx, see section 7).  Press Tab once again to
display a buffer with the list of matches.

To repeat a search, use the Search structure (section 6.1).

If the Word structure (section 6.1) is enabled, Edge will not prompt you for a
regular expression: it will, instead, just search for the word under the cursor.

To search backwards, use the Reverse modifier (section 6.3).


### 3.3. Search for a character

You can search for a character with "f": this will take you to the next
occurrence of the next character you type.  This is roughly equivalent to typing
"/", entering a character and pressing Enter.

To search backwards, use the Reverse modifier (section 6.3).


### 3.4. History of positions

Edge keeps a history of the positions in which you've been.  You can go back to
the previous position with "b".

The list of positions is kept in a buffer called "- positions".

This behavior is affected by variable push_positions_to_history (section 7.2.25).

TODO: Describe the effect of structures.


### 3.5. Goto beginning

Use "g" to go to the first character in the current line.  Press "g" again to go
to the last character in the current line.

If a repetition is active, the behavior changes to "go to the i-th character
in the current line".

The current structure alters the behavior thus:

*   Word: Goes to the i-th word from the beginning of the current line.
*   Line: Goes to the i-th line from the beginning of the current buffer.
*   Mark: Goes to the i-th mark from the beginning of the current buffer. See
    section 7.2.23 for details about line marks.
*   Page: Goes to the i-th page from the beginning of the current buffer.
*   Buffer: Goes to the i-th buffer (in the list of buffers).

If the Reverse modifier is enabled, moves backwards from the end.

If the structure modifier is set to "FromEndToCurrentPosition", moves backwards
from the end (unless the Reverse modifier is also enabled, in which case they
cancel out).

(The current implementation is slightly different than this in how it handles
repeated presses, but we'll likely change it to do this.)


### 3.6. Go to file

If the cursor is over a path that exists in the filesystem, pressing Enter
creates a new buffer with the contents of that file (or, if a buffer for the
file already exists, just takes you to it).

See section 5.5 for more commands to open a file.


### 3.7. Navigation mode

If you press "n", you activate a special navigation mode, which uses binary
search to help you very quickly land in the character (in the current line)
that you want.

This can be a very efficient way to reach a given position. For example, if you
start at the beginning of a relatively long line with 100 characters, all
positions are reachable with at most 8 keystrokes (including the initial "n").

In navigation mode, if you press "l" ("h") you'll move forwards (backwards) to
the middle position between the current cursor position and the end (beginning)
of the current line, discarding (for the purpose of subsequent navigation) all
characters before (after) the current cursor position. Each subsequent press (of
"l" or "h") moves the cursor in the corresponding direction.

The current structure alters the behaviors thus:
- Word: constrains the range to the current word (instead of the entire line).
- Line: instead of moving the column left or right, moves the cursor up or down
  in the current file.

You exit navigation mode by pressing any character other than "l" or "h".


## 4. Editing a buffer


### 4.1. Inserting characters

To type some text into a buffer, use "i".  This enters insert mode, which is
indicated at the bottom of the screen by the word "type" (of "type (raw)" if the
buffer is a command with a terminal).  Press Esc to exit insert mode.  Anything
you type while in insert mode will be inserted into the buffer; for buffers with
an associated terminal, the text will be written to the terminal.

If insert mode is activated with the Line structure, an empty line will be
inserted above the current line and text will be inserted into it.  The Reverse
modifier will instead insert the line after the current line.

The insertion modifier (section 6.5) will specify if insert should preserve the
contents that follow the cursor (default) or replace them as you type.

While inserting to a terminal, press ctrl+v to activate "literal" insertion: the
next character will be inserted literally.  This is mainly useful to insert an
Esc code (instead of having Esc take you out of insert mode).

### 4.1.1. Autocomplete

If the dictionary variable is set to a non-empty string, pressing Tab will cause
that string to be interpreted as the path to the dictionary file. The contents
of the file will be loaded into a buffer and used to look for completions.
Pressing Tab again will iterate through the entries in the dictionary following
the first completion. See section 7.2.22 for more information on how to create a
dictionary file.

### 4.2. Deleting contents

Press "d" to enter delete mode. The section to be deleted will be highlighted.
Press "\n" to confirm and execute the deletion, or Esc to abort it and leave the
buffer unmodified.

You can select modifiers in the delete mode (before pressing "\n"). For example:

*   To delete from the current position to the end of line, use: "e"
*   To delete the next 5 words (starting from the current position(, use: "5w"

#### 4.2.1. Deleting words

When deleting words, the contents of variable `word_characters` section 7.2.xxx)
are used to specify the list of characters that are considered part of a word.
If the cursor position is in a non-word character, characters will be deleted
until the beginning of the word, and then the "delete word" command will run).

### 4.3. Pasting characters

Press "p" to paste the last contents deleted by the delete command.

Whenever "d" deletes some contents, they go to a special buffer called "- paste
buffer".  You can modify the contents in this buffer directly to alter what "p"
will paste.

The insertion modifier (section 6.5) will specify if insert should preserve the
contents that follow the cursor (default) or replace them as you type.

This behavior is affected by variable `delete_into_paste_buffer` (section
7.2.26).


### 4.4. Undo & Redo

Edge maintains full undo history.  Press "u" to undo the last edition.
Navigation commands (that only alter the cursor position but not the contents of
the buffer) are ignored: undo will revert the last transformation.

To "re do" activate the Reverse modifier (section 6.3).


### 4.5. Repeat the last command

You can repeat the last transformation at the current position by pressing ".".

You can group multiple commands to be repeated by pressing "sr" (short for
Secondary > Record), then applying all the commands you want to group, and then
pressing "sr" again.  If you then press ".", you will repeat all the commands
in the sequence.


### 4.6 Capitals swap

Press "~" to convert the current character from upper case to lower case (and
viceversa) and advance by one position. Similar to deleting, you'll have to
confirm the operation by pressing "\n".


## 5. Buffers

### 5.1. List of buffers

Press "al" ("Advanced > List") to open a buffer that contains the list of
buffers.  Each line in this buffer represents a buffer: an open file or the
output of a subprocess (which may be running or may have already exited).

If you delete lines in this buffer (with "ed", see sections 6.1 and 4.2), this
will close the associated buffers.  If you have unsaved changes, you will lose
them.

The list of buffers will be updated dynamically whenever you create new buffers
or as they make progress (e.g. it will show the number of lines that have been
read from a command that's currently running).


### 5.2. Reload a buffer

You can reload a buffer with "ar".  If the buffer is a file, this discards any
unsaved changes.  If the buffer is a command (section 5.3), this will kill it
with SIGTERM (if it's still running), wait for it to exit, and restart it.

If you're in the list of buffers (section 5.1), you can activate Line mode
("e", section 6.1) and press "ar" to reload the buffer that the cursor is
currently on.  During software development, this can provide a quick way to
re-run commands like "make" during development and wait for them to terminate
successfully (since the list of buffers will be updated dynamically as they make
progress).


### 5.3. Run a command

You can run a command with "af" ("Advanced > Fork").  Edge will prompt you for a
command to run and will create a buffer in which it will run this command.  If a
buffer for the command already existed, Edge will just reload the process
(similar to "ar" as described in section 5.2).

If Line structure is active ("e", section 6.1), "af" will not prompt you for a
command but will instead run the command given in the current line.  For
commands that you might expect to run in the future, we recommend creating a
simple text file for your project in which you list them, so that you can
quickly launch them with this mechanism.

By default, commands will be run without a controlling terminal.  This can be
toggled (which can be very useful e.g. for running a shell in a buffer) with the
variable "pts" (section 7.2.1).  For example, if you run "bash", you'll probably
want to do "av" (set variable), then "pts" and Enter (to toggle the value of the
boolean variable) and then press "ar" to restart the shell.  Of course, this
should be done automatically through the buffer-reload.cc script (which we'll
start distributing soon).


### 5.4. Running many commands.

TODO: Describe aF


### 5.5. Open a file

#### 5.5.1. Advanced > Open ("ao")

Use "ao" ("Advanced > Open") to open a file. Edge will prompt you for the
filename. Tab will autocomplete (and pressing Tab again will show all the
options). You can adjust the list of paths in which autocomplete will search for
files by editing the special "- search paths" buffer (which should contain one
path per line).

In practice, we tend to use "a." (section 5.6) much more frequently to open
files.  We also tend to navigate across files by moving the cursor over to a
path and pressing Enter (section 3.6).


#### 5.5.2. Path suffixes

Edge will look for an optional suffix to the path indicating the position in the
file, which is mostly compatible with the output of GCC and other similar
compilers.  The following examples show the supported forms:

*   `file.cc:54` takes you to line 54 of `file.cc`.
*   `file.cc:54:12` additionally takes you to column 12.
*   `file.cc:/XXX` takes you to the first match in `file.cc` for the regular
    expression "XXX".

These suffixes can be given anywhere where Edge opens a file (e.g. they also
work for files specified through the command line).


#### 5.5.3. Anonymous buffers

If you open a buffer with an empty path (i.e. you press Return immediately after
pressing "ao"), you'll create an anonymous buffer.  Anonymous buffers can't be
saved unless you give them a path (by assigning to the path variable, described
in section 7.2.15).


### 5.6. Files view

If you press "a." ("Advanced > Current directory") in a buffer, a new buffer
will be created (unless it already existed) displaying the contents of the
directory containing the current buffer, one file (or directory) per line.

Like the list of buffers (section 5.1), this is a special buffer:

*   Deleting lines (with "ed") from this buffer will, after prompting you for
    confirmation, delete the underlying files from the file system. See section
    5.8 for more details on the logic of deleting buffers.

*   Pressing Enter on the line for a given file will create a buffer for that
    file (unless one already existed, in which case it'll just take you to it).


### 5.7. Save a file

Use "aw" ("Advanced > Write") in a buffer to save its contents.

Some buffers can't be saved and will display an error in the status.


### 5.8. Delete a buffer

Press "ad" ("Advanced > Delete") to delete (close) the current buffer.

A buffer is said to be dirty if any of these conditions holds:

*   It has unsaved modifications.
*   It has a running process.
*   It had a running process which was killed or exited with a non-zero status.

If you attempt to close a dirty buffer:

*   If variable `save_on_close` (section 7.2.7) is set, Edge will attempt to
    save the buffer.
*   If the buffer is still dirty, Edge will prevent you from deleting the
    buffer, unless variable `allow_dirty_delete` (section 7.2.8) is set to true
    or the strength modifier is set above Default.

See section 5.1 for another mechanism for closing buffers, by deleting lines
from the list of buffers.


## 6. Structures & structure modifiers

Edge has a notion of "modifiers" that alter the behavior of commands.  There are
various kinds of modifiers, described in this section.

TODO: This section is somewhat obsolete.


### 6.1. Logical structures

The most important type of modifier is the logical "structure" that the next
command will affect.  To set the structure, press any of the following keys:

*   "w" - Word: Affect the current word.
*   "e" - Line: Affect the current line.
*   "c" - Cursor: Affect the region from the current cursor to the next.
*   "!" - Mark: Use the marks for lines in the current buffer. See section 7.2.23
      for details about marks.
*   "F" - Search: The region affected is based on the next match to the last search
      query (performed with "/", section 3.2).  This structure is somewhat
      different to the others and relatively fewer commands recognize it.
*   "E" - Page: Affect the current page.  The size of a page is computed dynamically
      based on the current screen size.
*   "B" - Buffer: Affect the current buffer.

The default structure is the current character.  The current structure will be
shown at the bottom of the screen (unless the structure is set to the default
value).

Once a command is executed that is affected by the structure modifier, it will
set the structure back to the default value.  However, redundantly setting the
structure to the value it already has will activate "stickyness": in this case,
the structure will not reset back to the default value.

Pressing Esc will reset the structure to the default.

Here are some examples:

- If you expect to erase many lines, you can set the structure to Line in sticky
  mode ("ee") and then just delete away ("ddddd").
- To navigate by matches to a given search, you can perform a search and set the
  structure to Search in sticky mode ("FF") and then just navigate matches with
  "h" or "l".


### 6.2. Sub-structure modifiers ("[" and "]")

When the structure is set to anything other than character, "[" changes the
semantics to mean "apply the command to the part of the structure from its
beginning until the current cursor position".  Conversely, "]" changes the
meaning to "apply the command from the current cursor position until the end".

The current modifier will be shown at the bottom of the screen (next to the
current structure modifier).

The specific behavior depends on the command.  This is particularly useful with
the delete command, where e.g. "]ed" could be used to delete to the beginning
of the current line.


### 6.3. Reverse modifier ("r")

Pressing "r" will reverse the direction in which the command is applied.  Most
commands set the direction back to the default value (forwards), but if you
press "rr", you'll activate sticky reverse mode.  Press Esc to leave sticky
mode.


### 6.4. Repetitions

You can type a number to set an amount of repetitions that affects the next
command.

The specific behavior depends on the command.  Most navigation and editing
commands simply repeat the command the given number of times.  Other commands
may have their behavior altered in different ways.


### 6.5. Insertion Modifier ("R")

By default, commands that insert text will "push" existing contents after the
current cursor position back.  Press "R" to activate "replace" mode, which will
instead overwrite previous contents of the buffer.  After a command has been
executed, the modifier will reset back to the default, but if you press "RR",
you'll activate sticky replace mode.  Press Esc to go back to the default
behavior.


## 7. Variables

### 7.1. Setting variables

Type "av" to set the value of a given variable. You will be promoted for the
variable you want to set. You can press Tab twice to see a list of all options
that match the current input (which can be empty, to list all variables). Press
Enter to edit the value of a variable.

If the variable is a boolean, its value will be toggled. Otherwise, you will be
prompted for the new value for the variable.

For example, to adjust the line width of the current buffer, press "av", then
type "line_width" (or a shorter prefix of that and use Tab to autocomplete),
then Enter, then edit the current value, and then press Enter.


### 7.2. List of variables


#### 7.2.1. `pts` (bool)

If a command is forked that writes to this buffer, should it be run with its own
pseudoterminal?

Default: false.


#### 7.2.2. `close_after_clean_exit` (bool)

If this buffer is a command buffer (e.g. it was started with "af", as described
in section 5.3), should the buffer be closed when the command exists with a
successful status code?

This can be used to fork commands that you expect to succeed and where you don't
care for their output unless they fail.

Default: false


#### 7.2.3. `reload_after_exit` (bool)

If this buffer is a command buffer (e.g. it was started with "af", as described
in section 5.3), should the buffer be reloaded automatically when the command
exits?

When the buffer is reloaded, this variable is automatically set to the value of
default_reload_after_exit (section 7.2.4).

Default: false


#### 7.2.4. `default_reload_after_exit` (bool)

If this buffer is a command buffer (e.g. it was started with "af", as described
in section 5.3), what should reload_after_exit (section 7.2.3) be set to as the
buffer is reloaded?

Default: false


#### 7.2.5. `reload_on_enter` (bool)

Should this buffer be reloaded automatically when visited?

Default: false


#### 7.2.6. `atomic_lines` (bool)

If true, lines can't be joined (e.g. you can't delete the last character in a
line unless the line is empty).  In this case, instead of displaying the cursor,
Edge will show the currently selected line.

This is used by certain buffers such as the list of buffers or a directory view
that represent lists of things (each represented as a line), for which this is a
natural behavior.

Default: false


#### 7.2.7. `save_on_close` (bool)

Should this buffer be saved automatically when it's closed?  See section 5.8 for
details.

This applies both when the buffer is closed explicitly (e.g. through "ad",
section 5.8) or when Edge exits.

Default: false


#### 7.2.8. `allow_dirty_delete` (bool)

Can this buffer be deleted if it's dirty?  See section 5.8 for details.

This applies both when the buffer is closed explicitly (e.g. through "ad",
section 5.8) or when Edge exits.

Default: false


#### 7.2.9. `clear_on_reload` (bool)

Should any previous contents be discarded when this buffer is reloaded? If
false, previous contents will be preserved and new contents will be appended at
the end.

This is useful mainly for buffers with the output of commands, where you don't
want to discard the output of previous runs as you reload the buffer.

Default: true


#### 7.2.10. `paste_mode` (bool)

When paste_mode is true in a buffer, it will be displayed in a way that makes it
possible to select (with a mouse) parts of it (that are currently shown).  It
will also allow you to paste text directly into the buffer (i.e. it will disable
any smart "auto indenting").

Paste mode can be quickly toggled with 'vp'.

Default: false


#### 7.2.11. `commands_background_mode` (bool)

Should new commands forked from this buffer be started in background mode?  If
false, we will switch to them automatically.

This just affects whether we switch the currently selected Edge buffer to the
new buffer; it has no effect whatsoever in the command.

Default: false


#### 7.2.12. `reload_on_buffer_write` (bool)

Should the buffer on which this variable is set be reloaded automatically when
any buffer is written?

This is useful mainly for command buffers like 'make' or 'git diff'.

If you set this on a buffer, you may want to also set contains_line_marks
(section 7.2.22).

Default: false


#### 7.2.13. `word_characters` (string)

String with all the characters that should be considered part of a word.  This
affects commands such as "wd" (delete word).

Default: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_"


#### 7.2.14. `path_characters` (string)

String with all the characters that should be considered part of a path.

Default: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.*:/"


#### 7.2.15. `path` (string)

String with the path of the current file.

Default: ""


#### 7.2.16. `pts_path` (string)

String with the path of the terminal used by the current buffer (or empty if the
buffer is not using a terminal).

Default: ""


#### 7.2.17. `command` (string)

String with the current command (or empty).

Default: ""


## 8. Cursors

One important feature of Edge is the support for multiple cursors.  This allows
the user to apply edit commands at various parts of a document simultaneously.

Most of the time only a single cursor, the "current cursor", is "active":
commands will apply to that cursor only. However, after creating multiple
cursors (section 8.1), the user can mark all cursors as active (section 8.3), in
which case edit commands will apply at all cursor positions.

If the current buffer has more than one cursor (regardless of whether a single
cursor is active or all are), Edge will display this in the status line (at the
bottom), with a text such as "cursors:28".

For example, a user may "save the current position":

  1. Press "+" (section 8.1) to save the current position (i.e., create a new
     cursor at the current position).
  2. Scroll away to a different position in the document. The new cursor
     (created in step #1) remains in the old position.
  3. Perform some modifications at the new position.
  4. Press "-" (section 8.2) to erase the active cursor; the remaining cursor
     (from step #1) will now become active, bringing the user back to the
     position at which it was created.

As a more interesting example, a user may modify all occurrences of a word in a
given region in a document:

  1. Scroll to the end of the region and press "+" (section 8.1) to create a new
     cursor.
  2. Scroll down to the beginning of the region. The region is now delimited by
     the only two cursors in the document.
  3. Press "c" to set the current logical structure (section 6.1) to the region
     delimited by the cursors.
  4. Press "/", enter a query string (regular expression) matching the word you
     want to replace and press Enter (section 3.2). This will remove all
     cursors in the current buffer and add one for every match for the regular
     expression inside the region.
  5. Press "_" (section 8.3) to make all cursors active.
  6. Enter some edit commands to delete the old word (e.g. "wd") and insert the
     new word (e.g. "i").
  7. Press "=" to remove all cursors but one.


### 8.1. Creating new cursors

This section describes the various ways a user can create new cursors.

The simplest way to create a cursor is by pressing "+": this creates a new
cursor at the position of the current active cursor. The new cursor will become
visible once the active cursor scrolls away from the current position.

If the logical structure (section 6.1) is Line and a cursor follows the current
in the document, a new cursor will be created at the beginning of every line
between the two cursors. This can be helpful to quickly indent (or otherwise
modify) every single line in a given region. The Reverse modifier (section 6.3)
causes the region affected to end (rather than start) at the current cursor; in
other words, it creates new cursors before (rather than after) the current
cursor.

Searching (with "/" as described in section 3.2) is another quick way to create
a new cursor at every position in a document (or in a region) that matches a
given regular expression.


### 8.2. Deleting cursors

Pressing "-" will delete the current cursor (and immediately jump to the next,
or to the first cursor when deleting the last cursor in the buffer).

Pressing "=" will delete all cursors except for the current one. If you have
created many cursors (for example, with a search query), this is a quicker way
to discard them than through "-".


### 8.3. Toggling active cursors

Pressing "_" makes Edge toggle the value of boolean variable "multiple_cursors"
(section 7.2.24), toggling between making commands apply to all cursors or only
to the active one.


### 8.4. Push and Pop

Each buffer has a stack of sets of cursors. This can be a handy way to store a
set of cursors, apply some transformation elsewhere, and restore the set.

Press "C+" to push: you'll copy the set of active cursors into a new entry in
the stack.

Press "C-" to pop: you'll be replacing the set of active cursors with the last
entry pushed into the stack, and removing that entry from the stack.


## 9. Extension language


### 9.1. Running commands

TODO: "ac"


### 9.2. Running scripts

You can press "c" to run a script. Edge will prompt you for the path to the
script, starting at the path set in the variable editor_commands_path (section
7.2.18)

The contents at that path will be compiled and the resulting expression will be
evaluated in the environment of the buffer.


### 9.3. Syntax supported

### 9.4. Programming interface

#### 9.4.1. string

String represents a string of characters.

The following is an example:

  string s = "alejandro";
  return s.substr(3, 5);  // Returns: "jandr"


##### 9.4.1.1. int string::size()

Returns an integer with the number of characters in the string.


##### 9.4.1.2. string::substr(int start, int len)


##### 9.4.1.3. bool string::empty()

Returns a boolean value indicating if the string is empty.


##### 9.4.1.4. bool string::starts_with(string prefix)

Returns a boolean value indicating if the string starts with a given prefix.


##### 9.4.1.5. int string::find(string prefix, int start)


##### 9.4.1.6. int string::find_last_of(string chars, int start)


##### 9.4.1.7. int string::find_last_not_of(string chars, int start)


##### 9.4.1.8. int string::find_first_of(string chars, int start)


##### 9.4.1.9. int string::find_first_not_of(string chars, int start)


##### 9.4.1.10. string string::toupper()

Returns a copy of the string, with all characters converted to uppercase.

Example:

    string x = "Alejandro";
    x.toupper();  // Returns: "ALEJANDRO"


##### 9.4.1.11. string string::tolower()

Returns a copy of the string, with all characters converted to lowercase.

Example:

    string x = "Alejandro";
    x.toupper();  // Returns: "alejandro"


#### 9.4.2. Buffer


##### 9.4.2.1. bool Buffer::AddKeyboardTextTransformer(function<string(string)> transformer)

If you pass a function mapping a string to a string, it'll be called by Edge on
every character that the user attempts to insert (by typing "i", section 4.1.).
The transformer can return a different string, to override the string inserted.
It could even alter the buffer (for example, erase the previous character, or
adjust the current position).

If you call it multiple times, all transformers will be run in the order in
which they've been added. Each transfomer will see the output of the previous
ones.


## 10. Internals


                                    ╭───────╮
                              ╭─────> Parse ├───────╮
    ╭──────────╮   ╭──────────┴─╮   │ Tree  │       │
    │ Commands │   │ Background │   ╰───^───╯       │
    │(bindings)<─╮ │   Thread   <──╮    │           │
    ╰───┬──────╯ │ ╰────────────╯  │    │           │
        │        ╰──────────────╮  │    │           │
        │  ┏━━━━━━━━━━━━━┓    ┏━┷━━┷━━━━┷━━┓    ╭───V────────────╮
        │  ┃ EditorState ┠────> OpenBuffer ┠────> BufferContents │
        │  ┗━━━━┯━━━━━━━━┛    ┗━┯━━━━━━━┯━━┛    ╰───────┬────────╯
        │       │               │       │               │
        │       │     ╭─────────╯       │            ╭──V───╮   ╭───────────╮
        │   ╭───V─────V─╮   ╭───────────V──╮ ╭───<───┤ Line ├───> Modifiers │
        ╰───>Environment│   │ Process Info │ │       ╰──┬───╯   │ (colors…) │
            │  (cpp)    <─╮ │  (fd, pid…)  │ │          │       ╰───────────╯
            ╰─────┬─────╯ │ ╰──────────────╯ │   ╭──────V─────╮
                  │       │                  │   │ LazyString │
            ╭─────V─────╮ ╰──<─────<─────<───╯   ╰───┬────────╯
            │ EdgeStruct│                            │
            │Instance<…>│                            ├╴> EditableString
            ╰───────────╯                            ├╴> StringAppend
                                                     ├╴> Substring
                                                     ╰╴> MoveableCharBuffer

TODO: SetStatus("foo");
TODO: Document the integration points (e.g. buffer-reload.cc).
TODO: Useful examples

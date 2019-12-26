## Display

Consider using unicode watches to display duration, using exponential/logarithmic growth.

Fix a bug with buffers that are shown with fewer lines than mandated by their margins.

Make the widgets work closer to nethack or similar command-line games.

* `wrap_long_lines`:
  * Make `j` and `k` scroll within the line.
  * Adjust handing of margins to take into account lines that wrap.

Display the state of bool variables set different than their default values?

If a file is smaller than the screen (or available lines for its widget), show it centered (vertically) in the widget.

Change the cursor when we're in `type` mode.

### Syntax

Correctly handle: '\000'

Support more languages:
  - Python
  - Directory listings.
    - Perhaps have a bool that toggles 'stat' (show data about files)?
      Ideally this is done by the extensions!

## Editing

Implement delete of page.

For 'd': Add '?' (show modifiers available).

Add "pipe" command: select the region (similar to delete: line, paragraph, buffer...), and then prompt for a command. Pipe the contents of the region to the command, and replace them with the output of the command.

* Improve reflow:

  * Somehow integrate reflow (logic from `wrap_from_content`) with `reflow` script?

  * On reflow, leave the cursor where it was?

  * In a C++ file, handle multi-line strings better.
      printf("foo bar hey "
             "quux.");

* Improve multiple cursors:

  * Make sure that the section with the actual cursor is shown.

  * s+ and s-: Should support multiple cursors.

### Autocomplete

Add an AutoComplete mode that autocompletes based on the path.

When a user is typing a path to open, cache the contents of the directory. If the prefix entered uniquely matches a directory, start reading the contents of the directory proactively.

## Navigation

Make M center the widget around the current line.

Improve "g", the current behavior is kind of annoying:
  There should be a way (other than pressing it again) to specify if it should ignore space.  Maybe a modifier can do it?

Honor the `margin_columns` variable.

Add a boolean variable `highlight_current_line` (default: false); when set, highlight the line with the current cursor position.

In diff mode, add a link to every file. Following it should take you directly to the file (perhaps based on a variable for the `strip`, similar to the `patch` command).

Make "ad" and "aw" and "ar" honor repetitions (pick the buffer from the buffer's list)? Tricky: how to distinguish "current" from "1st".

## Widgets

Improve the bindings used to navigate the widgets.

* Add a key binding that shows a tree map with every single buffer (from the BuffersList). The user can then quickly select which one to go to. Perhaps the weight is proportional to use or time spent in file.

* Make hjkl move the active widget up/down/left/right.

## Prompt

When autocompletion of files fails, have a fail back:
  A second TAB that doesn't advance should do a deeper search: search in every path on which we have an open file.

When the prompt doesn't fit the screen, be smarter about what part to show? If it has a cursor, whatever is closest?

## Commands

Improve "af":
* Add more structures to "af":
  * BUFFER> run a command with the whole contents of the buffer
    (another possibility: run a command for each line in buffer (prompt))
  * Switch it to use the "new" enter-applies-command (similar to "d") mode.
* If the command doesn't match a regular expression (aiming to detect complex
  shell commands, such as `(for|while|[a-z]+=)`), automatically check in $PATH
  whether the first token of the command exists; show it in red if it doesn't.

When running subcommands with aC, actually set variables "ARG0", "ARG1", "ARG2", "ARG3", with words in line...

Create some "barriers" that limit the number of subprocesses.  Set a limit for each.  Maybe as a special type of buffer?  Let it reload to show the state?
  Commands should by default go against a shared barrier, but should have a variable that specifies what barrier to use.

Persist undo history?

Make follow_end_of_file the default.

## Directory view

Automatically adjust the width of the view to fit the screen.

## Readability

## Testing

### Fuzz testing

* Add a parameter with a file with input to deliver to the editor at start up. When starting, before running the main loop, read that as a file, and feed it to the editor.

* Maybe add a flag that triggers that the editor exits #x ms after the main loop starts?

* Ideally, it runs with full terminal. Maybe add a parameter that instructs it to create a fake terminal and write to it? We'll need to figure out how to redirect that output to /dev/null. Perhaps it can fork a separate process that reads from it?

## Variables

## VM

LineNumber and LineColumn types should be exposed to extensions?

Add support for templates, so that we can do "vector<string>".

Improve polymorphism: support polymorphic methods (rather than only global functions).

Support in-line functions and/or lambda forms. Tricky.

Improve support for `for`: the environments aren't properly nested as they should be.

Don't crash on this: buffer.set_view_start_line(-1);

Allow extensions to define classes or structures.

Support `LineColumn line;` (rather than having to use `LineColumn line = LineColumn();`).

### Client/server

## Bindings

* Improve repetitions handling: `a2g` should be equivalent to `2ag`

* Make a list of all keys and brainstorm bindings that could be given to them.

## Misc

Support variables scoped at the Editor level (i.e. not specific to a given buffer).

Don't do any blocking operations.

The first five times Edge runs, it should display a `help` message, perhaps at the top of the shell, and also in the SetStatus buffer.

Improve the help document:

* Add documentation string for VM functions/variables.

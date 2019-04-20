## Display

### Syntax

Correctly handle: '\000'

Support more languages:
  - Python
  - Directory listings.
    - Perhaps have a bool that toggles 'stat' (show data about files)?
      Ideally this is done by the extensions!

## Editing

Implement delete of page.

Let buffers "garbage collect" their contents: if they're clean and haven't been accessed in a while, just have them drop their contents.
  - Requires making them load them lazily.
  - Probably not too important? We can just let the OS page out appropriate pages.

For 'd': Add '?' (show modifiers available).

Add "pipe" command: select the region (similar to delete: line, paragraph, buffer...), and then prompt for a command. Pipe the contents of the region to the command, and replace them with the output of the command.

Add an autocomplete mode that autocompletes based on the path.

Improve logic around wrap_long_lines.
  Also scrolling to the end of file doesn't quite work.

Search: Should make sure to disable multi active cursors.

* Improve reflow:

  * Have an "auto-reflow" mode? As soon as the text width is crossed, do a
    reflow? Or, well, rather: whenever the buffer changes, reflow? For example,
    if the user deletes a work, just reflow?

  * On reflow, leave the cursor where it was?

  * In a C++ file, handle multi-line strings better.
      printf("foo bar hey "
             "quux.");

## Navigation

Improve "g", the current behavior is kind of annoying:
  There should be a way (other than pressing it again) to specify if it should ignore space.  Maybe a modifier can do it?

Honor the "margin_columns" variable.

Add a boolean variable "highlight_current_line" (default: false); when set, highlight the line with the current cursor position.

When searching, display in the Status the count of matches (especially when there were zero matches).

In diff mode, add a link to every file. Following it should take you directly to the file (perhaps based on a variable for the `strip`, similar to the `patch` command).

### List of buffers

## Prompt

When autocompletion of files fails, have a fail back:
  A second TAB that doesn't advance should do a deeper search: search in every path on which we have an open file.

When the prompt doesn't fit the screen, be smarter about what part to show? If it has a cursor, whatever is closest?

## Commands

Improve "af":
  Add more structures to "af":
    BUFFER> run a command with the whole contents of the buffer
      (another possibility: run a command for each line in buffer (prompt))
  Switch it to use the "new" enter-applies-command (similar to "d") mode.

When running subcommands with aC, actually set variables "ARG0", "ARG1", "ARG2", "ARG3", with words in line...

Create some "barriers" that limit the number of subprocesses.  Set a limit for each.  Maybe as a special type of buffer?  Let it reload to show the state?
  Commands should by default go against a shared barrier, but should have a variable that specifies what barrier to use.

Persist undo history?

Make follow_end_of_file the default.

## Variables

## VM

Add support for templates, so that we can do "vector<string>".

Improve polymorphism: support polymorphic methods (rather than only global functions).

Support in-line functions and/or lambda forms. Tricky.

Improve support for `for`: the environments aren't properly nested as they should be.

Don't crash on this: buffer.set_view_start_line(-1);

Allow extensions to define classes or structures.

Support `LineColumn line;` (rather than having to use `LineColumn line = LineColumn();`).

### Client/server

## Misc

Support variables scoped at the Editor level (i.e. not specific to a given buffer).

Don't do any blocking operations.

The first five times Edge runs, it should display a `help` message, perhaps at the top of the shell, and also in the SetStatus buffer.

Improve the help document:

* When showing variables, list bindings that toggle them.

* Move documentation about the variables from the README.md to buffer_variables.h

* Add documentation string for VM functions/variables.

## Display

### Syntax

Correctly handle: '\000'

Support more languages:
  - Python
  - Directory listings.
    - If a single dot:
      - Under-emphasize it.
      - Show the type of file, perhaps based on some variable?
         - text:cc,h,py,md,sh binary:o
         - Not sure the best way to do this is syntax highlighting. Maybe the buffer should do it, based also on `stat` results (e.g. permissions, symlink, subdir...)

## Editing

Implement delete of page.

Let buffers "garbage collect" their contents: if they're clean and haven't been accessed in a while, just have them drop their contents.
  - Requires making them load them lazily.
  - Probably not too important? We can just let the OS page out appropriate pages.

For 'd': Add '?' (show modifiers available).

Add "pipe" command: select the region (similar to delete: line, paragraph, buffer...), and then prompt for a command. Pipe the contents of the region to the command, and replace them with the output of the command.

Add an autocomplete mode that autocompletes based on the path.

Improve logic around wrap_long_lines.
  Cursors (other than active cursor) are off.
  Also scrolling to the end of file doesn't quite work.

Have a 'weak-word' region very similar to word, but breaking at camel-case or underscore.

## Navigation

Improve "g", the current behavior is kind of annoying:
  There should be a way (other than pressing it again) to specify if it should ignore space.  Maybe a modifier can do it?

Honor the "margin_columns" variable.

Add a boolean variable "highlight_current_line" (default: false); when set, highlight the line with the current cursor position.

If the buffer doesn't fit in the screen, don't show the scroll bar (or show it in a different way).

### List of buffers

## Prompt

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

atomic_lines should probably also apply to multiple cursors.

## VM

Add support for templates, so that we can do "vector<string>".

Improve polymorphism: support polymorphic methods (rather than only global functions).

Support in-line functions and/or lambda forms. Tricky.

Improve support for `for`: the environments aren't properly nested as they should be.

Don't crash on this: buffer.set_view_start_line(-1);

### Client/server

Allow a client to just disconnect.
  This is currently hard because the server doesn't know which client issued the commands it processes.

## Misc

Support variables scoped at the Editor level (i.e. not specific to a given buffer).

Don't do any blocking operations.

## Display

Consider using unicode watches to display duration, using exponential/logarithmic growth.

Make the widgets work closer to nethack or similar command-line games.

* `wrap_long_lines`:
  * Make `j` and `k` scroll within the line.

Display the state of bool variables set different than their default values?

Highlight the token under the cursor:
* If we're in a syntax tree, highlight other occurrences of the same token.

### Markdown

Handle links better:

* Have a "render" view (probably with a better name) that aims to display the
  "final" view of a file (rather than its source code). For links, this would
  hide the file and syntax tokens. It would also have effects in other syntax
  elements (e.g., bold, titles...).

### Syntax

Correctly handle: '\000'

Support more languages:
  - Python
  - Directory listings.
    - Perhaps have a bool that toggles 'stat' (show data about files)?
      Ideally this is done by the extensions!

## Editing

Have an auto-save mode? Perhaps don't save to the file but to a log that can then be replayed?

Add structures for English editing:
- Sentence clause. Initially probably just based on punctuation, ideally would be smart enough to parse the sentence, lol.

For operation: Add '?' (show modifiers available).

Add "pipe" command: select the region (similar to delete: line, paragraph, buffer...), and then prompt for a command. Pipe the contents of the region to the command, and replace them with the output of the command.

Make the delete buffer history (used by `p` paste) a stack; make it possible to pop.

* Improve reflow:

  * Somehow integrate reflow (logic from `wrap_from_content`) with `reflow` script?

  * On reflow, leave the cursor where it was? This is difficult because the
    transformations API requires that transformations are applied to a specific
    cursor. I suppose we could create a new cursor; set it as active; apply all
    transformations; and then set it as inactive. But then how do we make sure
    that the originally active cursor will be the active cursor? We can't even
    identify it (since all cursors may have moved).

  * In a C++ file, handle multi-line strings better.
      printf("foo bar hey "
             "quux.");

* Improve multiple cursors:

  * Make sure that the section with the actual cursor is shown.

### Backups:

Don't save backups of internal files, perhaps based on a variable.

### Autocomplete

If autocomplete of files doesn't find any matches, attempt a case-insensitive match? If it matches, expand (correcting invalid characters).

For /-based autocomplete, support globbing?

## Navigation

Improve "g", the current behavior is kind of annoying:
  There should be a way (other than pressing it again) to specify if it should ignore space.  Maybe a modifier can do it?

Honor the `margin_columns` variable.

In diff mode, add a link to every file. Following it should take you directly to the file (perhaps based on a variable for the `strip`, similar to the `patch` command).

## Widgets

Improve the bindings used to navigate the widgets.

* Add a key binding that shows a tree map with every single buffer (from the BuffersList). The user can then quickly select which one to go to. Perhaps the weight is proportional to use or time spent in file.
  * Probably worth starting with something very simple/naive and then iterate?
    * Then again, that's what editor variable `buffers_to_show` kinda does?
      * Maybe just make it smarter? Perhaps it should detect that many buffers could be put side-by-side?

* Make hjkl move the active widget up/down/left/right.

## Prompt

When autocompletion of files fails, have a fail back:
  A second TAB that doesn't advance should do a deeper search: search in every path on which we have an open file.
  Or perhaps do a case insensitive search.

When the prompt doesn't fit the screen, be smarter about what part to show? If it has a cursor, whatever is closest?

Standardize the colorization of prompts (based on prediction), rather than having each prompt implement its own coloring?

For `:` (vm command), improve the highlighter: enable autocompletion with a predictor that looks up available commands (similarish to `/`).

Fix problem with first read of prompt history (at start); doesn't refresh list of matches. [p:20]

Automatically save prompt buffers after a few seconds of inactivity.

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

When follow_end_of_file is on: When the cursor lands over a file and thus a new buffer is shown, don't move the cursor to end of file.

## Directory view

Automatically adjust the width of the view to fit the screen.

## Readability

## Testing

### Fuzz testing

* Add a parameter with a file with input to deliver to the editor at start up. When starting, before running the main loop, read that as a file, and feed it to the editor.

* Maybe add a flag that triggers that the editor exits #x ms after the main loop starts?

* Ideally, it runs with full terminal. Maybe add a parameter that instructs it to create a fake terminal and write to it? We'll need to figure out how to redirect that output to /dev/null. Perhaps it can fork a separate process that reads from it?

## Variables

Detect which variables have actually been assigned to (or, even better, which ones have a value different than their default) and only save those (in the state files).

Add command line options to set values for editor variables.

Add command line options to set default values for buffer variables. Figure out what to do when they conflict with the values from the persist state.

## VM

LineNumber and ColumnNumber types should be exposed to extensions?

Add support for templates, so that we can do "vector<string>".

Improve polymorphism: support polymorphic methods (rather than only global functions).

Improve in-line functions and/or lambda forms. Right now the body binds the entire containing environment. Instead, I suppose it'd be more compatible with C++ to only capture explicitly specified values.

Improve support for `for`: the environments aren't properly nested as they should be.

Allow extensions to define classes or structures.

### VM Integration

Make aC honor cursors (select based on cursors, similar to search). [p:10]

### Client/server

## Bindings

* Improve repetitions handling: `a2g` should be equivalent to `2ag`

* Make a list of all keys and brainstorm bindings that could be given to them.

## Misc

If a file is open in a nonexistant directory, ... when the file is saved, consider creating the directory (perhaps based on a variable). Also, warn the user that the directory doesn't exist.

Add a command-line flag that, when running as a client, causes paths to be resolved by the server, rather than as an absolute path in the client. This allows the server to apply EDGE_PATH and such.

Don't do any blocking operations.

The first five times Edge runs, it should display a `help` message, perhaps at the top of the shell, and also in the SetStatus buffer.

Improve the help document:

* Add documentation string for VM functions/variables.

When a file doesn't exist, don't attempt to run clang-tidy on it.

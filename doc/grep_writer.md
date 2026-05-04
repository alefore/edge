# Grep Writer

## Introduction

The Grep Writer is an extension to edit the output of a `grep -n` command
and commit those modifications to the respective files.

This should make it easy to do large (but simple) refactors.

## Ideal User Interface

Modifying lines in a buffer that holds a `grep -n` view
should modify them directly in corresponding buffers (in memory).
Saving the buffer would save all modified buffers.
Saving those buffers should trigger all post-save behaviors
(e.g., clang-format).

## Current Implementation

Right now the user must run vm code `grep_writer::Save()` explicitly
to commit to disk any changes to the Grep Writer buffer.

## Next steps

* Add a listener to OpenBuffer::Save events.
  Saving the Grep buffer should automatically trigger execution of `Save()`.
  This requires:

  * Extending OpenBuffer to support these listeners.
    Figure out how to integrate these listeners with
    `options_.get_save_callback`.

  * Figuring out how to install these listeners.
    Maybe when src/log_model detects that the output is in grep-n format?

* It would good to remember which lines have actual modifications
  and only "save" those.

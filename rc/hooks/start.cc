#include "../editor_commands/camelcase.cc"
#include "../editor_commands/fold-next-line.cc"
#include "../editor_commands/git.cc"
#include "../editor_commands/header.cc"
#include "../editor_commands/include.cc"
#include "../editor_commands/indent.cc"
#include "../editor_commands/lib/csv.cc"
#include "../editor_commands/lib/dates.cc"
#include "../editor_commands/lib/languages/es.cc"
#include "../editor_commands/lib/markdown.cc"
#include "../editor_commands/lib/numbers.cc"
#include "../editor_commands/lib/paths.cc"
#include "../editor_commands/lib/strings.cc"
#include "../editor_commands/lib/zk.cc"
#include "../editor_commands/reflow.cc"
#include "../editor_commands/shapes.cc"

////////////////////////////////////////////////////////////////////////////////
// Handlers
////////////////////////////////////////////////////////////////////////////////

void OnReload(Buffer buffer) {
  if (buffer.path() == "") {
    string command = BaseCommand(SkipInitialSpaces(buffer.command()));
    // Interactive commands that get a full pts. This must happen here (rather
    // than in buffer-first-enter.cc) so that the pts information is set before
    // the command is actually spawned.
    if (command == "bash" || command == "python" || command == "python3" ||
        command == "watch" || command == "sh" || command == "gdb" ||
        command == "fish")
      buffer.set_pts(true);
  }
}

////////////////////////////////////////////////////////////////////////////////
// Cursors
////////////////////////////////////////////////////////////////////////////////

string GetPathMetadata(string path) { return ""; }

////////////////////////////////////////////////////////////////////////////////
// Cursors
////////////////////////////////////////////////////////////////////////////////

editor.AddBinding("+", "Cursors: Create a new cursor at the current position.",
                  editor.CreateCursor);
editor.AddBinding("-", "Cursors: Destroy current cursor(s) and jump to next.",
                  editor.DestroyCursor);
editor.AddBinding("_",
                  "Cursors: Toggles whether operations apply to all cursors.",
                  []() -> void {
                    editor.ForEachActiveBuffer([](Buffer buffer) -> void {
                      buffer.set_multiple_cursors(!buffer.multiple_cursors());
                    });
                  });
editor.AddBinding("=", "Cursors: Destroy cursors other than the current one.",
                  editor.DestroyOtherCursors);
editor.AddBinding("Ct",
                  "Cursors: Toggles the active cursors with the previous set.",
                  editor.ToggleActiveCursors);
editor.AddBinding("C+", "Cursors: Pushes the active cursors to the stack.",
                  editor.PushActiveCursors);
editor.AddBinding("C-", "Cursors: Pops active cursors from the stack.",
                  editor.PopActiveCursors);
editor.AddBinding("C!",
                  "Cursors: Set active cursors to the marks on this buffer.",
                  editor.SetActiveCursorsToMarks);

void CenterScreenAroundCurrentLine(Buffer buffer) {
  if (buffer.pts()) return;
  // TODO(easy): Fix this. Requires defining `screen` in EditorState, which it
  // currently isn't. Or, alternatively, loading this file later than at
  // construction of EditorState. Ugh.
  number size = 80;  // screen.lines();
  size--;            // The status line doesn't count.
  number line = buffer.position().line();
  number start_line = line - size / 2;
  if (start_line < 0) {
    buffer.SetStatus("Near beginning of file.");
    start_line = 0;
  } else if (start_line + size > buffer.line_count()) {
    buffer.SetStatus("Near end of file.");
    start_line = (buffer.line_count() > size ? buffer.line_count() - size : 0);
  }
  buffer.set_view_start(LineColumn(start_line, 0));
}

editor.AddBinding("M", "Center the screen around the current line.",
                  []() -> void {
                    editor.ForEachActiveBuffer(CenterScreenAroundCurrentLine);
                  });

////////////////////////////////////////////////////////////////////////////////
// Frames / widget manipulation
////////////////////////////////////////////////////////////////////////////////

editor.AddBinding("ah", "Frames: Move to the previous buffer", []() -> void {
  editor.AdvanceActiveBuffer(-editor.pop_repetitions());
});
editor.AddBinding("al", "Frames: Move to the next buffer", []() -> void {
  editor.AdvanceActiveBuffer(editor.pop_repetitions());
});
editor.AddBinding("ag", "Frames: Set the active buffer",
                  []() -> void { editor.EnterSetBufferMode(); });
editor.AddBinding("aO", "Frames: Toggle the buffer sort order", []() -> void {
  if (editor.buffer_sort_order() == "last_visit")
    editor.set_buffer_sort_order("name");
  else
    editor.set_buffer_sort_order("last_visit");
  editor.SetStatus("Sort order: " + editor.buffer_sort_order());
});
editor.AddBinding("r", "Frames: Set the active buffer",
                  []() -> void { editor.EnterSetBufferMode(); });

////////////////////////////////////////////////////////////////////////////////
// Buffers manipulation (saving, reloading...)
////////////////////////////////////////////////////////////////////////////////

editor.AddBinding("ar", "Buffers: Reload the current buffer.", []() -> void {
  editor.ForEachActiveBufferWithRepetitions(
      [](Buffer buffer) -> void { buffer.Reload(); });
});

editor.AddBinding(
    "ae", "Buffers: stops writing to a subprocess (effectively sending EOF).",
    []() -> void {
      editor.ForEachActiveBufferWithRepetitions(
          [](Buffer buffer) -> void { buffer.SendEndOfFileToProcess(); });
    });

editor.AddBinding("aw", "Buffers: Save the current buffer.", []() -> void {
  editor.ForEachActiveBufferWithRepetitions(
      [](Buffer buffer) -> void { buffer.Save(); });
});

editor.AddBinding("ad", "Buffers: Close the current buffer.", []() -> void {
  editor.ForEachActiveBufferWithRepetitions(
      [](Buffer buffer) -> void { buffer.Close(); });
});

editor.AddBinding("ss", "Run a shell in the directory of the current buffer.",
                  []() -> void {
                    editor.ForEachActiveBuffer([](Buffer buffer) -> void {
                      ForkCommandOptions options;
                      options.set_command("sh -l");
                      string path = buffer.path();
                      if (!path.empty()) {
                        path = Dirname(path);
                        options.set_children_path(path);
                      }
                      options.set_insertion_type("visit");
                      options.set_name("💻 shell");
                      editor.ForkCommand(options).SetStatus("Children path: " +
                                                            path);
                    });
                  });

editor.AddBinding("sh", "Buffers: Navigate to the header / implementation.",
                  []() -> void { editor.ForEachActiveBuffer(ShowHeader); });

////////////////////////////////////////////////////////////////////////////////
// Editing commands
////////////////////////////////////////////////////////////////////////////////

editor.AddBinding(".", "Edit: Repeats the last command.",
                  editor.RepeatLastTransformation);

editor.AddBinding(
    terminal_backspace, "Edit: Delete previous character.", []() -> void {
      editor.ForEachActiveBuffer([](Buffer buffer) -> void {
        buffer.ApplyTransformation(
            DeleteTransformationBuilder()
                .set_modifiers(
                    Modifiers().set_backwards().set_delete_behavior(true))
                .build());
      });
    });

void DeleteCurrentLine(Buffer buffer) {
  buffer.PushTransformationStack();
  buffer.ApplyTransformation(SetColumnTransformation(0));
  buffer.ApplyTransformation(
      DeleteTransformationBuilder()
          .set_modifiers(Modifiers()
                             .set_line()
                             .set_repetitions(editor.repetitions())
                             .set_boundary_end_neighbor()
                             .set_delete_behavior(true))
          .build());
  buffer.PopTransformationStack();
}

editor.AddBinding("K", "Edit: Delete the current line", []() -> void {
  editor.ForEachActiveBuffer(DeleteCurrentLine);
  editor.pop_repetitions();
});

editor.AddBinding(
    "J", "Edit: Fold next line into the current line", []() -> void {
      editor.ForEachActiveBuffer([](Buffer buffer) -> void {
        buffer.ApplyTransformation(FunctionTransformation(
            [](TransformationInput input) -> TransformationOutput {
              return FoldNextLine(buffer, input);
            }));
      });
    });

editor.AddBinding(
    "`", "Edit: Add/remove ticks around current section.", []() -> void {
      editor.ForEachActiveBuffer([](Buffer buffer) -> void {
        buffer.ApplyTransformation(FunctionTransformation(
            [](TransformationInput input) -> TransformationOutput {
              // TODO: Instead of FindSymbol{Begin,End}, do something based on
              // the current modifier.
              //
              // TODO: If a tick was already present at both positions, delete
              // (rather than insert).
              //
              // TODO: If a tick was already present at only one position, don't
              // insert there?
              auto start = FindSymbolBegin(buffer, input.position());
              auto end = FindSymbolEnd(buffer, input.position());
              return TransformationOutput()
                  .push(SetPositionTransformation(end))
                  .push(InsertTransformationBuilder().set_text("`").build())
                  .push(SetPositionTransformation(start))
                  .push(InsertTransformationBuilder().set_text("`").build())
                  .push(SetPositionTransformation(LineColumn(
                      input.position().line(), input.position().column() - 1)));
            }));
      });
    });

editor.AddBinding("#", "Edit: Reflow current paragraph",
                  []() -> void { editor.ForEachActiveBuffer(Reflow); });

editor.AddBinding(
    terminal_control_k, "Edit: Delete to end of line.", []() -> void {
      editor.ForEachActiveBuffer([](Buffer buffer) -> void {
        buffer.ApplyTransformation(
            DeleteTransformationBuilder()
                .set_modifiers(Modifiers().set_line().set_delete_behavior(true))
                .build());
      });
    });

void HandleKeyboardControlU(Buffer buffer) {
  buffer.PushTransformationStack();
  Modifiers modifiers;
  modifiers.set_backwards();
  modifiers.set_delete_behavior(true);
  if (buffer.contents_type() == "path") {
    LineColumn position = buffer.position();
    string line = buffer.line(position.line());
    number column = position.column();
    if (column > 1 && line.substr(column - 1, 1) == "/") {
      column--;
    }
    if (column == 0) {
      return;
    }
    number last_slash = line.find_last_of("/", min(column - 1, line.size()));
    if (last_slash == -1) {
      modifiers.set_line();
    } else {
      modifiers.set_repetitions(position.column() - last_slash - 1);
    }
  } else {
    // Edit: Delete to the beginning of line.
    modifiers.set_line();
  }
  buffer.ApplyTransformation(
      DeleteTransformationBuilder().set_modifiers(modifiers).build());
  buffer.PopTransformationStack();
}

editor.AddBinding(terminal_control_u, "Edit: Delete the current line",
                  []() -> void {
                    editor.ForEachActiveBuffer(HandleKeyboardControlU);
                  });

void IncrementNumber(number direction) {
  number delta = direction * editor.pop_repetitions();
  editor.ForEachActiveBuffer(
      [](Buffer buffer) -> void { AddToIntegerTransformation(buffer, delta); });
  return;
}

editor.AddBinding("s+", "Numbers: Increment the number under the cursor.",
                  []() -> void { IncrementNumber(1); });
editor.AddBinding("s-", "Numbers: Decrement the number under the cursor.",
                  []() -> void { IncrementNumber(-1); });

void GoToBeginningOfLine() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.ApplyTransformation(SetColumnTransformation(0));
  });
}

void GoToEndOfLine() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.ApplyTransformation(SetColumnTransformation(999999999999));
  });
}

editor.AddBinding("^", "Go to the beginning of the current line",
                  GoToBeginningOfLine);
editor.AddBinding(terminal_control_a,
                  "Navigate: Move to the beginning of line.",
                  GoToBeginningOfLine);

editor.AddBinding(
    "Cc", "Edit: Adjust identifier to or from CamelCase.", []() -> void {
      editor.ForEachActiveBuffer([](Buffer buffer) -> void {
        buffer.ApplyTransformation(FunctionTransformation(
            [](TransformationInput input) -> TransformationOutput {
              return CamelCaseTransformation(buffer, input);
            }));
      });
    });

editor.AddBinding(terminal_control_d, "Edit: Delete current character.",
                  []() -> void {
                    editor.ForEachActiveBuffer([](Buffer buffer) -> void {
                      buffer.ApplyTransformation(
                          DeleteTransformationBuilder()
                              .set_modifiers(
                                  Modifiers().set_delete_behavior(true))
                              .build());
                    });
                  });

editor.AddBinding("$", "Go to the end of the current line", GoToEndOfLine);
editor.AddBinding(terminal_control_e, "Navigate: Move to the end of line.",
                  GoToEndOfLine);

editor.AddBinding("si",
                  "Edit: Indent the current line to the cursor's position.",
                  []() -> void { editor.ForEachActiveBuffer(Indent); });
editor.AddBinding("sI", "Edit: Add a `#include` directive.",
                  []() -> void { editor.ForEachActiveBuffer(AddIncludeLine); });

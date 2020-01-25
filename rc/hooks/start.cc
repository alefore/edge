#include "../editor_commands/lib/numbers"

AddBinding(".", "Edit: Repeats the last command.",
           editor.RepeatLastTransformation);
AddBinding("+", "Cursors: Create a new cursor at the current position.",
           editor.CreateCursor);
AddBinding("-", "Cursors: Destroy current cursor(s) and jump to next.",
           editor.DestroyCursor);
AddBinding("=", "Cursors: Destroy cursors other than the current one.",
           editor.DestroyOtherCursors);
AddBinding("Ct", "Cursors: Toggles the active cursors with the previous set.",
           editor.ToggleActiveCursors);
AddBinding("C+", "Cursors: Pushes the active cursors to the stack.",
           editor.PushActiveCursors);
AddBinding("C-", "Cursors: Pops active cursors from the stack.",
           editor.PopActiveCursors);
AddBinding("C!", "Cursors: Set active cursors to the marks on this buffer.",
           editor.SetActiveCursorsToMarks);

AddBinding(terminal_backspace, "Edit: Delete previous character.",
           []() -> void {
             editor.ForEachActiveBuffer([](Buffer buffer) -> void {
               buffer.ApplyTransformation(
                   DeleteTransformationBuilder()
                       .set_modifiers(Modifiers().set_backwards())
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
                             .set_repetitions(repetitions())
                             .set_boundary_end_neighbor())
          .build());

  buffer.PopTransformationStack();
  set_repetitions(1);
}

AddBinding("K", "Edit: Delete the current line",
           []() -> void { editor.ForEachActiveBuffer(DeleteCurrentLine); });

AddBinding(terminal_control_k, "Edit: Delete to end of line.", []() -> void {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.ApplyTransformation(DeleteTransformationBuilder()
                                   .set_modifiers(Modifiers().set_line())
                                   .build());
  });
});

AddBinding(terminal_control_u, "Edit: Delete the current line", []() -> void {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.PushTransformationStack();
    Modifiers modifiers = Modifiers();
    modifiers.set_backwards();
    if (buffer.contents_type() == "path") {
      LineColumn position = buffer.position();
      string line = buffer.line(position.line());
      int column = position.column();
      if (column > 1 && line.substr(column - 1, 1) == "/") {
        column--;
      }
      if (column == 0) {
        return;
      }
      int last_slash = line.find_last_of("/", min(column - 1, line.size()));
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
  });
});

// Logic to handle the tree of visible buffers.
AddBinding("a=", "Frames: Zoom to the active leaf", editor.ZoomToLeaf);
AddBinding("ah", "Frames: Move to the previous buffer", []() -> void {
  editor.AdvanceActiveBuffer(-repetitions());
  set_repetitions(1);
});
AddBinding("al", "Frames: Move to the next buffer", []() -> void {
  editor.AdvanceActiveBuffer(repetitions());
  set_repetitions(1);
});
AddBinding("ak", "Frames: Move to the previous active leaf", []() -> void {
  editor.AdvanceActiveLeaf(-repetitions());
  set_repetitions(1);
});
AddBinding("aj", "Frames: Move to the next active leaf", []() -> void {
  editor.AdvanceActiveLeaf(repetitions());
  set_repetitions(1);
});
AddBinding("ag", "Frames: Set the active buffer (by repetitions)",
           []() -> void {
             editor.SetActiveBuffer(repetitions() - 1);
             set_repetitions(1);
           });
AddBinding("a+j", "Frames: Add a horizontal split", editor.AddHorizontalSplit);
AddBinding("a+l", "Frames: Add a vertical split", editor.AddVerticalSplit);
AddBinding("aR", "Frames: Show all open buffers",
           editor.SetHorizontalSplitsWithAllBuffers);

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

AddBinding("^", "Go to the beginning of the current line", GoToBeginningOfLine);
AddBinding(terminal_control_a, "Navigate: Move to the beginning of line.",
           GoToBeginningOfLine);

AddBinding(terminal_control_d, "Edit: Delete current character.", []() -> void {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.ApplyTransformation(DeleteTransformationBuilder().build());
  });
});

AddBinding("$", "Go to the end of the current line", GoToEndOfLine);
AddBinding(terminal_control_e, "Navigate: Move to the end of line.",
           GoToEndOfLine);

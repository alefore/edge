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

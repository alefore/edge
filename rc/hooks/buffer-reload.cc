// buffer-reload.cc: Prepare for a buffer being reload.
//
// This program mainly sets several buffer variables depending on properties
// of the buffer (such as the extension of the file being loaded).

#include "../editor_commands/cpp-mode"
#include "../editor_commands/java-mode"
#include "../editor_commands/lib/clang-format"
#include "../editor_commands/lib/numbers"
#include "../editor_commands/lib/paths"
#include "../editor_commands/lib/strings"

// Optimizes the buffer for visualizing a patch (output of a `diff` command).
void DiffMode() { buffer.set_tree_parser("diff"); }

void GoToBeginningOfLine() {
  buffer.set_position(LineColumn(buffer.position().line(), 0));
}

void GoToEndOfLine() {
  int current_line = buffer.position().line();
  buffer.set_position(
      LineColumn(buffer.position().line(), buffer.line(current_line).size()));
}

void DeleteCurrentLine() {
  buffer.PushTransformationStack();
  buffer.ApplyTransformation(TransformationGoToColumn(0));

  Modifiers modifiers = Modifiers();
  modifiers.set_line();
  modifiers.set_repetitions(repetitions());
  modifiers.set_boundary_end_neighbor();
  buffer.ApplyTransformation(TransformationDelete(modifiers));

  buffer.PopTransformationStack();
  set_repetitions(1);
}

void CenterScreenAroundCurrentLine() {
  int size = screen.lines();
  size--;  // The status line doesn't count.
  int line = buffer.position().line();
  int start_line = line - size / 2;
  if (start_line < 0) {
    SetStatus("Near beginning of file.");
    start_line = 0;
  } else if (start_line + size > buffer.line_count()) {
    SetStatus("Near end of file.");
    start_line = (buffer.line_count() > size ? buffer.line_count() - size : 0);
  }
  // buffer.set_view_start_line(start_line);
}

buffer.set_editor_commands_path("~/.edge/editor_commands/");

void HandleFileTypes(string basename, string extension) {
  if (extension == "cc" || extension == "h" || extension == "c") {
    CppMode();
    buffer.AddBindingToFile("sh", buffer.editor_commands_path() + "header");
    buffer.AddBindingToFile("sI", buffer.editor_commands_path() + "include");
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    SetStatus("🔡 C++ file (" + extension + ")");
    return;
  }

  if (extension == "sh") {
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    SetStatus("🔡 Shell script (" + extension + ")");
  }

  if (extension == "java") {
    JavaMode();
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    buffer.AddBindingToFile("sR", buffer.editor_commands_path() + "reflow");
    SetStatus("🔡 Java file (" + extension + ")");
    return;
  }

  if (basename == "COMMIT_EDITMSG") {
    buffer.set_position(LineColumn(0, 0));
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    buffer.AddBindingToFile("sR", buffer.editor_commands_path() + "reflow");
    SetStatus("🔡 Git commit message");
    return;
  }

  if (extension == "py") {
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    SetStatus("🔡 Python file (" + extension + ")");
    return;
  }

  if (extension == "txt" || extension == "md") {
    buffer.set_wrap_from_content(true);
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    buffer.AddBindingToFile("sR", buffer.editor_commands_path() + "reflow");
  }

  if (extension == "md") {
    buffer.set_tree_parser("md");
    buffer.set_paragraph_line_prefix_characters("*-# ");
    buffer.set_line_prefix_characters(" ");
    SetStatus("🔡 Markdown file (" + extension + ")");
  }
}

string path = buffer.path();
if (path == "") {
  // If path is empty, this buffer is running a command.
  string command = buffer.command();
  if (command != "") {
    buffer.set_paste_mode(true);
  }

  command = SkipInitialSpaces(command);
  string base_command = BaseCommand(command);
  if (base_command != "") {
    if (base_command == "bash" || base_command == "python" ||
        base_command == "sh" || base_command == "gdb") {
      // These are interactive commands, that get a full pts.
      buffer.set_pts(true);
      buffer.set_follow_end_of_file(true);
      buffer.set_buffer_list_context_lines(5);
      if (base_command == "bash" || base_command == "sh") {
        // If the user deletes the buffer, we send SIGTERM to it and wait for
        // the shell to exit. If the shell is currently running a process, it
        // will simply ignore the signal.
        buffer.set_term_on_close(true);
      }
    } else if (base_command == "make") {
      buffer.set_contains_line_marks(true);
      buffer.set_reload_on_buffer_write(true);
      buffer.set_follow_end_of_file(true);
      buffer.set_buffer_list_context_lines(5);
    } else if (base_command == "grep") {
      buffer.set_contains_line_marks(true);
      buffer.set_allow_dirty_delete(true);
    } else if (base_command == "clang-format") {
      buffer.set_show_in_buffers_list(true);
      buffer.set_close_after_clean_exit(true);
      buffer.set_allow_dirty_delete(true);
    } else if (base_command == "diff") {
      DiffMode();
    } else if (base_command == "git") {
      string next = BaseCommand(SkipInitialSpaces(command.substr(
          base_command.size(), command.size() - base_command.size())));
      if (next == "diff") {
        DiffMode();
      }
    } else {
      buffer.set_follow_end_of_file(buffer.pts());
    }
    buffer.set_atomic_lines(false);
    buffer.set_reload_on_enter(false);
  }
} else {
  // If path is non-empty, this buffer is loading a file.
  int dot = path.find_last_of(".", path.size());
  string extension =
      dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);
  string basename = Basename(path);

  buffer.AddBinding("^", "Go to the beginning of the current line",
                    GoToBeginningOfLine);
  buffer.AddBinding("$", "Go to the end of the current line", GoToEndOfLine);
  buffer.AddBindingToFile("J",
                          buffer.editor_commands_path() + "fold-next-line");
  buffer.AddBinding("K", "Delete the current line", DeleteCurrentLine);
  buffer.AddBindingToFile("#", buffer.editor_commands_path() + "reflow");

  buffer.set_typos("overriden");

  HandleFileTypes(basename, extension);
}

if (!buffer.pts()) {
  buffer.AddBinding("M", "Center the screen around the current line.",
                    CenterScreenAroundCurrentLine);
}

// Logic to handle the tree of visible buffers.
void RewindActiveBuffer() {
  editor.AdvanceActiveBuffer(-repetitions());
  set_repetitions(1);
}
void AdvanceActiveBuffer() {
  editor.AdvanceActiveBuffer(repetitions());
  set_repetitions(1);
}
void SetActiveBuffer() {
  editor.SetActiveBuffer(repetitions() - 1);
  set_repetitions(1);
}
void RewindActiveLeaf() {
  editor.AdvanceActiveLeaf(-repetitions());
  set_repetitions(1);
}
void AdvanceActiveLeaf() {
  editor.AdvanceActiveLeaf(repetitions());
  set_repetitions(1);
}

buffer.AddBinding("a=", "Frames: Zoom to the active leaf", editor.ZoomToLeaf);
buffer.AddBinding("ah", "Frames: Move to the previous buffer",
                  RewindActiveBuffer);
buffer.AddBinding("al", "Frames: Move to the next buffer", AdvanceActiveBuffer);
buffer.AddBinding("ak", "Frames: Move to the previous active leaf",
                  RewindActiveLeaf);
buffer.AddBinding("aj", "Frames: Move to the next active leaf",
                  AdvanceActiveLeaf);
buffer.AddBinding("ag", "Frames: Set the active buffer (by repetitions)",
                  SetActiveBuffer);
buffer.AddBinding("a+j", "Frames: Add a horizontal split",
                  editor.AddHorizontalSplit);
buffer.AddBinding("a+l", "Frames: Add a horizontal split",
                  editor.AddVerticalSplit);

// buffer.AddBinding("aR", "Frames: Show all open buffers",
//                  editor.SetHorizontalSplitsWithAllBuffers);

void IncrementNumber() {
  AddToIntegerAtPosition(buffer.position(), repetitions());
  set_repetitions(1);
}
void DecrementNumber() {
  AddToIntegerAtPosition(buffer.position(), -repetitions());
  set_repetitions(1);
}

buffer.AddBinding("sl", "Numbers: Increment the number under the cursor.",
                  IncrementNumber);
buffer.AddBinding("sh", "Numbers: Decrement the number under the cursor.",
                  DecrementNumber);

void RunLocalShell() {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command("sh -l");
  string path = buffer.path();
  if (!path.empty()) {
    path = Dirname(path);
    SetStatus("Children path: " + path);
    options.set_children_path(path);
  }
  options.set_insertion_type("visit");
  ForkCommand(options);
}

buffer.AddBinding("ss", "Run a shell in the directory of the current buffer.",
                  RunLocalShell);

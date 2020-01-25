// buffer-reload.cc: Prepare for a buffer being reload.
//
// This program mainly sets several buffer variables depending on properties
// of the buffer (such as the extension of the file being loaded).

#include "../editor_commands/camelcase.cc"
#include "../editor_commands/compiler"
#include "../editor_commands/cpp-mode"
#include "../editor_commands/java-mode"
#include "../editor_commands/lib/clang-format"
#include "../editor_commands/lib/numbers"
#include "../editor_commands/lib/paths"
#include "../editor_commands/lib/strings"
#include "../editor_commands/prompt-context.cc"

// Optimizes the buffer for visualizing a patch (output of a `diff` command).
void DiffMode() { buffer.set_tree_parser("diff"); }

void CenterScreenAroundCurrentLine() {
  int size = screen.lines();
  size--;  // The status line doesn't count.
  int line = buffer.position().line();
  int start_line = line - size / 2;
  if (start_line < 0) {
    buffer.SetStatus("Near beginning of file.");
    start_line = 0;
  } else if (start_line + size > buffer.line_count()) {
    buffer.SetStatus("Near end of file.");
    start_line = (buffer.line_count() > size ? buffer.line_count() - size : 0);
  }
  // buffer.set_view_start_line(start_line);
}

buffer.set_editor_commands_path("~/.edge/editor_commands/");

void HandleFileTypes(string basename, string extension) {
  if (extension == "cc" || extension == "h" || extension == "c" ||
      extension == "cpp") {
    CppMode();
    buffer.AddBindingToFile("sh", buffer.editor_commands_path() + "header");
    buffer.AddBindingToFile("sI", buffer.editor_commands_path() + "include");
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    buffer.SetStatus("🔡 C++ file (" + extension + ")");
    return;
  }

  if (extension == "sh") {
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    buffer.SetStatus("🔡 Shell script (" + extension + ")");
  }

  if (extension == "java") {
    JavaMode();
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    buffer.AddBindingToFile("sR", buffer.editor_commands_path() + "reflow");
    buffer.SetStatus("🔡 Java file (" + extension + ")");
    return;
  }

  if (basename == "COMMIT_EDITMSG") {
    buffer.ApplyTransformation(SetPositionTransformation(LineColumn(0, 0)));
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    buffer.AddBindingToFile("sR", buffer.editor_commands_path() + "reflow");
    buffer.SetStatus("🔡 Git commit message");
    return;
  }

  if (extension == "py") {
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    buffer.SetStatus("🔡 Python file (" + extension + ")");
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
    buffer.SetStatus("🔡 Markdown file (" + extension + ")");
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
    } else if (base_command == "git" || base_command == "hg") {
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

  buffer.AddBindingToFile("#", buffer.editor_commands_path() + "reflow");

  buffer.set_typos("overriden optoins");

  HandleFileTypes(basename, extension);
}

buffer.AddBindingToFile("J", buffer.editor_commands_path() + "fold-next-line");

if (!buffer.pts()) {
  buffer.AddBinding("M", "Center the screen around the current line.",
                    CenterScreenAroundCurrentLine);
}

buffer.AddBinding("_",
                  "Cursors: Toggles whether operations apply to all cursors.",
                  []() -> void {
                    buffer.set_multiple_cursors(!buffer.multiple_cursors());
                  });
buffer.AddBinding("ar", "Buffers: Reload the current buffer.", buffer.Reload);
buffer.AddBinding("aw", "Buffers: Save the current buffer.", buffer.Save);

void IncrementNumber() {
  AddToIntegerTransformation(buffer, repetitions());
  set_repetitions(1);
}
void DecrementNumber() {
  AddToIntegerTransformation(buffer, -repetitions());
  set_repetitions(1);
}

buffer.AddBinding("s+", "Numbers: Increment the number under the cursor.",
                  IncrementNumber);
buffer.AddBinding("s-", "Numbers: Decrement the number under the cursor.",
                  DecrementNumber);

void Camel() {
  buffer.ApplyTransformation(FunctionTransformation(CamelCaseTransformation));
}

buffer.AddBinding("Cc", "Edit: Adjust identifier to or from CamelCase.", Camel);

void RunLocalShell() {
  auto options = ForkCommandOptions();
  options.set_command("sh -l");
  string path = buffer.path();
  if (!path.empty()) {
    path = Dirname(path);
    options.set_children_path(path);
  }
  options.set_insertion_type("visit");
  options.set_name("💻 shell");
  ForkCommand(options).SetStatus("Children path: " + path);
}

buffer.AddBinding("ss", "Run a shell in the directory of the current buffer.",
                  RunLocalShell);

#include "../editor_commands/cpp-mode"
#include "../editor_commands/java-mode"
#include "../editor_commands/lib/clang-format"
#include "../editor_commands/lib/paths"
#include "../editor_commands/lib/strings"

string ProcessCCInputLine(string line) {
  SetStatus("Got: " + line);
  return line;
}

void DiffMode() { buffer.set_tree_parser("diff"); }

string BaseCommand(string command) {
  int space = command.find_first_of(" ", 0);
  return space == -1 ? command : command.substr(0, space);
}

buffer.set_editor_commands_path("~/.edge/editor_commands/");

// It would be ideal to not have to do this, but since currently all cursors
// but the main one get reshuffled on reload, it's probably best to just remove
// them.
if (!buffer.reload_on_display()) {
  editor.DestroyOtherCursors();
}

string path = buffer.path();
if (path == "") {
  string command = buffer.command();
  if (command != "") {
    buffer.set_paste_mode(true);
  }

  command = SkipInitialSpaces(command);
  string base_command = BaseCommand(command);
  if (base_command != "") {
    if (base_command == "bash" || base_command == "python" ||
        base_command == "sh") {
      buffer.set_pts(true);
      buffer.set_follow_end_of_file(true);
      buffer.set_buffer_list_context_lines(5);
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
  int dot = path.find_last_of(".", path.size());
  string extension =
      dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);
  string basename = Basename(path);

  buffer.AddBindingToFile("J",
                          buffer.editor_commands_path() + "fold-next-line");

  buffer.set_typos("overriden");

  if (extension == "cc" || extension == "h" || extension == "c") {
    CppMode();
    buffer.AddBindingToFile("sh", buffer.editor_commands_path() + "header");
    buffer.AddBindingToFile("sI", buffer.editor_commands_path() + "include");
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    buffer.AddBindingToFile("sR", buffer.editor_commands_path() + "reflow");
    SetStatus("Loaded C file (" + extension + ")");
    return;
  }

  if (extension == "java") {
    JavaMode();
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    buffer.AddBindingToFile("sR", buffer.editor_commands_path() + "reflow");
    SetStatus("Loaded Java file (" + extension + ")");
    return;
  }

  if (basename == "COMMIT_EDITMSG") {
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    SetStatus("GIT commit msg");
    buffer.AddBindingToFile("sR", buffer.editor_commands_path() + "reflow");
  }

  if (extension == "py") {
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    SetStatus("Loaded Python file (" + extension + ")");
  }

  if (extension == "md") {
    buffer.set_tree_parser("md");
    buffer.AddBindingToFile("si", buffer.editor_commands_path() + "indent");
    buffer.AddBindingToFile("sR", buffer.editor_commands_path() + "reflow");
    buffer.set_paragraph_line_prefix_characters("*-# ");
    buffer.set_line_prefix_characters(" ");
    SetStatus("Loaded Markdown file (" + extension + ")");
  }
}

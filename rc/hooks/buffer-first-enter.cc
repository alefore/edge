// buffer-reload.cc: Prepare for a buffer being reload.
//
// This program mainly sets several buffer variables depending on properties
// of the buffer (such as the extension of the file being loaded).

#include "../editor_commands/compiler.cc"
#include "../editor_commands/cpp-mode.cc"
#include "../editor_commands/java-mode.cc"
#include "../editor_commands/lib/clang-format.cc"
#include "../editor_commands/lib/numbers.cc"
#include "../editor_commands/lib/paths.cc"
#include "../editor_commands/lib/strings.cc"
#include "../editor_commands/prompt-context.cc"

// Optimizes the buffer for visualizing a patch (output of a `diff` command).
void DiffMode() { buffer.set_tree_parser("diff"); }

buffer.set_editor_commands_path("~/.edge/editor_commands/");

if (buffer.buffer_list_context_lines() < 5) {
  buffer.set_buffer_list_context_lines(5);
}

void AddContextExtension(string ext) {
  if (buffer.file_context_extensions().find(ext, 0) == -1) {
    buffer.set_file_context_extensions(buffer.file_context_extensions() + " " +
                                       ext);
  }
}

void HandleFileTypes(Buffer buffer, string basename, string extension) {
  if (extension == "cc" || extension == "h" || extension == "c" ||
      extension == "cpp") {
    CppMode(buffer);
    buffer.SetStatus("🔡 C++ file (" + extension + ")");
    buffer.set_completion_model_paths("format/cpp iso-639/en");
    return;
  }

  if (extension == "sh") {
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    buffer.SetStatus("🔡 Shell script (" + extension + ")");
  }

  if (extension == "java") {
    JavaMode(buffer);
    buffer.SetStatus("🔡 Java file (" + extension + ")");
    return;
  }

  if (basename == "COMMIT_EDITMSG") {
    buffer.ApplyTransformation(SetPositionTransformation(LineColumn(0, 0)));
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    buffer.set_trigger_reload_on_buffer_write(false);
    buffer.SetStatus("🔡 Git commit message");
    return;
  }

  if (extension == "py") {
    buffer.set_paragraph_line_prefix_characters(" #");
    buffer.set_line_prefix_characters(" #");
    buffer.set_completion_model_paths("format/py iso-639/en");
    buffer.SetStatus("🔡 Python file (" + extension + ")");
    return;
  }

  if (extension == "sql" || extension == "sqlm") {
    buffer.set_paragraph_line_prefix_characters(" -");
    buffer.set_line_prefix_characters(" #");
    buffer.set_completion_model_paths("format/sql iso-639/en");
    buffer.SetStatus("🔡 SQL file (" + extension + ")");
    return;
  }

  if (extension == "csv") {
    csv::Enable(buffer);
    return;
  }

  if (extension == "txt" || extension == "md") {
    buffer.set_wrap_from_content(true);
    AddContextExtension("md");
    AddContextExtension("txt");
  }

  if (extension == "md") {
    buffer.set_tree_parser("md");
    buffer.set_paragraph_line_prefix_characters("*-# ");
    buffer.set_line_prefix_characters("> ");
    buffer.set_buffers_list_preview_follows_cursor(false);
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
        base_command == "python3" || base_command == "watch" ||
        base_command == "sh" || base_command == "gdb" ||
        base_command == "fish") {
      // Interactive commands that get a full pts (in buffer-reload.cc).
      buffer.set_follow_end_of_file(true);
      buffer.set_buffer_list_context_lines(5);
      if (base_command == "bash" || base_command == "sh") {
        // If the user deletes the buffer, we send SIGTERM to it and wait for
        // the shell to exit. If the shell is currently running a process, it
        // will simply ignore the signal.
        buffer.set_term_on_close(true);
        buffer.set_allow_dirty_delete(true);
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
  number dot = path.find_last_of(".", path.size());
  string extension =
      dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);
  string basename = Basename(path);

  buffer.set_typos("overriden optoins");

  HandleFileTypes(buffer, basename, extension);

  if (path.find("/zettelkasten", 0) != -1 &&
      buffer.cpp_prompt_namespaces().find("zettelkasten", 0) == -1) {
    buffer.set_cpp_prompt_namespaces(buffer.cpp_prompt_namespaces() +
                                     " zettelkasten");
  }
}

#include "paths.cc"

string path = buffer.path();
bool clang_format = false;
string reformat_command = "";
string reformat_command_in_place = "";

void ClangFormatOnSave() {
  if (!clang_format) {
    return;
  }
  buffer.SetStatus(reformat_command_in_place + " " + path.shell_escape() +
                   " ...");
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command(reformat_command_in_place + " " + path.shell_escape() +
                      "; edge --run 'Buffer original_buffer = " +
                      "editor.OpenFile(\"" + path.shell_escape() +
                      "\", false); original_buffer.Reload(); "
                      "original_buffer.SetStatus(\"clang-reformat 🗸\");'");
  options.set_insertion_type("ignore");
  Buffer clang_buffer = editor.ForkCommand(options);

  // We deliberately wait, in case other hooks want to execute further commands
  // on the file: we'd like those commands to get the updated (reformatted)
  // contents.
  //
  // TODO(P1, 2023-09-11): Turns out this was causing crashes somewhere in VM.
  // Perhaps waiting until the reload makes some references in the VM evaluation
  // expire?
  // clang_buffer.WaitForEndOfFile();
}

void ClangFormatToggle() {
  clang_format = !clang_format;
  buffer.SetStatus((clang_format ? "🗸" : "⛶") + " clang-format");
  if (reformat_command == "") {
    reformat_command = "clang-format";
    reformat_command_in_place = reformat_command + " -i ";
  }
}

string extension = Extension(path);

if (extension == "cc" || extension == "h" || extension == "cpp" ||
    extension == "java" || extension == "js") {
  reformat_command = "clang-format";
  reformat_command_in_place = "clang-format -i";
} else if (extension == "sql" || extension == "sqlt" || extension == "sqlm") {
  reformat_command = "~/bin/format_sql <";
  reformat_command_in_place = "~/bin/format_sql -in_place";
} else if (extension == "py") {
  reformat_command = "~/bin/format_py <";
  reformat_command_in_place = "~/bin/format_py -i";
} else if (Basename(path) == "BUILD") {
  reformat_command = "buildifier <";
  reformat_command_in_place = "buildifier";
}

if (reformat_command != "") {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command(
      "test ! -f " + path.shell_escape() + "||" + reformat_command + " " +
      path.shell_escape() + "| diff " + path.shell_escape() +
      " /dev/stdin > /tmp/edge-clang-format-diff-log " +
      "|| edge --run 'editor.OpenFile(\"'" + path.shell_escape() +
      "'\", false).SetWarningStatus(\"clang-format: File is not properly "
      "formatted.\");'");
  options.set_insertion_type("ignore");
  editor.ForkCommand(options);
}
if (reformat_command_in_place != "") {
  ClangFormatToggle();
}

buffer.AddBinding("sC", "clang_format = !clang_format", ClangFormatToggle);

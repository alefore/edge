#include "paths.cc"

namespace clang_format {
namespace internal {
// We use a VectorInt as a boxed boolean: the value is true if the vector is
// non-empty.
bool GetBool(VectorInt bool_box) { return !bool_box.empty(); }

bool ToggleAndGet(VectorInt bool_box) {
  if (bool_box.empty())
    bool_box.push_back(0);
  else
    bool_box.erase(0);
  return GetBool(bool_box);
}

void ClangFormatOnSave(Buffer clang_buffer, VectorInt clang_format_enabled,
                       string reformat_command_in_place) {
  if (!GetBool(clang_format_enabled)) return;
  string path = clang_buffer.path();
  clang_buffer.SetStatus(reformat_command_in_place + " " + path.shell_escape() +
                         " ...");
  RunCommandOptions options;
  options.set_command(reformat_command_in_place + " " + path.shell_escape() +
                      "; edge --run 'Buffer original_buffer = " +
                      "editor.OpenFile(\"" + path.shell_escape() +
                      "\", false); original_buffer.Reload(); "
                      "original_buffer.SetStatus(\"clang-reformat 🗸\");'");
  options.set_insertion_type("ignore");
  Buffer clang_output_buffer = editor.RunCommand(options);

  // We deliberately wait, in case other hooks want to execute further commands
  // on the file: we'd like those commands to get the updated (reformatted)
  // contents.
  //
  // TODO(P1, 2023-09-11): Turns out this was causing crashes somewhere in VM.
  // Perhaps waiting until the reload makes some references in the VM evaluation
  // expire?
  // clang_output_buffer.WaitForEndOfFile();
}

void ClangFormatToggle(Buffer clang_buffer, VectorInt clang_format_enabled) {
  clang_buffer.SetStatus((ToggleAndGet(clang_format_enabled) ? "🗸" : "⛶") +
                         " clang-format");
}
}  // namespace internal

void Install(Buffer clang_buffer) {
  string path = clang_buffer.path();
  VectorInt clang_format_enabled;
  string reformat_command = "";
  string reformat_command_in_place = "";

  string extension = Extension(path);

  if (extension == "cc" || extension == "h" || extension == "cpp" ||
      extension == "java" || extension == "js" || extension == "ts") {
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
    RunCommandOptions options;
    options.set_command(
        "test ! -f " + path.shell_escape() + "||" + reformat_command + " " +
        path.shell_escape() + "| diff " + path.shell_escape() +
        " /dev/stdin > /tmp/edge-clang-format-diff-log " +
        "|| edge --run 'editor.OpenFile(\"'" + path.shell_escape() +
        "'\", false).SetWarningStatus(\"clang-format: File is not properly "
        "formatted.\");'");
    options.set_insertion_type("ignore");
    editor.RunCommand(options);
  }
  if (reformat_command_in_place != "")
    internal::ClangFormatToggle(clang_buffer, clang_format_enabled);
  clang_buffer.AddBinding("sC", "clang_format = !clang_format", []() -> void {
    internal::ClangFormatToggle(clang_buffer, clang_format_enabled);
  });
  clang_buffer.AddSaveHook("ClangFormat", []() -> void {
    internal::ClangFormatOnSave(clang_buffer, clang_format_enabled,
                                reformat_command_in_place);
  });
}

void Install() {
  editor.ForEachActiveBuffer(
      [](Buffer clang_buffer) -> void { Install(clang_buffer); });
}
}  // namespace clang_format

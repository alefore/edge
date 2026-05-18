namespace py_mode {
namespace internal {
void RunMyPy(string path) {
  // We deliberately won't shell_escape `mypy` so that the home directory gets
  // expanded.
  string mypy = "~/bin/mypy";
  RunCommandOptions mypy_options;
  mypy_options.set_command("test ! -x " + mypy + " || " + mypy + " " +
                           path.shell_escape());
  mypy_options.set_insertion_type("ignore");
  Buffer mypy_buffer = editor.RunCommand(mypy_options);
  mypy_buffer.WaitForEndOfFile();

  // TODO: Would be better to just insert buffer, somehow. Without having to
  // re-run it. But I guess we don't currenlty have a mechanism to do that.
  if (mypy_buffer.child_exit_status() != 0) {
    mypy_options.set_insertion_type("visit");
    editor.RunCommand(mypy_options).set_allow_dirty_delete(true);
  }
}
}  // namespace internal

void Install(Buffer buffer) {
  buffer.set_paragraph_line_prefix_characters(" #");
  buffer.set_line_prefix_characters(" #");
  buffer.set_language_keywords(
      "None True False "
      "and or not "
      "with a "
      "import from "
      "assert "
      "async await yield "
      "for while break continue "
      "lambda return "
      "class def global nonlocal "
      "if elif else "
      "try catch except finally raise "
      "is in "
      "int bool float complex str list tuple dict set ");
  buffer.set_tree_parser("py");
  clang_format::Install(buffer);
  buffer.AddSaveHook("MyPy",
                     []() -> void { internal::RunMyPy(buffer.path()); });
}
}  // namespace py_mode

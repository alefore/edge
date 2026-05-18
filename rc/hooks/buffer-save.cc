#include "../editor_commands/lib/paths.cc"

string path = buffer.path();
number dot = path.find_last_of(".", path.size());
string extension = dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);

string git_push_path = Dirname(path) + "/.edge-git-push.txt";
RunCommandOptions git_push_options;
git_push_options.set_command("test ! -f " + git_push_path.shell_escape() +
                             " || ( git commit -a -m \"$( cat " +
                             git_push_path.shell_escape() +
                             ")\" && git push "
                             "|| edge --run 'editor.OpenFile(\"'" +
                             path.shell_escape() +
                             "'\", false).SetWarningStatus(\"git-push failed."
                             " See /tmp/edge-git-push.log\");'"
                             ") >/tmp/edge-git-push.log 2>&1");
git_push_options.set_insertion_type("ignore");
editor.RunCommand(git_push_options);

if (Extension(path) == "py") {
  // We deliberately won't escape `mypy` so that the home directory gets
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

// if (path.starts_with("/home/alejo/edge/src")) {
//   RunCommandOptions options;
//   options.set_command("make -j3");
//   options.set_insertion_type("only_list");
//   RunCommand(options);
// }

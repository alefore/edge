#include "lib/paths.cc"

namespace git {
namespace internal {
void MaybeCommitAndPush(string path) {
  number dot = path.find_last_of(".", path.size());
  string extension =
      dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);

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
}
}  // namespace internal

void CommitAll(string message) {
  RunCommandOptions options;
  options.set_command("git commit -a" +
                      (message.empty() ? "" : " -m " + message.shell_escape()));
  options.set_insertion_type("visit");
  editor.RunCommand(options);
}

void Install(Buffer target_buffer) {
  target_buffer.AddSaveHook("GitCommitAndPush", []() -> void {
    internal::MaybeCommitAndPush(target_buffer.path());
  });
}
}  // namespace git

#include "../editor_commands/lib/paths.cc"

string path = buffer.path();
int dot = path.find_last_of(".", path.size());
string extension = dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);

ClangFormatOnSave();

string git_push_path = Dirname(path) + "/.edge-git-push.txt";
ForkCommandOptions git_push_options = ForkCommandOptions();
git_push_options.set_command("test ! -f " + git_push_path.shell_escape() +
                             " || ( git commit -a -m \"$( cat " +
                             git_push_path.shell_escape() +
                             ")\" && git push ) >/tmp/edge-git-push.log 2>&1");
git_push_options.set_insertion_type("ignore");
editor.ForkCommand(git_push_options);

// if (path.starts_with("/home/alejo/edge/src")) {
//   ForkCommandOptions options = ForkCommandOptions();
//   options.set_command("make -j3");
//   options.set_insertion_type("only_list");
//   ForkCommand(options);
// }

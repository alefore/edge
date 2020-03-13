#include "paths.cc"

void zkls() {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command("~/bin/zkls");
  options.set_insertion_type("visit");
  ForkCommand(options);
}

void zkrev() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    string path = Basename(buffer.path());
    if (path == "") return;
    ForkCommandOptions options = ForkCommandOptions();
    options.set_command("grep " + path.shell_escape() + " ???.md");
    options.set_insertion_type("visit");
    ForkCommand(options);
  });
  return;
}

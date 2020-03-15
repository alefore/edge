// This file contains functions that I use to manage my Zettelkasten.

#include "paths.cc"

void zkRunCommand(string name, string command) {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command(command);
  options.set_insertion_type("visit");
  options.set_name("zk: " + name);
  ForkCommand(options);
}

void zkls() { zkRunCommand("ls", "~/bin/zkls"); }

void zkrev() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    string path = Basename(buffer.path());
    if (path == "") return;
    zkRunCommand("rev: " + path, "grep " + path.shell_escape() + " ???.md");
  });
  return;
}

// Produce the index.
void zki() { OpenFile("index.md", true); }

void zks(string query) {
  zkRunCommand("s: " + query, "grep " + query.shell_escape() + " ???.md");
}

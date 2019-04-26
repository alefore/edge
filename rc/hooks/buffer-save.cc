string path = buffer.path();
int dot = path.find_last_of(".", path.size());
string extension = dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);

ClangFormatOnSave();

if (path.starts_with("/home/alejo/edge/src")) {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command("make -j3");
  options.set_insertion_type("skip");
  ForkCommand(options);
}

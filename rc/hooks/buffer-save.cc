string path = buffer.path();
int dot = path.find_last_of(".", path.size());
string extension = dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);

ClangFormatOnSave();

if (path.starts_with("/home/alejo/edge/src")) {
  ForkCommand("make -j3", false);
}

string path = buffer.path();
int dot = path.find_last_of(".", path.size());
string extension = dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);

if (extension == "cc" || extension == "h") {
  ForkCommand("clang-format -i " + path, false);
}

if (path.starts_with("/home/alejo/edge/src")) {
  ForkCommand("make -j3", false);
}

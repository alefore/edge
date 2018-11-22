string path = buffer.path();
int dot = path.find_last_of(".", path.size());
string extension = dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);

if (extension == "cc" || extension == "h" || extension == "java") {
  ForkCommand("clang-format -i " + path.shell_escape() +
                  "; sleep 1 | edge --run 'OpenFile(\"" + path.shell_escape() +
                  "\"); CurrentBuffer().Reload();'",
              false);
}

if (path.starts_with("/home/alejo/edge/src")) {
  ForkCommand("make -j3", false);
}

string Basename(string path) {
  int last_slash = path.find_last_of("/", path.size());
  if (last_slash == -1) {
    return path;
  }
  return path.substr(last_slash + 1, path.size() - (last_slash + 1));
}

string Dirname(string path) {
  int last_slash = path.find_last_of("/", path.size());
  if (last_slash == -1) {
    return path;
  }
  return path.substr(0, last_slash);
}

string Extension(string path) {
  int last_dot = path.find_last_of(".", path.size());
  if (last_dot == -1) {
    return "";
  }
  return path.substr(last_dot + 1, path.size() - (last_dot + 1));
}

string path = buffer.path();

int dot = path.find_last_of(".", path.size());

string ProcessCCInputLine(string line) {
  SetStatus("Got: " + line);
  return line;
}

string Basename(string path) {
  int last_slash = path.find_last_of("/", path.size());
  if (last_slash == -1) {
    return path;
  }
  return path.substr(last_slash + 1, path.size() - (last_slash + 1));
}

buffer.set_editor_commands_path("~/.edge/editor_commands/");

string extension = dot == -1 ? "" : path.substr(dot + 1, path.size() - dot - 1);
string basename = Basename(path);
if (extension == "cc" || extension == "h") {
  buffer.set_line_prefix_characters(" /");
  //buffer.set_input_line_processor(ProcessCCInputLine);
  SetStatus("Loaded C file (" + extension + ")");
  return;
}

if (basename == "COMMIT_EDITMSG") {
  buffer.set_line_prefix_characters(" #");
  SetStatus("GIT commit msg");
}

if (extension == "py") {
  buffer.set_editor_commands_path("~/.edge/editor_commands/");
  buffer.set_line_prefix_characters(" #");
  SetStatus("Loaded Python file (" + extension + ")");
}

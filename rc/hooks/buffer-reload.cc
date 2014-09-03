string path = buffer.path();

int dot = path.find_last_of(".", path.size());

if (dot == -1) {
  SetStatus("Unable to find extension.");
  return;
}

string extension = path.substr(dot + 1, path.size() - dot - 1);
if (extension == "cc" || extension == "h") {
  buffer.set_editor_commands_path("~/.edge/editor_commands/");
  buffer.set_line_prefix_characters(" /");
  SetStatus("Loaded C file (" + extension + ")");
}

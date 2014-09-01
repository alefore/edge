string path = buffer.path();
int dot = path.find_last_of(".", path.size());
if (dot == -1) {
  SetStatus("Unable to find extension.");
} else {
  string extension = path.substr(dot + 1, path.size() - dot - 1);
  if (extension.empty()) {
    SetStatus("");
  } else if (extension == "cc" || extension == "h") {
    buffer.set_editor_commands_path("/home/alejo/.edge/editor_commands/");
    SetStatus("Loaded C file (" + extension + ")");
  } else {
    SetStatus("Unrecognized extension: " + extension);
  }
}

namespace grep_writer {
namespace internal {
// Parses the line into a vector with 3 elements: the path, line number,
// content. If the line can't be parsed, returns an empty vector.
VectorString ParseLine(string line) {
  VectorString output;
  number first_colon = line.find_first_of(":", 0);
  if (first_colon == -1) return output;
  number second_colon = line.find_first_of(":", first_colon + 1);
  if (second_colon == -1) return output;
  output.push_back(line.substr(0, first_colon));
  output.push_back(
      line.substr(first_colon + 1, second_colon - (first_colon + 1)));
  output.push_back(
      line.substr(second_colon + 1, line.size() - (second_colon + 1)));
  return output;
}

void ApplyLine(string content_to_parse) {
  VectorString line_struct = ParseLine(content_to_parse);
  if (line_struct.size() != 3) {
    Error("GrepWriter: Unable to parse line");
    return;
  }
  string path = line_struct.get(0);
  number line_number = line_struct.get(1).toint() - 1;
  string content = line_struct.get(2);

  if (line_number < 0) {
    Error("GrepWriter: Received invalid line number");
    return;
  }

  bool visit = false;
  Buffer buffer = editor.OpenFile(path, visit);
  buffer.WaitForEndOfFile();

  if (line_number > buffer.line_count()) {
    // TODO(2026-05-04, P1): Show more details in the error.
    Error("GrepWriter: Received line past buffer size");
    return;
  }

  // Delete the old line.
  buffer.ApplyTransformation(
      SetPositionTransformation(LineColumn(line_number, 0)));
  buffer.ApplyTransformation(DeleteTransformationBuilder()
                                 .set_modifiers(Modifiers().set_repetitions(
                                     buffer.line(line_number).size()))
                                 .build());
  buffer.ApplyTransformation(
      InsertTransformationBuilder().set_text(content).build());
}

void Save(Buffer buffer) {
  buffer.ForEach(
      [](number num, string content) -> void { ApplyLine(content); });

  SetString paths_to_save;
  buffer.ForEach([](number num, string content) -> void {
    VectorString line_struct = ParseLine(content);
    if (line_struct.size() != 3) return;
    paths_to_save.insert(line_struct.get(0));
  });
  VectorString paths_to_save_vector;
  paths_to_save.ForEach(
      [](string path) -> void { paths_to_save_vector.push_back(path); });
  editor.OpenFile(paths_to_save_vector, false)
      .ForEach([](Buffer buffer) -> void {
        buffer.WaitForEndOfFile();
        buffer.Save();
      });
}
}  // namespace internal

void Save() {
  editor.ForEachActiveBuffer(
      [](Buffer buffer) -> void { internal::Save(buffer); });
}
}  // namespace grep_writer

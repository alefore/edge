string ExtractToken(Buffer buffer, string text) {
  int pos_start = text.find_first_not_of(buffer.line_prefix_characters(), 0);
  if (pos_start == -1) {
    return text;
  }
  int pos_end = text.find(" ", pos_start);
  if (pos_end == -1) {
    return text;
  }
  return text.substr(0, pos_end);
}

string LeadingPrefix(Buffer buffer, string text) {
  string prefix = ExtractToken(buffer, text);
  int pos_end = prefix.find_first_not_of(buffer.line_prefix_characters(), 0);
  if (pos_end == -1) {
    return prefix;
  } else {
    return prefix.substr(0, pos_end);
  }
}

TransformationOutput FoldNextLine(Buffer buffer, TransformationInput input) {
  TransformationOutput output = TransformationOutput();
  int column = buffer.line(input.position().line()).size();
  for (int i = 0; i < editor.repetitions(); i++) {
    output.push(SetColumnTransformation(column));
    int next_line = input.position().line() + i + 1;
    if (next_line < buffer.line_count()) {
      auto prefix_size = LeadingPrefix(buffer, buffer.line(next_line)).size();
      output.push(
          DeleteTransformationBuilder()
              .set_modifiers(
                  Modifiers().set_paste_buffer_behavior(false).set_repetitions(
                      (1 + prefix_size)))
              .build());
      if (buffer.line(next_line).size() > prefix_size) {
        column += buffer.line(next_line).size() - prefix_size + 1;
        output.push(InsertTransformationBuilder().set_text(" ").build());
      }
    }
  }
  editor.set_repetitions(1);
  return output;
}

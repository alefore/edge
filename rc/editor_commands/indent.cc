// Removes all spaces at the beginning of the current line and then inserts
// enough spaces to leave the beginning of the line at the point the cursor was
// (before any spaces were removed).
//
// This uses the line_prefix_characters variable to figure out where the line
// actually begins.

// Return the position (column) at which the prefix of the current line ends.
number GetBeginningOfCurrentLine(Buffer buffer) {
  string line = buffer.line(buffer.position().line());
  number column = line.find_first_not_of(buffer.line_prefix_characters(), 0);
  return column == -1 ? line.size() : column;
}

void InsertSpacesAtBeginningOfLine(Buffer buffer) {
  number line = buffer.position().line();

  number desired_column = buffer.position().column();
  number start_column = GetBeginningOfCurrentLine(buffer);

  buffer.ApplyTransformation(SetColumnTransformation(0));

  if (start_column > desired_column) {
    buffer.ApplyTransformation(DeleteTransformationBuilder()
                                   .set_modifiers(Modifiers().set_repetitions(
                                       start_column - desired_column))
                                   .build());
  }

  while (start_column < desired_column) {
    buffer.ApplyTransformation(
        InsertTransformationBuilder().set_text(" ").build());
    start_column = start_column + 1;
  }

  buffer.set_position(LineColumn(line, desired_column));
}

void Indent(Buffer buffer) {
  buffer.PushTransformationStack();
  InsertSpacesAtBeginningOfLine(buffer);
  buffer.PopTransformationStack();
}

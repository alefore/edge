// TODO(easy): Switch this to the FunctionTransformation interface.
void AddIncludeLine(Buffer buffer) {
  LineColumn position = buffer.position();
  string line = buffer.line(position.line());
  SetStatus(line);

  if (line == "#include <>") {
    buffer.ApplyTransformation(
        SetPositionTransformation(LineColumn(position.line(), 9)));
    buffer.ApplyTransformation(
        DeleteTransformationBuilder()
            .set_modifiers(Modifiers().set_repetitions(2))
            .build());
    buffer.ApplyTransformation(
        InsertTransformationBuilder().set_text("\"\"").build());
    buffer.ApplyTransformation(
        SetPositionTransformation(LineColumn(position.line(), 10)));
  } else if (line == "#include \"\"") {
    buffer.ApplyTransformation(
        SetPositionTransformation(LineColumn(position.line(), 9)));
    buffer.ApplyTransformation(
        DeleteTransformationBuilder()
            .set_modifiers(Modifiers().set_repetitions(2))
            .build());
    buffer.ApplyTransformation(
        InsertTransformationBuilder().set_text("<>").build());
    buffer.ApplyTransformation(
        SetPositionTransformation(LineColumn(position.line(), 10)));
  } else {
    buffer.ApplyTransformation(
        SetPositionTransformation(LineColumn(position.line(), 0)));
    buffer.ApplyTransformation(
        InsertTransformationBuilder().set_text("#include <>\n").build());
    buffer.ApplyTransformation(
        SetPositionTransformation(LineColumn(position.line(), 10)));
  }
}

Buffer OutputBuffer(string path) {
  Buffer output = editor.OpenFile(path, true).WaitForEndOfFile();
  output.ApplyTransformation(SetPositionTransformation(LineColumn(0, 0)));
  output.ApplyTransformation(
      DeleteTransformationBuilder()
          // TODO: Add `set_buffer` and use that?
          .set_modifiers(Modifiers().set_line().set_repetitions(9999999))
          .build());
  return output;
}

void OutputBufferLog(Buffer log, string text) {
  log.ApplyTransformation(
      InsertTransformationBuilder()
          .set_text(Now().format("%F %T") + ": " + text + "\n")
          .build());
}

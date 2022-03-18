void Today() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.ApplyTransformation(FunctionTransformation(
        [](TransformationInput input) -> TransformationOutput {
          return TransformationOutput().push(
              InsertTransformationBuilder()
                  .set_text(Now().format("%Y-%m-%d"))
                  .build());
        }));
  });
}

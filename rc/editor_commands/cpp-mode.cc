string cpp_build_command = "";

void build() {
  if (cpp_build_command == "") {
    editor.SetStatus(
        "Error: Must assign a non-empty value to variable `cpp_build_command`");
    return;
  }
  buffer.SetStatus("Run: " + cpp_build_command);

  string path = buffer.path().shell_escape();
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command(
      "if " + cpp_build_command + "; then " +
      "edge --run 'Buffer build_buffer = " +
      "editor.OpenFile(\"" + path + "\", false); " +
      "build_buffer.SetStatus(\"🗸 build success\");';" + " else " +
      "STATUS=$?;" + "edge --run 'Buffer build_buffer = " +
      "editor.OpenFile(\"" + path +
      "\", false); " + "build_buffer.SetStatus(\"💥 build fail\");';" +
      "exit $STATUS;" + "fi");
  options.set_insertion_type("only_list");
  options.set_name("build: " + cpp_build_command);
  Buffer build_buffer = editor.ForkCommand(options);
  SetAsCompiler(build_buffer);
}

TransformationOutput StdMoveTransformation(TransformationInput input,
                                           string text_to_insert,
                                           number delta_from_end) {
  return TransformationOutput()
      .push(InsertTransformationBuilder().set_text(text_to_insert).build())
      .push(SetColumnTransformation(input.position().column() +
                                    text_to_insert.size() - delta_from_end));
}

void RegisterSimpleCppBinding(Buffer buffer, string keys, string text_to_insert,
                              number delta_from_end) {
  buffer.AddBinding(keys, "C++: Insert `" + text_to_insert + "`", []() -> void {
    buffer.ApplyTransformation(FunctionTransformation(
        [](TransformationInput input) -> TransformationOutput {
          return StdMoveTransformation(input, text_to_insert, delta_from_end);
        }));
  });
}

void CppMode(Buffer buffer) {
  buffer.set_paragraph_line_prefix_characters(" /*");
  buffer.set_line_prefix_characters(" /*");
  buffer.set_language_keywords(
      "static extern override virtual "
      "class struct enum private public protected "
      "using typedef namespace "
      "sizeof "
      "static_cast dynamic_cast "
      "delete new "
      // Flow control.
      "switch case default "
      "if else "
      "for while do "
      "break continue "
      "return "
      // Types
      "void const mutable auto "
      "unique_ptr shared_ptr optional "
      "std function vector list "
      "map unordered_map set unordered_set "
      "int double float string wstring bool char "
      "size_t "
      // Values
      "true false nullptr NULL");
  buffer.set_tree_parser("cpp");

  RegisterSimpleCppBinding(buffer, "Sm", "std::move()", 1);
  RegisterSimpleCppBinding(buffer, "So", "std::optional<>", 1);
  RegisterSimpleCppBinding(buffer, "Su", "std::unique_ptr<>", 1);
  RegisterSimpleCppBinding(buffer, "Ss", "std::shared_ptr<>", 1);
  RegisterSimpleCppBinding(buffer, "Sf", "std::function<()>", 2);
}

void PyMode(Buffer buffer) {
  buffer.set_paragraph_line_prefix_characters(" /*");
  buffer.set_line_prefix_characters(" /*");
  buffer.set_language_keywords(
      "None True False "
      "and or not "
      "with a "
      "import from "
      "assert "
      "async await yield "
      "for while break continue "
      "lambda return "
      "class def global nonlocal "
      "if elif else "
      "try catch except finally raise "
      "is in "
      "int bool float complex str list tuple dict set ");
  buffer.set_tree_parser("py");
}

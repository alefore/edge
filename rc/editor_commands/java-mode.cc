void JavaMode(Buffer buffer) {
  buffer.set_paragraph_line_prefix_characters(" /*");
  buffer.set_line_prefix_characters(" /*");
  buffer.set_line_width(100);
  buffer.set_language_keywords(
      "class interface extends implements this new "
      "public private protected static "
      "final "
      "void "
      // Flow control.
      "switch case default "
      "if else "
      "for while do "
      "break continue "
      "return "
      // Types
      "double long int String Object Integer Boolean Double "
      // Values
      "true false null");
  buffer.set_tree_parser("cpp");
}
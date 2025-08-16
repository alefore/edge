void JavaMode(Buffer buffer) {
  buffer.set_paragraph_line_prefix_characters(" /*");
  buffer.set_line_prefix_characters(" /*");
  buffer.set_line_width(100);
  buffer.set_language_keywords(
      "class interface extends implements this new "
      "super instanceof abstract "
      "public private protected static "
      "package import enum "
      "final "
      "void "
      // Flow control.
      "switch case default "
      "if else "
      "for while do "
      "break continue "
      "return "
      "try catch finally throws throw "
      "synchronized "
      // Types
      "double long int boolean byte short char float "
      "String Object Integer Boolean Double "
      // Values
      "true false null");
  buffer.set_tree_parser("java");
}

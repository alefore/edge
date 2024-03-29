// Reflows the current paragraph, based on variables `line_prefix_characters`
// and `paragraph_line_prefix_characters`, to make each line (save for the last
// one) as long as possible but shorter than the value of variable `line_width`.

#include "lib/strings.cc"

// Should words longer than `line_width` be broken? If false, we'll only break
// lines at spaces (never breaking a word). If true, we'll never let lines
// exceed the desired `line_width`.
bool break_words = false;

// Folds into the current line all lines in the current paragraph (according to
// LineHasPrefix). The end result is that the current line will contain the
// entire paragraph (probably being far larger than `buffer.line_width()`).
void FoldNextLineWhilePrefixIs(Buffer buffer, string prefix) {
  editor.SetStatus("Folding paragraph into a single line.");
  number line = buffer.position().line();
  bool first_line = true;
  while (line + 1 < buffer.line_count() &&
         LineHasPrefix(buffer, prefix, line + 1) &&
         (first_line || GetPrefix(buffer.line(line + 1),
                                  buffer.paragraph_line_prefix_characters()) ==
                            GetPrefix(buffer.line(line + 1),
                                      buffer.line_prefix_characters()))) {
    buffer.ApplyTransformation(
        SetPositionTransformation(LineColumn(line, buffer.line(line).size())));
    if (buffer.position().column() > prefix.size()) {
      // Avoid a space in the first line:
      buffer.ApplyTransformation(
          InsertTransformationBuilder().set_text(" ").build());
    }
    number prefix_to_delete =
        buffer.line(line + 1).find_first_not_of(" ", prefix.size());
    buffer.ApplyTransformation(
        DeleteTransformationBuilder()
            .set_modifiers(
                Modifiers().set_paste_buffer_behavior(false).set_repetitions(
                    1 + prefix_to_delete))
            .build());
    first_line = false;
  }
}

// Deletes length characters starting at start and breaks the line.
void BreakAt(Buffer buffer, string prefix, number start, number length) {
  buffer.ApplyTransformation(
      SetPositionTransformation(LineColumn(buffer.position().line(), start)));
  buffer.ApplyTransformation(
      DeleteTransformationBuilder()
          .set_modifiers(
              Modifiers().set_paste_buffer_behavior(false).set_repetitions(
                  length))
          .build());
  buffer.ApplyTransformation(
      InsertTransformationBuilder().set_text("\n" + prefix).build());
}

void BreakLine(Buffer buffer, string prefix, number line_width) {
  editor.SetStatus("Breaking line by line width: " + line_width.tostring());
  while (buffer.line(buffer.position().line()).size() > line_width) {
    string s = buffer.line(buffer.position().line());

    // The last space before line_width.
    number last_space = s.find_last_of(" ", line_width);
    if (last_space != -1 && last_space > prefix.size()) {
      // We were able to find a space after the prefix.
      //
      // Find the last non-space character preceeding it.
      number last_char = s.find_last_not_of(" ", last_space);
      if (last_char == -1) {
        editor.SetStatus("Giving up: couldn't find start of break.");
        return;
      }
      BreakAt(buffer, prefix, last_char + 1, last_space - last_char);
    } else {  // No space found after the prefix and before line_width.
      // Find the first non-space character after line_width.
      number break_at =
          break_words ? line_width : s.find_first_of(" ", line_width);
      if (break_at == -1) {
        editor.SetStatus("We're done: No space remains.");
        return;
      }
      number next_char = s.find_first_of(buffer.symbol_characters(), break_at);
      if (next_char == -1) {
        editor.SetStatus("We're done: Only spaces now.");
        return;
      }
      BreakAt(buffer, prefix, break_at, next_char - break_at);
    }
  }
}

void Reflow(Buffer buffer) {
  buffer.PushTransformationStack();

  string prefix = GetPrefix(buffer.line(buffer.position().line()),
                            buffer.line_prefix_characters());

  ScrollBackToBeginningOfParagraph(buffer, prefix);
  LineColumn starting_position = buffer.position();

  // This is in case the first line in the paragraph has spaces that should be
  // removed.
  buffer.ApplyTransformation(SetPositionTransformation(
      LineColumn(starting_position.line(), prefix.size())));
  buffer.ApplyTransformation(
      InsertTransformationBuilder().set_text("\n" + prefix).build());
  buffer.ApplyTransformation(SetPositionTransformation(starting_position));

  FoldNextLineWhilePrefixIs(buffer, prefix);
  buffer.ApplyTransformation(SetPositionTransformation(starting_position));

  BreakLine(buffer, prefix, buffer.line_width());
  buffer.ApplyTransformation(SetPositionTransformation(starting_position));

  buffer.PopTransformationStack();
}

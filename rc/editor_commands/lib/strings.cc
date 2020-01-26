string SkipInitialSpaces(string text) {
  int start = text.find_first_not_of(" ", 0);
  return start > 0 ? text.substr(start, text.size() - start) : text;
}

string SkipFinalSpaces(string text) {
  int end = text.find_last_not_of(" ", text.size());
  return end > 0 ? text.substr(0, end + 1) : text;
}

string SkipSpaces(string text) {
  return SkipFinalSpaces(SkipInitialSpaces(text));
}

VectorString BreakWords(string text) {
  VectorString output = VectorString();
  int start = 0;
  while (start < text.size()) {
    start = text.find_first_not_of(" ", start);
    if (start != -1) {
      int word_end = text.find_first_of(" ", start);
      if (word_end == -1) {
        word_end = text.size();
      }
      output.push_back(text.substr(start, word_end - start));
      start = word_end;
    } else {
      start = text.size();
    }
  }
  return output;
}

// Extracts the first argument in a shell command.
//
// For example, `BaseCommand("cp foo.txt bar.txt")` returns `"cp"`.
string BaseCommand(string command) {
  command = SkipInitialSpaces(command);
  int space = command.find_first_of(" ", 0);
  return space == -1 ? command : command.substr(0, space);
}

// Returns a substring of text from the start, up until (excluding) the first
// character not in prefix_characters.
//
// For example,
//     GetPrefix("  // For example,", " /")
// returns
//     "  // ");
string GetPrefix(string text, string prefix_characters) {
  int pos_start = text.find_first_not_of(prefix_characters, 0);
  if (pos_start == -1) {
    return text;
  }
  return text.substr(0, pos_start);
}

// Checks if a given line starts with the prefix. The line must have an
// additional character after the prefix for this to be true (in other words,
// this is false if the line is just equal to the prefix).
bool LineHasPrefix(Buffer buffer, string prefix, int line) {
  string contents = buffer.line(line);
  return contents.starts_with(prefix) && contents.size() > prefix.size();
}

bool LineIsInParagraph(Buffer buffer, string prefix, int line) {
  string contents = buffer.line(line);
  return GetPrefix(contents, buffer.paragraph_line_prefix_characters())
                 .size() >= prefix.size() &&
         contents.size() > prefix.size();
}

// Moves the current position to the first line that follows a line that doesn't
// have a given prefix.
//
// We use `LineIsInParagraph` to allow the first line in the paragraph to have a
// different prefix.
//
// For example, in Markdown, with the cursor at "hey":
//
//   * alejandro
//     forero
//     cuervo
//   * bar
//     foo
//     hey
//
// We want this result:
//
//   * alejandro
//     forero
//     cuervo
//   * bar foo hey
//
// That is: the paragraph begins at "bar", not at "foo" nor at "alejandro".
void ScrollBackToBeginningOfParagraph(Buffer buffer, string prefix) {
  SetStatus("Scrolling back to beginning of paragraph.");
  int line = buffer.position().line();
  if (GetPrefix(buffer.line(line),
                buffer.paragraph_line_prefix_characters()) == prefix) {
    while (line > 0 && LineHasPrefix(buffer, prefix, line - 1)) {
      line--;
    }

    if (line > 0 && LineIsInParagraph(buffer, prefix, line - 1)) {
      line--;
    }
  }
  buffer.ApplyTransformation(SetPositionTransformation(LineColumn(line, 0)));
}

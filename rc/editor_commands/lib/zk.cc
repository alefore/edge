// This file contains functions that I use to manage my Zettelkasten.
//
// The following functions are defined (intended to be executed with `:`):
//
// * zki - Open the index file (index.md)
// * zkls - List all notes (with their title).
// * zkl - Expand the paths under the cursors to a full link.
// * zkln - Create a new entry based on the title under the cursor.

#include "paths.cc"
#include "strings.cc"

string GetNoteTitle(Buffer buffer) {
  auto line = buffer.line(0);
  if (line.substr(0, 1) == "#") {
    line = line.substr(1, line.size() - 1);
  }
  return SkipSpaces(line);
}

string GetNoteTitle(string path) {
  auto buffer = OpenFile(path, false);
  buffer.WaitForEndOfFile();
  return GetNoteTitle(buffer);
}

string ToMarkdownPath(string path) {
  string path_without_extension = path;
  int last_dot = path.find_last_of(".", path.size());
  if (last_dot != -1) {
    return path_without_extension = path.substr(0, last_dot);
  }
  return path_without_extension + ".md";
}

// Returns the path (ID) of the next available (empty) file.
string ZkInternalNextEmpty() {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command(
      "find -size 0b -name '???.md' -printf '%f\n' | sort | head -1");
  options.set_insertion_type("ignore");
  auto buffer = ForkCommand(options);
  buffer.WaitForEndOfFile();
  return buffer.line(0);
}

Buffer zkRunCommand(string name, string command, string insertion_type) {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command(command);
  options.set_insertion_type(insertion_type);
  options.set_name("zk: " + name);
  return ForkCommand(options);
}

void zkls() { zkRunCommand("ls", "~/bin/zkls", "visit"); }

void zkrev() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    string path = Basename(buffer.path());
    if (path == "") return;
    zkRunCommand("rev: " + path, "grep " + path.shell_escape() + " ???.md",
                 "visit");
  });
  return;
}

// Open the index. index.md is expected to be a link to the main entry point.
void zki() { OpenFile("index.md", true); }

void zks(string query) {
  zkRunCommand("s: " + query, "grep -i " + query.shell_escape() + " ???.md",
               "visit");
}

Buffer Previewzks(string query) {
  auto buffer = zkRunCommand(
      "s: " + query, "grep -i " + query.shell_escape() + " ???.md", "ignore");
  buffer.WaitForEndOfFile();
  return buffer;
}

Buffer zkInternalTitleSearch(string query, string insertion_type) {
  auto buffer = zkRunCommand("t: " + query,
                             "awk '{if (tolower($0)~\"" + query.shell_escape() +
                                 "\") print FILENAME, $0; nextfile;}' ???.md",
                             insertion_type);
  buffer.WaitForEndOfFile();
  return buffer;
}

// Receives a string and produces a list of all Zettel that include that string
// in their title.
void zkt(string query) { zkInternalTitleSearch(query, "visit"); }
Buffer Previewzkt(string query) {
  return zkInternalTitleSearch(query, "ignore");
}

// Find the smallest unused ID. This assumes that files are of the form `???.md`
// and are created in advance.
void zkn() { OpenFile(ZkInternalNextEmpty(), true); }

TransformationOutput ZKInternalLink(Buffer buffer, TransformationInput input) {
  auto line = buffer.line(input.position().line());
  auto path_characters = buffer.path_characters();
  int start = line.find_last_not_of(path_characters, input.position().column());
  if (start == -1) {
    start = 0;  // Didn't find, must be at beginning.
  } else {
    start++;
  }
  int end = line.find_first_not_of(path_characters, start);
  if (end == -1) {
    end = line.size();
  }

  string path = line.substr(start, end - start);
  string adjusted_path = ToMarkdownPath(path);
  string title = GetNoteTitle(adjusted_path);
  auto output =
      TransformationOutput()
          .push(SetColumnTransformation(end))
          .push(InsertTransformationBuilder()
                    .set_text((path != adjusted_path ? ".md" : "") + ")")
                    .build())
          .push(SetColumnTransformation(start))
          .push(InsertTransformationBuilder().set_text("[](").build())
          .push(SetColumnTransformation(start + 1))
          .push(InsertTransformationBuilder().set_text(title).build())
          .push(SetColumnTransformation(start + 1 + title.size() + 1 + 1 +
                                        path.size() + 1));
  if (input.position().line() + 1 >= buffer.line_count()) {
    output.push(SetColumnTransformation(99999999))
        .push(InsertTransformationBuilder().set_text("\n").build());
  }
  return output;
}

// Replaces a path (e.g., `03d.md`) with a link to it, extracting the text of
// the link from the first line in the file (e.g. `[Bauhaus](03d.md)`).
void zkl() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.ApplyTransformation(FunctionTransformation(
        [](TransformationInput input) -> TransformationOutput {
          return ZKInternalLink(buffer, input);
        }));
  });
}

TransformationOutput ZKInternalNewLink(Buffer buffer,
                                       TransformationInput input) {
  auto line = buffer.line(input.position().line());
  auto path_characters = buffer.path_characters();
  int start = line.find_last_of("[", input.position().column());
  if (start == -1) {
    start = 0;  // Didn't find, must be at beginning.
  } else {
    start++;
  }
  int end = line.find_first_of("]", start);
  int title_length = 0;
  if (end == -1) {
    end = line.size();
    title_length = end - start;
  } else {
    end++;
    title_length = end - start - 1;
  }

  auto path = ZkInternalNextEmpty();
  string title = line.substr(start, title_length);
  auto output =
      TransformationOutput()
          .push(SetColumnTransformation(end))
          .push(
              InsertTransformationBuilder().set_text("(" + path + ")").build());
  if (input.position().line() + 1 >= buffer.line_count()) {
    output.push(SetColumnTransformation(99999999))
        .push(InsertTransformationBuilder().set_text("\n").build());
  }
  auto new_note = OpenFile(path, true);
  new_note.WaitForEndOfFile();
  new_note.ApplyTransformation(FunctionTransformation(
      [](TransformationInput input) -> TransformationOutput {
        return TransformationOutput()
            .push(InsertTransformationBuilder()
                      .set_text("# " + title + "\n\n\n\nRelated:\n* [" +
                                GetNoteTitle(buffer.path()) + "](" +
                                Basename(buffer.path()) + ")\n")
                      .build())
            .push(SetPositionTransformation(LineColumn(2, 0)));
      }));

  return output;
}

// Turns a text like "[Some Title]" into a link "[Some Title](xxx.md)", where
// xxx.md is the next available (unused) identifier; loads the next note (from
// said identifier) and inserts some initial skeleton into the new file
// (including the title).
void zkln() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.ApplyTransformation(FunctionTransformation(
        [](TransformationInput input) -> TransformationOutput {
          return ZKInternalNewLink(buffer, input);
        }));
  });
}

void zkeRegisterLinks(string line, VectorString output) {
  int column = 0;
  string extension = ".md";
  while (column < line.size()) {
    column = line.find_first_of("(", column);
    if (column == -1) {
      return;
    }
    column++;
    if (line.size() < column + "XXX".size() + extension.size() + ")".size() ||
        line.substr(column + "XXX".size(), extension.size() + 1) !=
            extension + ")") {
      return;
    }
    string path = line.substr(column, "XXX".size() + extension.size());
    column += path.size() + ")".size();
    output.push_back(path);
  }
}

void zkeRegisterLinks(Buffer buffer, VectorString output) {
  int line = 0;
  while (line < buffer.line_count()) {
    zkeRegisterLinks(buffer.line(line), output);
    line++;
  }
}

LineColumn FindNextOpenLink(Buffer buffer, LineColumn start) {
  while (start.line() < buffer.line_count()) {
    auto line_contents = buffer.line(start.line());
    int column = line_contents.find_first_of("[", start.column());
    if (column == -1) {
      start = LineColumn(start.line() + 1, 0);
    } else {
      return LineColumn(start.line(), column);
    }
  }
  return start;
}

LineColumn FindLinkTextEnd(Buffer buffer, LineColumn start) {
  while (start.line() < buffer.line_count()) {
    auto line_contents = buffer.line(start.line());
    int column = line_contents.find_first_of("](", start.column());
    if (column == -1) {
      start = LineColumn(start.line() + 1, 0);
    } else {
      return LineColumn(start.line(), column);
    }
  }
  return start;
}

bool IsLocalLink(Buffer buffer, LineColumn link_start) {
  auto line_contents = buffer.line(link_start.line());
  auto basename = "XXX";
  auto tail = ".md)";
  return line_contents.size() >=
             link_start.column() + basename.size() + tail.size() &&
         line_contents.substr(link_start.column() + basename.size(),
                              tail.size()) == tail;
}

void zkeRemoveLocalLinks(Buffer buffer) {
  LineColumn position = LineColumn(0, 0);
  while (position.line() < buffer.line_count()) {
    auto start = FindNextOpenLink(buffer, position);
    if (start.line() == buffer.line_count()) {
      return;
    }
    auto end = FindLinkTextEnd(buffer, start);
    if (end.line() == buffer.line_count()) {
      return;
    }
    if (IsLocalLink(buffer,
                    LineColumn(end.line(), end.column() + "](".size()))) {
      buffer.ApplyTransformation(SetPositionTransformation(end));
      buffer.ApplyTransformation(
          DeleteTransformationBuilder()
              .set_modifiers(Modifiers().set_repetitions("](XXX.md)".size()))
              .build());
      buffer.ApplyTransformation(SetPositionTransformation(start));
      buffer.ApplyTransformation(DeleteTransformationBuilder().build());
      position = start;
    } else {
      position = LineColumn(start.line(), start.column() + 1);
    }
  }
}

void zkeExpand(Buffer buffer, string path, SetString titles, int depth,
               SetString visited) {
  if (visited.contains(path) || visited.size() > 100) return;
  visited.insert(path);
  Buffer sub_buffer = OpenFile(path, false);
  sub_buffer.WaitForEndOfFile();
  int line = 0;
  string text = "";
  for (int i = 0; i < depth; i++) {
    text += "#";
  }
  bool copy_contents = true;
  bool first_line = true;
  string title = "";
  while (line < sub_buffer.line_count()) {
    if (sub_buffer.line(line) == "Related:") {
      copy_contents = false;
    }
    auto line_contents = sub_buffer.line(line);
    if (first_line) {
      title = line_contents;
      int candidate_index = -1;
      for (int i = 0; i < titles.size(); i++) {
        string candidate = titles.get(i);
        if (line_contents.size() > candidate.size() &&
            line_contents.substr(0, candidate.size()) == candidate) {
          candidate_index = i;
        }
      }
      if (candidate_index != -1) {
        string candidate = titles.get(candidate_index);
        line_contents = line_contents.substr(
            candidate.size(), line_contents.size() - candidate.size());
        if (!line_contents.empty() && line_contents.substr(0, 1) == ":") {
          line_contents = "# " + SkipSpaces(line_contents.substr(
                                     1, line_contents.size() - 1));
        }
      }
      first_line = false;
    }
    if (copy_contents) {
      text += line_contents + "\n";
    }
    line++;
  }
  buffer.ApplyTransformation(
      InsertTransformationBuilder().set_text(text).build());

  VectorString pending = VectorString();
  zkeRegisterLinks(sub_buffer, pending);
  int links = 0;
  if (!title.empty()) {
    titles.insert(title);
  }
  while (links < pending.size()) {
    zkeExpand(buffer, pending.get(links), titles, depth + 1, visited);
    links++;
  }
  if (!titles.empty()) {
    titles.erase(title);
  }
}

Buffer zke(string path, string start, SetString visited) {
  auto buffer = OpenFile(path, true);
  buffer.WaitForEndOfFile();
  buffer.ApplyTransformation(SetPositionTransformation(LineColumn(0, 0)));
  buffer.ApplyTransformation(
      DeleteTransformationBuilder()
          // TODO: Add `set_buffer` and use that?
          .set_modifiers(Modifiers().set_line().set_repetitions(9999999))
          .build());
  zkeExpand(buffer, start, SetString(), 0, visited);
  zkeRemoveLocalLinks(buffer);
  buffer.Save();
  return buffer;
}

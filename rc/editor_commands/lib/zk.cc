// This file contains functions that I use to manage my Zettelkasten.
//
// The following functions are defined in the zettelkasten namespace (intended
// to be executed with `:` after adding zettelkasten to cpp_prompt_namespaces):
//
// * i - Open the index file (index.md)
// * ls - List all notes (with their title).
// * l - Expand the paths under the cursors to a full link.
// * ln - Create a new entry based on the title under the cursor.
// * Expand - Generate an article.

#include "paths.cc"
#include "strings.cc"

namespace zettelkasten {
namespace internal {
string GetNoteTitle(Buffer buffer) {
  auto line = buffer.line(0);
  if (line.substr(0, 1) == "#") {
    line = line.substr(1, line.size() - 1);
  }
  return SkipSpaces(line);
}

string GetNoteTitle(string path) {
  auto buffer = editor.OpenFile(path, false);
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

// Returns the path (ID) of the next available (empty) file. Includes the `.md`
// extension.
string NextEmpty() {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command(
      "find -size 0b -name '???.md' -printf '%f\n' | sort | head -1");
  options.set_insertion_type("ignore");
  auto buffer = editor.ForkCommand(options);
  buffer.WaitForEndOfFile();
  return buffer.line(0);
}

Buffer RunCommand(string name, string command, string insertion_type) {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command(command);
  options.set_insertion_type(insertion_type);
  options.set_name("zk: " + name);
  return editor.ForkCommand(options);
}

Buffer TitleSearch(string query, string insertion_type) {
  auto buffer = RunCommand("t: " + query,
                           "awk '{if (tolower($0)~\"" + query.shell_escape() +
                               "\") print FILENAME, $0; nextfile;}' ???.md",
                           insertion_type);
  buffer.set_allow_dirty_delete(true);
  buffer.WaitForEndOfFile();
  return buffer;
}

void VisitFileWithTitleSearch(string query) {
  auto buffer = RunCommand("search: " + query,
                           "awk '{if (tolower($0)~\"" + query.shell_escape() +
                               "\") system(\"edge -X \" FILENAME); "
                               "nextfile;}' ???.md | head -1",
                           "ignore");
  buffer.set_allow_dirty_delete(true);
}

TransformationOutput Link(Buffer buffer, TransformationInput input) {
  auto line = buffer.line(input.position().line());
  auto path_characters = buffer.path_characters();

  // Scroll back until we're at a path.
  int start = line.find_last_of(path_characters, input.position().column());
  if (start == -1) {
    // Nothing before us in the current line. Nothing.
    //
    // TODO(easy): Scroll back to the previous line and keep trying?
    return TransformationOutput();
  }

  // Scroll back to the beginning of the path.
  start = line.find_last_not_of(path_characters, start);
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
          .push(InsertTransformationBuilder().set_text(title).build());
  if (input.position().line() + 1 >= buffer.line_count()) {
    output.push(SetColumnTransformation(99999999))
        .push(InsertTransformationBuilder().set_text("\n").build());
  }
  int end_column = start + 1 + title.size() + 1 + 1 + adjusted_path.size() + 1;
  output.push(SetColumnTransformation(end_column));
  return output;
}

Buffer InitializeNewNote(string path, string title, string parent_title,
                         string parent_path) {
  auto new_note = editor.OpenFile(path, true);
  new_note.WaitForEndOfFile();
  new_note.ApplyTransformation(FunctionTransformation(
      [](TransformationInput input) -> TransformationOutput {
        return TransformationOutput()
            .push(InsertTransformationBuilder()
                      .set_text("# " + title + "\n\n\n\n## Related\n* [" +
                                parent_title + "](" + parent_path + ")\n")
                      .build())
            .push(SetPositionTransformation(LineColumn(2, 0)));
      }));
  return new_note;
}

TransformationOutput NewLink(Buffer buffer, TransformationInput input) {
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

  auto path = NextEmpty();
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
  InitializeNewNote(path, title, GetNoteTitle(buffer.path()),
                    Basename(buffer.path()));
  return output;
}

void RegisterLinks(string line, VectorString output) {
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

void RegisterLinks(Buffer buffer, VectorString output) {
  int line = 0;
  while (line < buffer.line_count()) {
    RegisterLinks(buffer.line(line), output);
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

void RemoveLocalLinks(Buffer buffer) {
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

void Expand(Buffer buffer, string path, SetString titles, int depth,
            SetString visited) {
  if (visited.contains(path) || visited.size() > 1000) return;
  visited.insert(path);
  Buffer sub_buffer = editor.OpenFile(path, false);
  sub_buffer.WaitForEndOfFile();
  int line = 0;
  string text = "";
  for (int i = 0; i < min(6, depth); i++) {
    text += "#";
  }
  bool copy_contents = true;
  bool first_line = true;
  string title = "";
  while (line < sub_buffer.line_count()) {
    if (sub_buffer.line(line) == "Related:" ||
        sub_buffer.line(line) == "## Related" ||
        sub_buffer.line(line) == "## Related:") {
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
  RegisterLinks(sub_buffer, pending);
  int links = 0;
  if (!title.empty()) {
    titles.insert(title);
  }
  while (links < pending.size()) {
    Expand(buffer, pending.get(links), titles, depth + 1, visited);
    links++;
  }
  if (!titles.empty()) {
    titles.erase(title);
  }
}

SetString ParseBlacklist(string blacklist) {
  SetString output = SetString();
  int start = 0;
  while (true) {
    if (start == blacklist.size()) {
      return output;
    }
    int end = blacklist.find_first_of(" ", start);
    if (end == -1) {
      end = blacklist.size();
    }
    output.insert(blacklist.substr(start, end - start) + ".md");
    start = end;
    while (start < blacklist.size() && blacklist.substr(start, 1) == " ")
      start++;
  }
}

Buffer ExpandIntoPath(string path, string start, string blacklist) {
  auto buffer = editor.OpenFile(path + ".md", true);
  buffer.WaitForEndOfFile();
  buffer.ApplyTransformation(SetPositionTransformation(LineColumn(0, 0)));
  buffer.ApplyTransformation(
      DeleteTransformationBuilder()
          // TODO: Add `set_buffer` and use that?
          .set_modifiers(Modifiers().set_line().set_repetitions(9999999))
          .build());
  Expand(buffer, start + ".md", SetString(), 0, ParseBlacklist(blacklist));
  RemoveLocalLinks(buffer);
  buffer.Save();
  return buffer;
}

// TODO(easy): This is garbage. Delete.
string ReplaceText(string pattern, string replacement, string input) {
  string output = "";
  int position = 0;
  while (position < input.size()) {
    int next = input.find(pattern, position);
    bool at_match = next != -1;
    if (next == -1) {
      next = input.size();
    }
    output += input.substr(position, next - position);
    if (at_match) {
      output += "<b>" + replacement + "</b>";
    }
    position = next + pattern.size();
  }

  return output;
}

void AppendLink(Buffer buffer, string title, string path) {
  buffer.ApplyTransformation(FunctionTransformation(
      [](TransformationInput input) -> TransformationOutput {
        return TransformationOutput()
            .push(SetPositionTransformation(LineColumn(10000, 0)))
            .push(InsertTransformationBuilder()
                      .set_text("* [" + title + "](" + path + ")\n")
                      .build());
      }));
}

string ExtractContentsFromTemplate(string path) {
  Buffer template = editor.OpenFile(path + ".md", false);
  template.WaitForEndOfFile();
  string output = "";
  bool found_start_marker = false;
  for (int line = 0; line < template.line_count(); line++) {
    string contents = template.line(line);
    if (!found_start_marker) {
      if (contents.starts_with("## ")) {
        found_start_marker = true;
      }
    } else if (contents.starts_with("## ")) {
      return output;
    } else {
      output = output + contents + "\n";
    }
  }
  return output;
}

void Journal(int days_to_generate, Time start, string template_path) {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    auto parent_title = GetNoteTitle(buffer.path());
    auto parent_path = Basename(buffer.path());
    auto template_contents = ExtractContentsFromTemplate(template_path);
    buffer.ApplyTransformation(FunctionTransformation(
        [](TransformationInput input) -> TransformationOutput {
          auto output = TransformationOutput();
          string previous_child_path = "";
          string previous_child_title = "";
          auto next_child_path = NextEmpty();
          for (int i = 0; i < days_to_generate; i++) {
            auto child_title = start.format("%Y-%m-%d (%a)");
            auto child_buffer = InitializeNewNote(next_child_path, child_title,
                                                  parent_title, parent_path);
            // Append the template.
            child_buffer.ApplyTransformation(FunctionTransformation(
                [](TransformationInput input) -> TransformationOutput {
                  return TransformationOutput()
                      .push(SetPositionTransformation(LineColumn(2, 0)))
                      .push(InsertTransformationBuilder()
                                .set_text(template_contents)
                                .build());
                }));

            if (previous_child_path != "") {
              AppendLink(child_buffer, previous_child_title,
                         previous_child_path);
            }

            output.push(SetPositionTransformation(input.position()))
                .push(InsertTransformationBuilder()
                          .set_text("* [" + child_title + "](" +
                                    next_child_path + ")\n")
                          .build());
            previous_child_path = next_child_path;
            previous_child_title = child_title;
            start = start.AddDays(1);
            // This is suboptimal: we need to save before we call NextEmpty (so
            // that it won't return the current buffer). That forces us to save
            // again after we append a link to it.
            child_buffer.Save();
            if (i + 1 < days_to_generate) {
              next_child_path = NextEmpty();
              AppendLink(child_buffer, start.format("%Y-%m-%d (%a)"),
                         next_child_path);
              child_buffer.Save();
            }
          }
          return output;
        }));
    // buffer.Save();
  });
}
}  // namespace internal

void Journal(string days_to_generate, string start_day, string template_path) {
  internal::Journal(days_to_generate.toint(), ParseTime(start_day, "%Y-%m-%d"),
                    template_path);
}

Buffer PreviewJournal(string days_to_generate, string start_day,
                      string template_path) {
  auto preview_buffer = editor.OpenFile("", false);
  preview_buffer.ApplyTransformation(
      InsertTransformationBuilder()
          .set_text(
              "Generate journal entries for many days from a given template.\n"
              "Format: journal DAYS_TO_GENERATE START_DATE TEMPLATE_PATH\n"
              "Ex: journal 10 2021-03-10 00a")
          .build());
  preview_buffer.set_name("Journal (help)");
  return preview_buffer;
}

// Open the index. index.md is expected to be a link to the main entry point.
void I() { editor.OpenFile("index.md", true); }

void Ls() {
  internal::RunCommand("ls", "~/bin/zkls", "visit")
      .set_allow_dirty_delete(true);
}

void Rev() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    string path = Basename(buffer.path());
    if (path == "") return;
    internal::RunCommand("rev: " + path,
                         "grep " + path.shell_escape() + " ???.md", "visit")
        .set_allow_dirty_delete(true);
    ;
  });
  return;
}

void S(string query) {
  internal::RunCommand("s: " + query,
                       "grep -i " + query.shell_escape() + " ???.md", "visit")
      .set_allow_dirty_delete(true);
}

Buffer PreviewS(string query) {
  auto buffer = internal::RunCommand(
      "s: " + query, "grep -i " + query.shell_escape() + " ???.md", "ignore");
  buffer.WaitForEndOfFile();
  buffer.set_allow_dirty_delete(true);
  return buffer;
}

// Receives a string and produces a list of all Zettel that include that string
// in their title.
void T(string query) { internal::TitleSearch(query, "visit"); }

void Today() { internal::VisitFileWithTitleSearch(Now().format("%Y-%m-%d")); }

void Yesterday() {
  internal::VisitFileWithTitleSearch(Now().AddDays(-1).format("%Y-%m-%d"));
}

void Tomorrow() {
  internal::VisitFileWithTitleSearch(Now().AddDays(1).format("%Y-%m-%d"));
}

Buffer PreviewT(string query) { return internal::TitleSearch(query, "ignore"); }

// Replaces a path (e.g., `03d.md`) with a link to it, extracting the text of
// the link from the first line in the file (e.g. `[Bauhaus](03d.md)`).
void L() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.ApplyTransformation(FunctionTransformation(
        [](TransformationInput input) -> TransformationOutput {
          return internal::Link(buffer, input);
        }));
  });
}

// Turns a text like "[Some Title]" into a link "[Some Title](xxx.md)", where
// xxx.md is the next available (unused) identifier; loads the next note (from
// said identifier) and inserts some initial skeleton into the new file
// (including the title); and saves the original buffer.
void N() {  // Short for New.
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.ApplyTransformation(FunctionTransformation(
        [](TransformationInput input) -> TransformationOutput {
          return internal::NewLink(buffer, input);
        }));
    buffer.Save();
  });
}

auto Expand = internal::ExpandIntoPath;
}  // namespace zettelkasten

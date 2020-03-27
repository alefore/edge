// This file contains functions that I use to manage my Zettelkasten.

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

// Produce the index.
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
  string title = GetNoteTitle(path);
  return TransformationOutput()
      .push(SetColumnTransformation(end))
      .push(InsertTransformationBuilder().set_text(")").build())
      .push(SetColumnTransformation(start))
      .push(InsertTransformationBuilder().set_text("[](").build())
      .push(SetColumnTransformation(start + 1))
      .push(InsertTransformationBuilder().set_text(title).build())
      .push(SetColumnTransformation(start + 1 + title.size() + 1 + 1 +
                                    path.size() + 1));
}

// Replaces a path (e.g., `03d.md` with a link to it, extracting the text of the
// link from the first line in the file.
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
// said identifier) and inserts the title.
void zkln() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.ApplyTransformation(FunctionTransformation(
        [](TransformationInput input) -> TransformationOutput {
          return ZKInternalNewLink(buffer, input);
        }));
  });
}

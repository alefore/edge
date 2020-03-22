// This file contains functions that I use to manage my Zettelkasten.

#include "paths.cc"
#include "strings.cc"

void zkRunCommand(string name, string command) {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command(command);
  options.set_insertion_type("visit");
  options.set_name("zk: " + name);
  ForkCommand(options);
}

void zkls() { zkRunCommand("ls", "~/bin/zkls"); }

void zkrev() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    string path = Basename(buffer.path());
    if (path == "") return;
    zkRunCommand("rev: " + path, "grep " + path.shell_escape() + " ???.md");
  });
  return;
}

// Produce the index.
void zki() { OpenFile("index.md", true); }

void zks(string query) {
  zkRunCommand("s: " + query, "grep -i " + query.shell_escape() + " ???.md");
}

// Find the smallest unused ID. This assumes that files are of the form `???.md`
// and are created in advance.
void zkn() {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command(
      "edge -X $( find . -size 0b -name '???.md' | sort | head -1 )");
  options.set_insertion_type("ignore");
  ForkCommand(options);
}

string GetNoteTitle(string path) {
  auto buffer = OpenFile(path, false);
  buffer.WaitForEndOfFile();
  auto line = buffer.line(0);
  if (line.substr(0, 1) == "#") {
    line = line.substr(1, line.size() - 1);
  }
  return SkipSpaces(line);
}

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

  return TransformationOutput()
      .push(SetColumnTransformation(end))
      .push(InsertTransformationBuilder().set_text(")").build())
      .push(SetColumnTransformation(start))
      .push(InsertTransformationBuilder().set_text("[](").build())
      .push(SetColumnTransformation(start + 1))
      .push(InsertTransformationBuilder().set_text(GetNoteTitle(path)).build());
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

#include "strings.cc"

namespace md {
namespace internal {
string GetLineTitle(number depth, string input) {
  string prefix = "#" * depth + " ";
  if (!input.starts_with(prefix)) return "";
  return SkipSpaces(input.substr(prefix.size(), input.size() - prefix.size()));
}

bool IsLineTitle(string title_expected, number depth, string input) {
  return GetLineTitle(depth, input) == title_expected;
}

LineColumn FindSectionEnd(Buffer buffer, number line, number depth) {
  while (line < buffer.line_count() &&
         GetLineTitle(depth, buffer.line(line)) == "")
    line++;
  return LineColumn(line, 0);
}

void AddLinksFromLine(string line, VectorString output) {
  while (true) {
    number column = line.find_first_of("[", 0);
    if (column == -1) return;
    column = line.find_first_of("]", column);
    if (column == -1) return;
    line = line.substr(column, line.size());
    if (!line.empty() && line.starts_with("(")) {
      number target_end = line.find_first_of(")", 1);
      if (target_end == -1) return;
      output.push_back(line.substr(1, target_end - 1));
      column = target_end + 1;
    }
  }
}

}  // namespace internal

void Pandoc(string launch_browser) {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.SetStatus("pandoc ...");
    ForkCommandOptions options = ForkCommandOptions();
    string command = "pandoc " + buffer.path().shell_escape() +
                     " --shift-heading-level-by=-1"
                     " -f markdown -t html -s -o /tmp/output.html; edge "
                     "--run 'editor.OpenFile(\"" +
                     buffer.path().shell_escape() +
                     "\", false).SetStatus(\"pandoc 🗸\");'";
    if (!launch_browser.empty())
      command += "; xdg-open file:///tmp/output.html";
    options.set_command(command);
    options.set_insertion_type("ignore");
    editor.ForkCommand(options);
  });
}

// TODO(trivial, 2025-04-17): Convert this to OptionalRange. I /think/ (not
// sure) we can't currently create OptionalRange values with a Range (from the
// vm code).
Range FindSection(Buffer buffer, string title, number depth) {
  for (number line; line < buffer.line_count(); line++) {
    if (internal::IsLineTitle(title, depth, buffer.line(line)))
      return Range(LineColumn(line, 0),
                   internal::FindSectionEnd(buffer, line + 1, depth));
  }
  return Range(LineColumn(0, 0), LineColumn(0, 0));
}

VectorString GetLinks(Buffer buffer) {
  VectorString output;
  // TODO(trivial, 2025-04-22): Use Buffer.ForEach?
  for (number line; line < buffer.line_count(); line++)
    AddLinksFromLine(buffer.line(line), output);
  return output;
}
}  // namespace md

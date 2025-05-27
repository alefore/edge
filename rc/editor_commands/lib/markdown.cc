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
    line = SkipSpaces(line.substr(column + 1, line.size() - (column + 1)));
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
    ForkCommandOptions options;
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

SearchOptions SearchOptionsForSection(string title, number depth) {
  return SearchOptions().set_query("^" + "#" * depth + " *" + title);
}

OptionalRange FindSection(Buffer buffer, string title, number depth) {
  VectorLineColumn matches =
      SearchOptionsForSection(title, depth).search(buffer);
  if (matches.size() == 0) return OptionalRange();
  LineColumn start = matches.get(0);
  return OptionalRange(
      Range(start, internal::FindSectionEnd(buffer, start.line() + 1, depth)));
}

VectorString GetLinks(Buffer buffer) {
  VectorString output;
  buffer.ForEach([](number i, string line) -> void {
    internal::AddLinksFromLine(line, output);
  });
  return output;
}
}  // namespace md

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

Range FindSection(Buffer buffer, string title, number depth) {
  for (number line = 0; line < buffer.line_count(); line++) {
    if (internal::IsLineTitle(title, depth, buffer.line(line)))
      return Range(LineColumn(line, 0),
                   internal::FindSectionEnd(buffer, line + 1, depth));
  }
  return Range(LineColumn(0, 0), LineColumn(0, 0));
}
}  // namespace md

void Pandoc(string launch_browser) {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    buffer.SetStatus("pandoc ...");
    ForkCommandOptions options = ForkCommandOptions();
    string command = "pandoc " + buffer.path().shell_escape() +
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

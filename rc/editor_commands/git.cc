void GitCommitAll(string message) {
  RunCommandOptions options;
  options.set_command("git commit -a" +
                      (message.empty() ? "" : " -m " + message.shell_escape()));
  options.set_insertion_type("visit");
  editor.RunCommand(options);
}

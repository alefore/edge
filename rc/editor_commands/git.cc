void GitCommitAll(string message) {
  ForkCommandOptions options = ForkCommandOptions();
  options.set_command("git commit -a" +
                      (message.empty() ? "" : " -m " + message.shell_escape()));
  options.set_insertion_type("visit");
  editor.ForkCommand(options);
}

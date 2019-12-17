#include "lib/strings"

SetString shell_prompt_help_programs = SetString();
shell_prompt_help_programs.insert("blaze");
shell_prompt_help_programs.insert("cat");
shell_prompt_help_programs.insert("date");
shell_prompt_help_programs.insert("edge");
shell_prompt_help_programs.insert("find");
shell_prompt_help_programs.insert("gcc");
shell_prompt_help_programs.insert("git");
shell_prompt_help_programs.insert("grep");
shell_prompt_help_programs.insert("ls");
shell_prompt_help_programs.insert("locate");
shell_prompt_help_programs.insert("make");
shell_prompt_help_programs.insert("man");
shell_prompt_help_programs.insert("python");
shell_prompt_help_programs.insert("rm");
shell_prompt_help_programs.insert("sleep");

SetString git_sub_commands = SetString();
git_sub_commands.insert("add");
git_sub_commands.insert("bisect");
git_sub_commands.insert("branch");
git_sub_commands.insert("checkout");
git_sub_commands.insert("clone");
git_sub_commands.insert("commit");
git_sub_commands.insert("diff");
git_sub_commands.insert("fetch");
git_sub_commands.insert("grep");
git_sub_commands.insert("init");
git_sub_commands.insert("log");
git_sub_commands.insert("merge");
git_sub_commands.insert("mv");
git_sub_commands.insert("pull");
git_sub_commands.insert("push");
git_sub_commands.insert("rebase");
git_sub_commands.insert("reset");
git_sub_commands.insert("restore");
git_sub_commands.insert("rm");
git_sub_commands.insert("show");
git_sub_commands.insert("status");
git_sub_commands.insert("switch");
git_sub_commands.insert("tag");

string GetSubCommand(string command) {
  command = SkipInitialSpaces(command);
  int space = command.find_first_of(" ", 0);
  if (space == -1) {
    return "";
  }
  return BaseCommand(
      SkipInitialSpaces(command.substr(space, command.size() - space)));
}

string HelpCommandFor(string command) { return command + " --help"; }

string GetShellPromptContextProgram(string input) {
  string base_command = BaseCommand(input);
  string sub_command = "";
  if (base_command == "git") {
    string sub_command_candidate = GetSubCommand(input);
    if (git_sub_commands.contains(sub_command_candidate)) {
      sub_command = sub_command_candidate;
    }
  }
  if (shell_prompt_help_programs.contains(base_command)) {
    return HelpCommandFor(base_command +
                          (sub_command.empty() ? "" : " " + sub_command));
  }
  return "";
}

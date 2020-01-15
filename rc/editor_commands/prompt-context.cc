#include "lib/strings"

SetString shell_prompt_help_programs_man = SetString();
shell_prompt_help_programs_man.insert("look");

SetString shell_prompt_help_programs = SetString();
shell_prompt_help_programs.insert("apt-get");
shell_prompt_help_programs.insert("blaze");
shell_prompt_help_programs.insert("cat");
shell_prompt_help_programs.insert("csearch");
shell_prompt_help_programs.insert("date");
shell_prompt_help_programs.insert("edge");
shell_prompt_help_programs.insert("find");
shell_prompt_help_programs.insert("gcc");
shell_prompt_help_programs.insert("git");
shell_prompt_help_programs.insert("grep");
shell_prompt_help_programs.insert("hg");
shell_prompt_help_programs.insert("ls");
shell_prompt_help_programs.insert("locate");
shell_prompt_help_programs.insert("make");
shell_prompt_help_programs.insert("man");
shell_prompt_help_programs.insert("python");
shell_prompt_help_programs.insert("rm");
shell_prompt_help_programs.insert("sleep");

SetString blaze_sub_commands = SetString();
blaze_sub_commands.insert("test");
blaze_sub_commands.insert("build");

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

SetString hg_sub_commands = SetString();
hg_sub_commands.insert("amend");
hg_sub_commands.insert("checkout");
hg_sub_commands.insert("co");
hg_sub_commands.insert("commit");
hg_sub_commands.insert("diff");
hg_sub_commands.insert("xl");
hg_sub_commands.insert("uploadchain");

string GetSubCommand(string command) {
  command = SkipInitialSpaces(command);
  int space = command.find_first_of(" ", 0);
  if (space == -1) {
    return "";
  }
  return BaseCommand(
      SkipInitialSpaces(command.substr(space, command.size() - space)));
}

string LookUpSubCommand(SetString sub_commands, string command) {
  string candidate = GetSubCommand(command);
  return sub_commands.contains(candidate) ? candidate : "";
}

string HelpCommandFor(string command) { return command + " --help"; }

string GetShellPromptContextProgram(string input) {
  string base_command = BaseCommand(input);
  if (shell_prompt_help_programs_man.contains(base_command)) {
    return "man " + base_command;
  }
  string sub_command = "";
  if (base_command == "blaze") {
    sub_command = LookUpSubCommand(blaze_sub_commands, input);
    if (!sub_command.empty()) {
      return base_command + " help " + sub_command;
    }
  } else if (base_command == "git") {
    sub_command = LookUpSubCommand(git_sub_commands, input);
  } else if (base_command == "hg") {
    sub_command = LookUpSubCommand(hg_sub_commands, input);
  }
  if (shell_prompt_help_programs.contains(base_command)) {
    return HelpCommandFor(base_command +
                          (sub_command.empty() ? "" : " " + sub_command));
  }
  return "";
}

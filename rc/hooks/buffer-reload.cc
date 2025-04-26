#include "../editor_commands/lib/strings.cc"

if (buffer.path() == "") {
  string command = BaseCommand(SkipInitialSpaces(buffer.command()));
  // Interactive commands that get a full pts. This must happen here (rather
  // than in buffer-first-enter.cc) so that the pts information is set before
  // the command is actually spawned.
  if (command == "bash" || command == "python" || command == "python3" ||
      command == "watch" || command == "sh" || command == "gdb" ||
      command == "fish")
    buffer.set_pts(true);
}

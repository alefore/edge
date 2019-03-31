#ifndef __AFC_EDITOR_SRC_ARGS_H__
#define __AFC_EDITOR_SRC_ARGS_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace afc {
namespace editor {

using std::string;

struct Args {
  string binary_name;
  std::vector<string> files_to_open;
  std::vector<string> commands_to_fork;

  // Contains C++ (VM) code to execute.
  string commands_to_run;

  bool server = false;
  string server_path = "";

  // If non-empty, path of the server to connect to.
  string client = "";

  bool mute = false;
  bool background = false;
};

Args ParseArgs(int argc, const char** argv);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SRC_ARGS_H__

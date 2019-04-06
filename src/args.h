#ifndef __AFC_EDITOR_SRC_ARGS_H__
#define __AFC_EDITOR_SRC_ARGS_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace afc {
namespace editor {

using std::string;
using std::wstring;

// TODO: Convert all to wstring.
struct Args {
  wstring binary_name;
  wstring home_directory;
  std::vector<wstring> config_paths;

  std::vector<wstring> files_to_open;
  std::vector<wstring> commands_to_fork;

  // Contains C++ (VM) code to execute.
  wstring commands_to_run;

  bool server = false;
  wstring server_path = L"";

  // If non-empty, path of the server to connect to.
  wstring client = L"";

  bool mute = false;
  bool background = false;

  enum class NestedEdgeBehavior {
    // Wait until the buffers we open have been closed in the parent.
    kWaitForClose,
    // Exit as soon as we know that we've successfully communicated with the
    // parent.
    kExitEarly,
  };

  NestedEdgeBehavior nested_edge_behavior = NestedEdgeBehavior::kWaitForClose;
};

Args ParseArgs(int argc, const char** argv);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SRC_ARGS_H__

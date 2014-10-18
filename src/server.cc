#include "server.h"

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <utility>

#include "buffer.h"
#include "editor.h"
#include "file_link_mode.h"
#include "lazy_string.h"
#include "vm/public/vm.h"

namespace afc {
namespace editor {

using namespace afc::vm;

using std::pair;
using std::shared_ptr;
using std::cerr;
using std::string;

struct Environment;

string CreateFifo() {
  while (true) {
    char* path_str = mktemp(strdup("/tmp/edge-server-XXXXXX"));
    if (mkfifo(path_str, 0600) == -1) {
      free(path_str);
      continue;
    }
    string path(path_str);
    free(path_str);
    return path;
  }
}

int MaybeConnectToParentServer() {
  const char* variable = "EDGE_PARENT_ADDRESS";
  char* server_address = getenv(variable);
  if (server_address == nullptr) {
    return - 1;
  }
  string private_fifo = CreateFifo();
  int fd = open(server_address, O_WRONLY);
  if (fd == -1) {
    cerr << server_address << ": open failed: " << strerror(errno);
    exit(1);
  }
  string command = "ConnectTo(\"" + private_fifo + "\");\n";
  if (write(fd, command.c_str(), command.size()) == -1) {
    cerr << server_address << ": write failed: " << strerror(errno);
    exit(1);
  }
  close(fd);
  int private_fd = open(private_fifo.c_str(), O_RDWR);
  if (private_fd == -1) {
    cerr << private_fd << ": open failed: " << strerror(errno);
    exit(1);
  }
  return private_fd;
}

class ServerBuffer : public OpenBuffer {
 public:
  ServerBuffer(EditorState* editor_state, const string& name)
      : OpenBuffer(editor_state, name) {
    set_bool_variable(variable_clear_on_reload(), false);
    set_bool_variable(variable_vm_exec(), true);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    string address = read_string_variable(variable_path());
    int fd = open(address.c_str(), O_RDONLY | O_NDELAY);
    if (fd == -1) {
      cerr << address << ": open failed: " << strerror(errno);
      exit(1);
    }
    target->SetInputFile(fd, false, -1);

    editor_state->ScheduleRedraw();
  }
};

string GetBufferName(const string& prefix, size_t count) {
  return prefix + " " + std::to_string(count);
}

// TODO: Reuse this for anonymous buffers.
string GetUnusedBufferName(EditorState* editor_state, const string& prefix) {
  size_t count = 0;
  while (editor_state->buffers()->find(GetBufferName(prefix, count))
         != editor_state->buffers()->end()) {
    count++;
  }
  return GetBufferName(prefix, count);
}

void StartServer(EditorState* editor_state) {
  string address = CreateFifo();
  setenv("EDGE_PARENT_ADDRESS", address.c_str(), 1);
  auto buffer = OpenServerBuffer(editor_state, address);
  buffer->set_bool_variable(OpenBuffer::variable_reload_after_exit(), true);
  buffer->set_bool_variable(OpenBuffer::variable_default_reload_after_exit(), true);
}

shared_ptr<OpenBuffer>
OpenServerBuffer(EditorState* editor_state, const string& address) {
  shared_ptr<OpenBuffer> buffer(
      new ServerBuffer(editor_state,
                       GetUnusedBufferName(editor_state, "- server")));
  buffer->set_string_variable(OpenBuffer::variable_path(), address);
  editor_state->buffers()->insert(make_pair(buffer->name(), buffer));
  buffer->Reload(editor_state);
  return buffer;
}

}  // namespace editor
}  // namespace afc

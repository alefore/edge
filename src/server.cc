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
#include "wstring.h"

namespace afc {
namespace editor {

using namespace afc::vm;

using std::pair;
using std::shared_ptr;
using std::cerr;
using std::string;

struct Environment;

bool CreateFifo(wstring input_path, wstring* output, wstring* error) {
  while (true) {
    char* path_str = input_path.empty()
        ? mktemp(strdup("/tmp/edge-server-XXXXXX"))
        : strdup(ToByteString(input_path).c_str());
    if (mkfifo(path_str, 0600) == -1) {
      *error =
          FromByteString(path_str) + L": " + FromByteString(strerror(errno));
      free(path_str);
      if (!input_path.empty()) {
        return false;
      }
      continue;
    }
    *output = FromByteString(path_str);
    free(path_str);
    return true;
  }
}

int MaybeConnectToParentServer(wstring *error) {
  wstring dummy;
  if (error == nullptr) {
    error = &dummy;
  }

  const char* variable = "EDGE_PARENT_ADDRESS";
  char* server_address = getenv(variable);
  if (server_address == nullptr) {
    *error = L"Unable to find remote address (through environment variable "
             L"EDGE_PARENT_ADDRESS).";
    return -1;
  }
  return MaybeConnectToServer(string(server_address), error);
}

int MaybeConnectToServer(const string& address, wstring* error) {
  wstring dummy;
  if (error == nullptr) {
    error = &dummy;
  }

  int fd = open(address.c_str(), O_WRONLY);
  if (fd == -1) {
    *error = FromByteString(address) + L": Connecting to server: open failed: "
             + FromByteString(strerror(errno));
    return -1;
  }
  wstring private_fifo;
  if (!CreateFifo(L"", &private_fifo, error)) {
    *error = L"Unable to create fifo for communication with server: " + *error;
    return -1;
  }
  LOG(INFO) << "Fifo created: " << private_fifo;
  string command = "ConnectTo(\"" + ToByteString(private_fifo) + "\");\n";
  LOG(INFO) << "Sending connection command: " << command;
  if (write(fd, command.c_str(), command.size()) == -1) {
    *error = FromByteString(address) + L": write failed: "
           + FromByteString(strerror(errno));
    return -1;
  }
  close(fd);
  int private_fd = open(ToByteString(private_fifo).c_str(), O_RDWR);
  LOG(INFO) << "Connection fd: " << private_fd;
  if (private_fd == -1) {
    *error = private_fifo + L": open failed: "
           + FromByteString(strerror(errno));
    return -1;
  }
  CHECK_GT(private_fd, -1);
  return private_fd;
}

class ServerBuffer : public OpenBuffer {
 public:
  ServerBuffer(EditorState* editor_state, const wstring& name)
      : OpenBuffer(editor_state, name) {
    set_bool_variable(variable_clear_on_reload(), false);
    set_bool_variable(variable_vm_exec(), true);
    set_bool_variable(variable_show_in_buffers_list(), false);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    wstring address = read_string_variable(variable_path());
    int fd = open(ToByteString(address).c_str(), O_RDONLY | O_NDELAY);
    if (fd == -1) {
      cerr << address << ": ReloadInto: open failed: " << strerror(errno);
      exit(1);
    }

    LOG(INFO) << "Server received connection: " << fd;
    target->SetInputFiles(editor_state, fd, -1, false, -1);

    editor_state->ScheduleRedraw();
  }
};

wstring GetBufferName(const wstring& prefix, size_t count) {
  return prefix + L" " + std::to_wstring(count);
}

// TODO: Reuse this for anonymous buffers.
wstring GetUnusedBufferName(EditorState* editor_state, const wstring& prefix) {
  size_t count = 0;
  while (editor_state->buffers()->find(GetBufferName(prefix, count))
         != editor_state->buffers()->end()) {
    count++;
  }
  return GetBufferName(prefix, count);
}

bool StartServer(EditorState* editor_state, wstring address,
                 wstring* actual_address, wstring* error) {
  wstring dummy;
  if (actual_address == nullptr) {
    actual_address = &dummy;
  }

  if (!CreateFifo(address, actual_address, error)) {
    *error = L"Error creating fifo: " + *error;
    return false;
  }

  LOG(INFO) << "Starting server: " << *actual_address;
  setenv("EDGE_PARENT_ADDRESS", ToByteString(*actual_address).c_str(), 1);
  auto buffer = OpenServerBuffer(editor_state, *actual_address);
  buffer->set_bool_variable(OpenBuffer::variable_reload_after_exit(), true);
  buffer->set_bool_variable(OpenBuffer::variable_default_reload_after_exit(),
                            true);

  return true;
}

shared_ptr<OpenBuffer>
OpenServerBuffer(EditorState* editor_state, const wstring& address) {
  shared_ptr<OpenBuffer> buffer(
      new ServerBuffer(editor_state,
                       GetUnusedBufferName(editor_state, L"- server")));
  buffer->set_string_variable(OpenBuffer::variable_path(), address);
  editor_state->buffers()->insert(make_pair(buffer->name(), buffer));
  buffer->Reload(editor_state);
  return buffer;
}

}  // namespace editor
}  // namespace afc

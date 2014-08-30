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

namespace afc {
namespace editor {

using std::pair;
using std::shared_ptr;

struct Environment;

struct Value {
  enum Type {
    TYPE_INTEGER,
    TYPE_STRING,
    TYPE_SYMBOL,
    EDITOR_STATE,
    ENVIRONMENT,
    FUNCTION1,
  };

  Type type;

  union {
    int integer;
    shared_ptr<LazyString>* str;
    EditorState* editor_state;
    Environment* environment;
    std::function<void(Value)>* function1;
  } data;
};

struct Environment {
  pair<map<string, Value>, Environment*> table;

  Value Lookup(const string& symbol);
};

Value Environment::Lookup(const string& symbol) {
  auto it = table.first.find(symbol);
  if (it != table.first.end()) {
    return it->second;
  }
  if (table.second != nullptr) {
    return table.second->Lookup(symbol);
  }

  Value output;
  output.type = Value::TYPE_INTEGER;
  output.data.integer = 0;
  return output;
}

void ValueDestructor(Value value) {
  switch (value.type) {
    case Value::TYPE_INTEGER:
      break;
    case Value::TYPE_STRING:
      delete value.data.str;
    case Value::TYPE_SYMBOL:
      delete value.data.str;
    case Value::EDITOR_STATE:
      break;
    case Value::ENVIRONMENT:
      break;
    case Value::FUNCTION1:
      delete value.data.function1;
  }
}

#include "cpp.h"
#include "cpp.c"

using std::cerr;
using std::string;

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
  ServerBuffer(const string& name)
      : OpenBuffer(name),
        parser_(CppAlloc(malloc), [](void* parser) { CppFree(parser, free); }) {
    set_bool_variable(variable_clear_on_reload(), false);
    environment_.type = Value::ENVIRONMENT;
    environment_.data.environment = nullptr;
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    string address = read_string_variable(variable_path());
    int fd = open(address.c_str(), O_RDONLY | O_NDELAY);
    if (fd == -1) {
      cerr << address << ": open failed: " << strerror(errno);
      exit(1);
    }
    SetInputFile(fd, false, -1);

    environment_.data.environment = new Environment();
    auto& table = environment_.data.environment->table.first;
    table["OpenBuffer"].type = Value::FUNCTION1;
    table["OpenBuffer"].data.function1 = new std::function<void(Value)>([editor_state](Value value) {
      if (value.type != Value::TYPE_STRING) {
        return;
      }
      string path = (*value.data.str)->ToString();
      editor_state->set_current_buffer(OpenFile(editor_state, path, path));
    });

    table["ConnectTo"].type = Value::FUNCTION1;
    table["ConnectTo"].data.function1 = new std::function<void(Value)>([editor_state](Value value) {
      if (value.type != Value::TYPE_STRING) {
        return;
      }
      OpenServerBuffer(editor_state, (*value.data.str)->ToString());
    });
    environment_.data.environment->table.second = nullptr;

    editor_state->ScheduleRedraw();
  }

  virtual void AppendRawLine(
      EditorState* editor_state, shared_ptr<LazyString> str) {
    OpenBuffer::AppendRawLine(editor_state, str);
    size_t pos = 0;
    int token;
    while (pos < str->size()) {
      Value input;
      switch (str->get(pos)) {
        case '/':
          if (pos + 1 < str->size() && str->get(pos + 1) == '/') {
            pos = str->size();
          }
          break;

        case ';':
          token = SEMICOLON;
          pos++;
          break;

        case '+':
          token = PLUS;
          pos++;
          break;

        case '-':
          token = MINUS;
          pos++;
          break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          token = INTEGER;
          input.type = Value::TYPE_INTEGER;
          input.data.integer = 0;
          while (pos < str->size() && isdigit(str->get(pos))) {
            input.data.integer = input.data.integer * 10 + str->get(pos) - '0';
            pos++;
          }
          break;

        case '"':
          {
            token = STRING;
            input.type = Value::TYPE_STRING;
            size_t start = ++pos;
            while (pos < str->size() && str->get(pos) != '"') {
              pos++;
            }
            input.data.str = new shared_ptr<LazyString>();
            *input.data.str = Substring(str, start, pos - start);
            pos++;
          }
          break;

        case ' ':
          pos++;
          continue;

        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
        case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
        case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
        case 'V': case 'W': case 'X': case 'Y': case 'Z':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
        case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
        case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
        case 'v': case 'w': case 'x': case 'y': case 'z':
          {
            token = SYMBOL;
            input.type = Value::TYPE_SYMBOL;
            size_t start = pos;
            while (pos < str->size() && isalnum(str->get(pos))) {
              pos++;
            }
            input.data.str = new shared_ptr<LazyString>();
            *input.data.str = Substring(str, start, pos - start);
          }
          break;

        case '(':
          token = LPAREN;
          pos++;
          break;

        case ')':
          token = RPAREN;
          pos++;
          break;

        default:
          cerr << "Unhandled character at position " << pos << ": " << str->ToString();
          exit(54);
      }
      Cpp(parser_.get(), token, input, environment_);
    }
  }

  std::unique_ptr<void, std::function<void(void*)>> parser_;
  Value environment_;
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
      new ServerBuffer(GetUnusedBufferName(editor_state, "- server")));
  buffer->set_string_variable(OpenBuffer::variable_path(), address);
  editor_state->buffers()->insert(make_pair(buffer->name(), buffer));
  buffer->Reload(editor_state);
  return buffer;
}

}  // namespace editor
}  // namespace afc

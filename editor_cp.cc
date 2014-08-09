#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>

extern "C" {
#include <curses.h>
#include <term.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
}

#include "token.h"
#include "line_parser.h"

namespace afc {
namespace editor {

struct TerminalInformation {
  TerminalInformation()
      : clear_screen_string(tgetstr("cl", nullptr)) {
  }

  char* clear_screen_string;
};


static std::unique_ptr<Token> ParseFromPath(const std::string& path) {
  std::ifstream input(path.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
  std::cout << input.rdbuf();
  std::stringstream sstr;
  sstr << input.rdbuf();
  printf("READ: %s (length: %d)\n", sstr.str().c_str(), sstr.str().length());
  return Parse(sstr.str());
}

class OpenFile {
 public:
  OpenFile(const std::string& path)
      : path_(path),
        contents_(ParseFromPath(path)) {
  }
  void Display() {
    std::string contents;
    contents_->AppendToString(&contents);
    printf("[[[%s]]]", contents.c_str());
  }

 private:
  std::string path_;
  std::unique_ptr<Token> contents_;
};

void InitTerminal() {
  char *term = getenv("TERM");
  tgetent(nullptr, term);
}

}  // namespace editor
}  // namespace afc

int main(int argc, const char* argv[]) {
  using namespace afc::editor;
  InitTerminal();
  TerminalInformation terminal_information;
  //printf("%s", terminal_information.clear_screen_string);

  OpenFile file("editor.cc");

  printf("It works?\n");
  file.Display();
}

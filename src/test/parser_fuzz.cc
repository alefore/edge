#include <glog/logging.h>

#include <csignal>
#include <iostream>
#include <string>

#include "src/audio.h"
#include "src/cpp_parse_tree.h"
#include "src/editor.h"
#include "src/terminal.h"
#include "src/tree.h"

using namespace afc::editor;

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  auto parser = NewCppTreeParser(
      {L"auto", L"int", L"char", L"if", L"while", L"const", L"for"},
      {L"optoins"});

  CHECK_EQ(argc, 2);
  int fd = open(argv[1], O_RDONLY);
  CHECK_GT(fd, 0);

  char BUFFER[64000];
  int len = read(fd, &buffer, sizeof(buffer));
  CHECK_GE(0, len);

  std::wstringstream ss(wstring(buffer, len));
  std::wstring line;

  BufferContents contents;
  while (std::getline(ss, line, '\n')) {
    Line::Options options;
    options.contents = NewLazyString(line);
    contents.AppendToLastLine(Line(std::move(options)));
    contents.push_back(std::make_shared<Line>());
  }

  parser->FindChildren(const BufferContents& lines, ParseTree* root) = 0;

  return 0;
}

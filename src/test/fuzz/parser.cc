#include <glog/logging.h>

#include <csignal>
#include <fstream>
#include <iostream>
#include <string>

extern "C" {
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
}

#include "src/buffer_contents.h"
#include "src/char_buffer.h"
#include "src/cpp_parse_tree.h"
#include "src/lazy_string.h"

using namespace afc::editor;

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  auto parser = NewCppTreeParser(
      {L"auto", L"int", L"char", L"if", L"while", L"const", L"for"},
      {L"optoins"});

  std::wifstream input(argv[1]);

  BufferContents contents;
  for (std::wstring line; std::getline(input, line, L'\n');) {
    Line::Options options;
    options.contents = NewLazyString(line);
    contents.AppendToLine(contents.EndLine(), Line(std::move(options)));
    contents.push_back(std::make_shared<Line>());
  }

  std::cout << "Parsing input: " << contents.ToString();
  ParseTree tree = parser->FindChildren(contents, contents.range());
  std::cout << "Hash: " << tree.hash();
  return 0;
}

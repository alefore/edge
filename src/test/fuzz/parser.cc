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
#include "src/cpp_parse_tree.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"

using namespace afc::editor;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineSequence;

int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);
  auto parser = NewCppTreeParser(
      {L"auto", L"int", L"char", L"if", L"while", L"const", L"for"},
      {L"optoins"}, IdentifierBehavior::kNone);

  std::wifstream input(argv[1]);

  LineSequence contents;
  for (std::wstring line; std::getline(input, line, L'\n');) {
    LineBuilder options(NewLazyString(line));
    contents.AppendToLine(contents.EndLine(), std::move(options).Build());
    contents.push_back(afc::language::MakeNonNullShared<Line>());
  }

  std::cout << "Parsing input: " << contents.ToString();
  ParseTree tree = parser->FindChildren(contents, contents.range());
  std::cout << "Hash: " << tree.hash();
  return 0;
}

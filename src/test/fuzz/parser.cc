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

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/mutable_line_sequence.h"
#include "src/parsers/cpp.h"

using namespace afc::editor;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::MutableLineSequence;

int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);
  auto parser = parsers::NewCppTreeParser(
      ParserId::Cpp(),
      {NonEmptySingleLine{SingleLine{LazyString{L"auto"}}},
       NonEmptySingleLine{SingleLine{LazyString{L"int"}}},
       NonEmptySingleLine{SingleLine{LazyString{L"char"}}},
       NonEmptySingleLine{SingleLine{LazyString{L"if"}}},
       NonEmptySingleLine{SingleLine{LazyString{L"while"}}},
       NonEmptySingleLine{SingleLine{LazyString{L"const"}}},
       NonEmptySingleLine{SingleLine{LazyString{L"for"}}}},
      {NonEmptySingleLine{SingleLine{LazyString{L"optoins"}}}},
      IdentifierBehavior::kNone);

  std::wifstream input(argv[1]);

  MutableLineSequence contents;
  for (std::wstring line; std::getline(input, line, L'\n');) {
    LineBuilder options{SingleLine{LazyString{line}}};
    contents.AppendToLine(contents.EndLine(), std::move(options).Build());
    contents.push_back(Line());
  }

  std::cout << "Parsing input: " << contents.snapshot().ToString();
  ParseTree tree = parser->FindChildren(contents.snapshot(), contents.range());
  std::cout << "Hash: " << tree.hash();
  return 0;
}

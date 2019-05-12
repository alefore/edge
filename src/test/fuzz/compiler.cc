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
#include "src/vm/public/vm.h"

using namespace afc::editor;

int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);
  std::wifstream input(argv[1]);
  wstring error;
  afc::vm::CompileFile(argv[1], afc::vm::Environment::GetDefault(), &error);
  return 0;
}

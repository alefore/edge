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

#include "src/args.h"
#include "src/audio.h"
#include "src/buffer_contents.h"
#include "src/char_buffer.h"
#include "src/cpp_parse_tree.h"
#include "src/editor.h"
#include "src/fuzz.h"
#include "src/lazy_string.h"

using namespace afc::editor;

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);

  CHECK_EQ(argc, 3);

  std::unique_ptr<fuzz::FuzzTestable> fuzz_testable;
  std::string class_name(argv[1]);
  auto audio_player = NewNullAudioPlayer();
  EditorState editor(CommandLineValues(), audio_player.get());
  OpenBuffer::Options options;
  options.editor = &editor;
  OpenBuffer buffer(options);
  if (class_name == "BufferContents") {
    fuzz_testable = std::make_unique<BufferContents>();
  } else if (class_name == "BufferTerminal") {
    fuzz_testable = buffer.NewTerminal();
  }
  CHECK(fuzz_testable != nullptr)
      << "Invalid parameter for class name: " << class_name;

  std::ifstream input(argv[2]);
  fuzz::FuzzTestable::Test(input, fuzz_testable.get());
  return 0;
}

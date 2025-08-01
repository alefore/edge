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
#include "src/buffer_registry.h"
#include "src/cpp_parse_tree.h"
#include "src/editor.h"
#include "src/infrastructure/audio.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/text/mutable_line_sequence.h"
#include "src/tests/fuzz.h"

using afc::language::NonNull;
using afc::language::text::MutableLineSequence;

using namespace afc::editor;

namespace gc = afc::language::gc;
namespace fuzz = afc::tests::fuzz;

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);

  CHECK_EQ(argc, 3);

  std::unique_ptr<fuzz::FuzzTestable> fuzz_testable;
  std::string class_name(argv[1]);
  auto audio_player = afc::infrastructure::audio::NewNullPlayer();
  NonNull<std::unique_ptr<EditorState>> editor =
      EditorState::New(CommandLineValues(), audio_player.value());
  OpenBuffer::Options options{
      .editor = editor.value(),
      .name = editor->buffer_registry().NewAnonymousBufferName()};
  gc::Root<OpenBuffer> buffer = OpenBuffer::New(options);
  if (class_name == "MutableLineSequence") {
    fuzz_testable = std::make_unique<MutableLineSequence>();
  } else if (class_name == "BufferTerminal") {
    fuzz_testable = std::move(buffer.ptr()->NewTerminal().get_unique());
  }
  CHECK(fuzz_testable != nullptr)
      << "Invalid parameter for class name: " << class_name;

  std::ifstream input(argv[2]);
  fuzz::FuzzTestable::Test(input, fuzz_testable.get());
  return 0;
}

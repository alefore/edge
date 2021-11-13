#include "src/args.h"
#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
EditorState* EditorForTests() {
  static auto player = NewNullAudioPlayer();
  static EditorState editor_for_tests(CommandLineValues{}, player.get());
  return &editor_for_tests;
}

std::shared_ptr<OpenBuffer> NewBufferForTests() {
  return OpenBuffer::New({.editor = EditorForTests()});
}

std::wstring GetMetadata(std::wstring line) {
  auto buffer = NewBufferForTests();
  buffer->AppendToLastLine(NewLazyString(line));

  // Gives it a chance to execute:
  EditorForTests()->work_queue()->Execute();

  auto metadata = buffer->LineAt(LineNumber())->metadata();
  auto output = metadata == nullptr ? L"" : metadata->ToString();
  return output;
}

const bool buffer_tests_registration = tests::Register(
    L"BufferTests",
    {{.name = L"MetadataSimpleInt",
      .callback = [] { CHECK(GetMetadata(L"5") == L"5"); }},
     {.name = L"MetadataSimpleDouble",
      .callback = [] { CHECK(GetMetadata(L"2.3") == L"2.3"); }},
     {.name = L"MetadataSimpleString",
      .callback = [] { CHECK(GetMetadata(L"\"xyz\"") == L"\"xyz\""); }},
     {.name = L"MetadataSimpleExpression",
      .callback = [] { CHECK(GetMetadata(L"1 + 2 + 3") == L"6"); }},
     {.name = L"MetadataFunctionPure",
      .callback =
          [] {
            CHECK(GetMetadata(L"[](int x) -> int { return x * 2; }(4)") ==
                  L"8");
          }},
     {.name = L"MetadataImpureDoesNotExecute",
      .callback =
          [] {
            CHECK(GetMetadata(L"buffer.SetStatus(\"xyz\"); 4") ==
                  L"C++: \"int\"");
          }},
     {.name = L"MetadataPurePow",
      .callback = [] { CHECK(GetMetadata(L"2 * pow(5, 3)") == L"250"); }}});
}  // namespace
}  // namespace afc::editor

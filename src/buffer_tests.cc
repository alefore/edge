#include "src/args.h"
#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
std::wstring GetMetadata(std::wstring line) {
  auto buffer = NewBufferForTests();
  buffer->AppendToLastLine(NewLazyString(line));

  // Gives it a chance to execute:
  buffer->editor().work_queue()->Execute();

  auto metadata = buffer->LineAt(LineNumber())->metadata();
  auto output = metadata == nullptr ? L"" : metadata->ToString();
  VLOG(5) << "GetMetadata output: " << output;
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
      .callback = [] { CHECK(GetMetadata(L"2 * pow(5, 3)") == L"250"); }},
     {.name = L"MetadataScientificNotation",
      .callback = [] { CHECK(GetMetadata(L"1e3") == L"1000"); }},
     {.name = L"MetadataScientificNotationPlus",
      .callback = [] { CHECK(GetMetadata(L"1e+3") == L"1000"); }},
     {.name = L"MetadataScientificNotationMinus",
      .callback = [] { CHECK(GetMetadata(L"1e-3") == L"0.001"); }},
     {.name = L"MetadataIntToStringNormal",
      .callback = [] { CHECK(GetMetadata(L"(1).tostring()") == L"\"1\""); }},
     {.name = L"MetadataIntToStringRuntimeError",
      .callback =
          [] {
            CHECK(GetMetadata(L"(1/0).tostring()").substr(0, 3) == L"E: ");
          }}

    });

}  // namespace
}  // namespace afc::editor

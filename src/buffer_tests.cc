#include "src/args.h"
#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::editor {
using concurrent::WorkQueue;
using language::NonNull;
using language::Pointer;

namespace {
std::wstring GetMetadata(std::wstring line) {
  auto buffer = NewBufferForTests();
  buffer->AppendToLastLine(std::move(NewLazyString(line).get_unique()));

  // Gives it a chance to execute:
  buffer->editor().work_queue()->Execute();

  auto metadata = buffer->LineAt(LineNumber())->metadata();
  auto output = metadata == nullptr ? L"" : metadata->ToString();
  VLOG(5) << "GetMetadata output: " << output;
  return output;
}

const bool buffer_tests_registration = tests::Register(
    L"BufferTests",
    {
        {.name = L"MetadataSimpleInt",
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
             }},
        {.name = L"HonorsExistingMetadata",
         .callback =
             [] {
               auto buffer = NewBufferForTests();
               Line::Options options(NewLazyString(L"foo"));
               options.SetMetadata(Line::MetadataEntry{
                   .initial_value = NewLazyString(L"bar"),
                   .value = futures::Past(NonNull<std::shared_ptr<LazyString>>(
                       NewLazyString(L"quux")))});
               buffer->AppendRawLine(
                   std::make_shared<Line>(std::move(options)));
               // Gives it a chance to execute:
               buffer->editor().work_queue()->Execute();
               CHECK(Pointer(buffer->contents().back()->metadata())
                         .Reference()
                         .ToString() == L"quux");
             }},
    });

const bool buffer_work_queue_tests_registration = tests::Register(
    L"BufferWorkQueue",
    {{.name = L"WorkQueueStaysAlive", .callback = [] {
        // Validates that the work queue in a buffer is correctly connected to
        // the work queue in the editor, including not being destroyed early.
        bool keep_going = true;
        std::shared_ptr<WorkQueue> work_queue =
            NewBufferForTests()->work_queue();
        int iterations = 0;
        std::function<void()> callback =
            [work_queue_weak = std::weak_ptr<WorkQueue>(work_queue), &callback,
             &iterations, &keep_going] {
              if (keep_going) {
                CHECK(work_queue_weak.lock() != nullptr);
                work_queue_weak.lock()->Schedule(callback);
              }
              iterations++;
            };
        callback();
        work_queue = nullptr;
        for (int i = 0; i < 10; i++) {
          CHECK_EQ(iterations, i + 1);
          EditorForTests().work_queue()->Execute();
        }
        keep_going = false;
        EditorForTests().work_queue()->Execute();
        CHECK_EQ(iterations, 12);
        EditorForTests().work_queue()->Execute();
        CHECK_EQ(iterations, 12);
      }}});

}  // namespace
}  // namespace afc::editor

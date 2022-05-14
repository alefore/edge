#include "src/args.h"
#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::editor {
using concurrent::WorkQueue;
using language::MakeNonNullShared;
using language::NonNull;
using language::Pointer;
using language::ValueOrError;

namespace gc = language::gc;
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
        {.name = L"MetadataReturnIntToStringRuntimeError",
         .callback =
             [] {
               // TODO(2022-04-24): Figure out why this test fails if we remove
               // the semicolon.
               CHECK(GetMetadata(L"return (1/0).tostring();").substr(0, 3) ==
                     L"E: ");
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
                   MakeNonNullShared<Line>(std::move(options)));
               // Gives it a chance to execute:
               buffer->editor().work_queue()->Execute();
               CHECK(Pointer(buffer->contents().back()->metadata())
                         .Reference()
                         .ToString() == L"quux");
             }},
        {.name = L"PassingParametersPreservesThem",
         .callback =
             [] {
               auto buffer = NewBufferForTests();

               gc::Root<Value> result =
                   buffer
                       ->EvaluateString(
                           L"int F() { return \"foo\".find_last_of(\"o\", 3); }"
                           L" F() == F();")
                       .Get()
                       .value()
                       .value();
               CHECK(result.ptr()->get_bool());
             }},
        {.name = L"NestedStatements",
         .callback =
             [] {
               auto buffer = NewBufferForTests();
               ValueOrError<gc::Root<Value>> result =
                   buffer->EvaluateString(L"{ int v = 5; } v").Get().value();
               CHECK(result.IsError());
             }},
    });

const bool buffer_tests_leaks = tests::Register(L"VMMemoryLeaks", [] {
  auto callback = [](std::wstring code) {
    return tests::Test{
        .name = code.empty() ? L"<empty>" : (L"Code: " + code),
        .callback = [code] {
          auto buffer = NewBufferForTests();
          // We call Reclaim more than once because the first call enables
          // additional symbols to be removed by the 2nd call (because the first
          // call only removes some roots after it has traversed them).
          buffer->editor().gc_pool().Reclaim();
          auto stats_0 = buffer->editor().gc_pool().Reclaim();
          LOG(INFO) << "Start: " << stats_0;

          CHECK(!buffer->editor().work_queue()->NextExecution().has_value());

          buffer->CompileString(code);

          buffer->editor().gc_pool().Reclaim();
          auto stats_1 = buffer->editor().gc_pool().Reclaim();
          LOG(INFO) << "After compile: " << stats_1;
          CHECK_EQ(stats_0.roots, stats_1.roots);
          CHECK_EQ(stats_0.end_total, stats_1.end_total);

          {
            futures::ValueOrError<language::gc::Root<Value>> future_value =
                buffer->EvaluateString(code);
            while (buffer->editor().work_queue()->NextExecution().has_value())
              buffer->editor().work_queue()->Execute();
            future_value.Get().value().value().ptr();
          }

          for (int i = 0; i < 10; i++) buffer->editor().gc_pool().Reclaim();

          auto stats_2 = buffer->editor().gc_pool().Reclaim();
          LOG(INFO) << "Start: " << stats_1;

          // The real assertions of this test are these:
          CHECK_EQ(stats_0.roots, stats_2.roots);
          CHECK_EQ(stats_0.end_total, stats_2.end_total);
        }};
  };
  return std::vector<tests::Test>({
      callback(L""),
      callback(L"5"),
      callback(L"\"foo\";"),
      callback(L"true;"),
      callback(L"false;"),
      callback(L"int x = 5;"),
      callback(L"for (int i = 0; i < 5; i++) i;"),
      callback(L"{"
               L"auto foo = [](int x) -> int { return x * 5; };"
               L"foo(3) * 2;"
               L"\"text\" * 2;"
               L"foo((\"xyz\").size() + 1) - 5;"
               L"int y = 0;"
               L"for (int i = 0; i < 5; i++) { y += foo(i); }"
               L"}"),
  });
}());

const bool buffer_work_queue_tests_registration = tests::Register(
    L"BufferWorkQueue", {{.name = L"WorkQueueStaysAlive", .callback = [] {
                            // Validates that the work queue in a buffer is
                            // correctly connected to the work queue in the
                            // editor, including not being destroyed early.
                            bool keep_going = true;
                            int iterations = 0;
                            NonNull<std::shared_ptr<WorkQueue>> work_queue =
                                NewBufferForTests()->work_queue();
                            std::function<void()> callback =
                                [work_queue_weak = std::weak_ptr<WorkQueue>(
                                     work_queue.get_shared()),
                                 &callback, &iterations, &keep_going] {
                                  if (keep_going) {
                                    CHECK(work_queue_weak.lock() != nullptr);
                                    work_queue_weak.lock()->Schedule(callback);
                                  }
                                  iterations++;
                                };
                            callback();
                            work_queue = WorkQueue::New();
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

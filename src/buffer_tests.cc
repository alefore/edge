#include "src/args.h"
#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::editor {
using concurrent::WorkQueue;
using language::MakeNonNullShared;
using language::NonNull;
using language::Pointer;
using language::ValueOrDie;
using language::ValueOrError;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;

namespace gc = language::gc;
namespace {
std::wstring GetMetadata(std::wstring line) {
  gc::Root<OpenBuffer> buffer = NewBufferForTests();
  buffer.ptr()->Set(buffer_variables::name, L"tests");

  // We add this so that tests can refer to it.
  buffer.ptr()->AppendToLastLine(NewLazyString(L"5.0/2.0"));
  buffer.ptr()->AppendEmptyLine();
  buffer.ptr()->AppendToLastLine(NewLazyString(L"5.0/ does not compile"));
  buffer.ptr()->AppendEmptyLine();

  buffer.ptr()->AppendToLastLine(NewLazyString(line));

  // Gives it a chance to execute:
  buffer.ptr()->editor().work_queue()->Execute();

  auto line_in_buffer = buffer.ptr()->LineAt(buffer.ptr()->EndLine());
  auto metadata = line_in_buffer->metadata();
  auto output = metadata == nullptr ? L"" : metadata->ToString();
  VLOG(5) << "GetMetadata output: " << line_in_buffer->ToString() << ": "
          << output;
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
        {.name = L"MetadataReader",
         .callback =
             [] { CHECK(GetMetadata(L"buffer.name()") == L"\"tests\""); }},
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
               // Needs the semicolon to be a valid statement (unlike the
               // similar MetadataIntToStringRuntimeError test, which is an
               // expression, rather than a statement).
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
               buffer.ptr()->AppendRawLine(
                   MakeNonNullShared<Line>(std::move(options)));
               // Gives it a chance to execute:
               buffer.ptr()->editor().work_queue()->Execute();
               CHECK(Pointer(buffer.ptr()->contents().back()->metadata())
                         .Reference()
                         .ToString() == L"quux");
             }},
        {.name = L"PassingParametersPreservesThem",
         .callback =
             [] {
               auto buffer = NewBufferForTests();

               gc::Root<vm::Value> result = ValueOrDie(
                   buffer.ptr()
                       ->EvaluateString(
                           L"int F() { return \"foo\".find_last_of(\"o\", 3); }"
                           L" F() == F();")
                       .Get()
                       .value(),
                   L"tests");
               CHECK(result.ptr()->get_bool());
             }},
        {.name = L"NestedStatements",
         .callback =
             [] {
               auto buffer = NewBufferForTests();
               ValueOrError<gc::Root<vm::Value>> result =
                   buffer.ptr()
                       ->EvaluateString(L"{ int v = 5; } v")
                       .Get()
                       .value();
               CHECK(IsError(result));
             }},
        {.name = L"LineMetadataString",
         .callback =
             [] {
               CHECK(GetMetadata(L"buffer.LineMetadataString(0)") ==
                     L"\"2.5\"");
             }},
        {.name = L"LineMetadataStringRuntimeError",
         .callback =
             [] {
               CHECK(
                   GetMetadata(L"buffer.LineMetadataString(1)").substr(0, 3) ==
                   L"E: ");
             }},

    });

const bool vm_memory_leaks_tests = tests::Register(L"VMMemoryLeaks", [] {
  auto callback = [](std::wstring code) {
    return tests::Test{
        .name = code.empty() ? L"<empty>" : (L"Code: " + code),
        .callback = [code] {
          auto Reclaim = [] {
            // We call Reclaim more than once because the first call enables
            // additional symbols to be removed by the 2nd call (because the
            // first call only removes some roots after it has traversed
            // them).
            std::optional<size_t> end_total;
            while (true) {
              gc::Pool::FullCollectStats stats =
                  EditorForTests().gc_pool().FullCollect();
              if (end_total == stats.end_total) return stats;
              end_total = stats.end_total;
            }
          };
          auto stats_0 = Reclaim();
          LOG(INFO) << "Start: " << stats_0;

          // We deliberately set things up to let objects be deleted as soon
          // as they are no longer needed, in order to make the tests
          // stronger.
          {
            futures::ValueOrError<gc::Root<vm::Value>> future_value = [&code] {
              std::pair<NonNull<std::unique_ptr<vm::Expression>>,
                        gc::Root<vm::Environment>>
                  compilation_result = [&code] {
                    auto buffer = NewBufferForTests();
                    auto output = ValueOrDie(buffer.ptr()->CompileString(code));
                    auto erase_result =
                        EditorForTests().buffers()->erase(buffer.ptr()->name());
                    CHECK_EQ(erase_result, 1ul);
                    return output;
                  }();

              LOG(INFO) << "Start evaluation.";
              return Evaluate(
                  compilation_result.first.value(), EditorForTests().gc_pool(),
                  compilation_result.second,
                  [](std::function<void()> resume_callback) {
                    EditorForTests().work_queue()->Schedule(
                        WorkQueue::Callback{.callback = resume_callback});
                  });
            }();
            while (!future_value.Get().has_value())
              EditorForTests().work_queue()->Execute();

            ValueOrDie(future_value.Get().value(), L"tests").ptr();
          }

          auto stats_2 = Reclaim();
          LOG(INFO) << "End: " << stats_2;

          // The real assertions of this test are these:
          CHECK_EQ(stats_0.roots, stats_2.roots);
          CHECK_EQ(stats_0.end_total, stats_2.end_total);
        }};
  };
  return std::vector<tests::Test>({
      callback(L""),
      callback(L"5"),
      callback(L"5.2e8"),
      callback(L"\"foo\";"),
      callback(L"true;"),
      callback(L"false;"),
      callback(L"(1 + 2 * 3 - 4) < 12 ? \"f\" : \"t\" * 2"),
      callback(L"int x = 5;"),
      callback(L"namespace Foo { int x = 12; } Foo::x + 4;"),
      callback(L"int Foo(int x) { return x * 5 + 1; }; Foo(Foo(10));"),
      callback(L"for (int i = 0; i < 5; i++) i;"),
      callback(L"sleep(0.001);"),
      // TODO(medium, 2022-05-29): Figure out why the following test fails.
      // callback(L"int foo = 5; double foo = 6; foo + 0.0;"),
      // TODO(medium, 2022-05-29): Figure out why the following test fails. Find
      // a way to make it pass even when `screen` is correctly defined (just not
      // to a VmScreen type).
      // callback(L"screen.set_size(LineColumnDelta(1, 2));"),
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
    L"BufferWorkQueue",
    {{.name = L"WorkQueueStaysAlive", .callback = [] {
        // Validates that the work queue in a buffer is
        // correctly connected to the work queue in the
        // editor, including not being destroyed early.
        bool keep_going = true;
        int iterations = 0;
        NonNull<std::shared_ptr<WorkQueue>> work_queue =
            NewBufferForTests().ptr()->work_queue();
        std::function<void()> callback =
            [work_queue_weak =
                 std::weak_ptr<WorkQueue>(work_queue.get_shared()),
             &callback, &iterations, &keep_going] {
              if (keep_going) {
                CHECK(work_queue_weak.lock() != nullptr);
                work_queue_weak.lock()->Schedule(
                    WorkQueue::Callback{.callback = callback});
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

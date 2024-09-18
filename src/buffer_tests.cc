#include "src/args.h"
#include "src/buffer.h"
#include "src/buffer_name.h"
#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/language/container.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/once_only_function.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::concurrent::WorkQueue;
using afc::infrastructure::screen::CursorsSet;
using afc::language::EraseOrDie;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::Pointer;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineMetadataEntry;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::MutableLineSequence;

namespace afc::editor {
namespace {
std::wstring GetMetadata(std::wstring line) {
  NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
  gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
  buffer.ptr()->Set(buffer_variables::name, LazyString{L"tests"});

  // We add this so that tests can refer to it.
  buffer.ptr()->AppendToLastLine(LazyString{L"5.0/2.0"});
  buffer.ptr()->AppendEmptyLine();
  buffer.ptr()->AppendToLastLine(LazyString{L"5.0/ does not compile"});
  buffer.ptr()->AppendEmptyLine();

  buffer.ptr()->AppendToLastLine(LazyString{line});

  // Gives it a chance to execute:
  buffer.ptr()->editor().work_queue()->Execute();

  auto line_in_buffer = buffer.ptr()->LineAt(buffer.ptr()->EndLine());
  if (const auto metadata_it = line_in_buffer->metadata().find(LazyString{});
      metadata_it != line_in_buffer->metadata().end()) {
    LOG(INFO) << "GetMetadata output: " << line_in_buffer->ToString() << ": ["
              << metadata_it->second.get_value().ToString() << L"]";
    return metadata_it->second.get_value().ToString();
  }
  return L"";
}

const bool buffer_tests_registration = tests::Register(
    L"BufferTests",
    {
        {.name = L"MetadataSimpleInt",
         .callback = [] { CHECK(GetMetadata(L"5") == L"5"); }},
        {.name = L"MetadataStringNotEquals",
         .callback = [] { CHECK(GetMetadata(L"\"x\" != \"x\"") == L"false"); }},
        {.name = L"MetadataSimpleDouble",
         .callback = [] { CHECK(GetMetadata(L"2.3") == L"2.3"); }},
        {.name = L"MetadataInexactDivision",
         .callback = [] { CHECK(GetMetadata(L"1 / 3") == L"0.33333"); }},
        {.name = L"MetadataExactDivision",
         .callback = [] { CHECK(GetMetadata(L"6 / 3") == L"2"); }},
        {.name = L"MetadataSimpleString",
         .callback = [] { CHECK(GetMetadata(L"\"xyz\"") == L"\"xyz\""); }},
        {.name = L"MetadataSimpleExpression",
         .callback = [] { CHECK(GetMetadata(L"1 + 2 + 3") == L"6"); }},
        {.name = L"MetadataFunctionPure",
         .callback =
             [] {
               CHECK(GetMetadata(
                         L"[](number x) -> number { return x * 2; }(4)") ==
                     L"8");
             }},
        {.name = L"MetadataReader",
         .callback =
             [] { CHECK(GetMetadata(L"buffer.name()") == L"\"tests\""); }},
        {.name = L"MetadataLocalVariables",
         .callback =
             [] { CHECK(GetMetadata(L"number x = 2; x * 2") == L"4"); }},
        {.name = L"MetadataImpureDoesNotExecute",
         .callback =
             [] {
               CHECK(GetMetadata(L"buffer.SetStatus(\"xyz\"); 4") ==
                     L"C++: \"number\"");
             }},
        {.name = L"MetadataPurePow",
         .callback = [] { CHECK(GetMetadata(L"2 * pow(5, 3)") == L"250"); }},
        {.name = L"MetadataStringFind",
         .callback =
             [] {
               CHECK(GetMetadata(L"\"foo\".find_first_of(\" \", 0)") == L"-1");
             }},
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
               NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
               auto buffer = NewBufferForTests(editor.value());
               LineBuilder options{SingleLine{LazyString{L"foo"}}};
               options.SetMetadata(
                   {{LazyString{},
                     language::text::LineMetadataEntry{
                         .initial_value = LazyString{L"bar"},
                         .value = futures::Past(LazyString{L"quux"})}}});
               buffer.ptr()->AppendRawLine(std::move(options).Build());
               // Gives it a chance to execute:
               buffer.ptr()->editor().work_queue()->Execute();
               CHECK(buffer.ptr()
                         ->contents()
                         .back()
                         .metadata()
                         .at(LazyString{})
                         .value.get_copy()
                         ->ToString() == L"quux");
             }},
        {.name = L"PassingParametersPreservesThem",
         .callback =
             [] {
               NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
               auto buffer = NewBufferForTests(editor.value());

               gc::Root<vm::Value> result =
                   ValueOrDie(buffer.ptr()
                                  ->EvaluateString(LazyString{
                                      L"number F() { return "
                                      L"\"foo\".find_last_of(\"o\", 3); }"
                                      L" F() == F();"})
                                  .Get()
                                  .value(),
                              L"tests");
               CHECK(result.ptr()->get_bool());
             }},
        {.name = L"NestedStatements",
         .callback =
             [] {
               NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
               auto buffer = NewBufferForTests(editor.value());
               ValueOrError<gc::Root<vm::Value>> result =
                   buffer.ptr()
                       ->EvaluateString(LazyString{L"{ number v = 5; } v"})
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
  auto callback = [](std::wstring code, std::wstring name = L"") {
    return tests::Test{
        .name = name.empty() ? (L"Code: " + code) : name, .callback = [code] {
          NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
          auto Reclaim = [&editor] {
            // We call Reclaim more than once because the first call enables
            // additional symbols to be removed by the 2nd call (because the
            // first call only removes some roots after it has traversed
            // them).
            std::optional<size_t> end_total;
            while (true) {
              gc::Pool::FullCollectStats stats =
                  editor->gc_pool().FullCollect();
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
            futures::ValueOrError<gc::Root<vm::Value>> future_value =
                [&code, &editor] {
                  std::pair<NonNull<std::unique_ptr<vm::Expression>>,
                            gc::Root<vm::Environment>>
                      compilation_result = [&code, &editor] {
                        auto buffer = NewBufferForTests(editor.value());
                        CHECK(editor->current_buffer() == buffer);
                        CHECK_EQ(editor->active_buffers().size(), 1ul);
                        CHECK(editor->active_buffers()[0] == buffer);
                        auto output = ValueOrDie(
                            buffer.ptr()->CompileString(LazyString{code}));
                        editor->CloseBuffer(buffer.ptr().value());
                        return output;
                      }();

                  LOG(INFO) << "Start evaluation.";
                  return Evaluate(
                      std::move(compilation_result.first), editor->gc_pool(),
                      compilation_result.second,
                      [&editor](OnceOnlyFunction<void()> resume_callback) {
                        editor->work_queue()->Schedule(WorkQueue::Callback{
                            .callback = std::move(resume_callback)});
                      });
                }();
            while (!future_value.Get().has_value())
              editor->work_queue()->Execute();

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
      callback(L"", L"empty"),
      callback(L"5"),
      callback(L"5.2e8"),
      callback(L"\"foo\";"),
      callback(L"true;"),
      callback(L"false;"),
      callback(L"(1 + 2 * 3 - 4) < 12 ? \"f\" : \"t\" * 2"),
      callback(L"number x = 5;"),
      callback(L"namespace Foo { number x = 12; } Foo::x + 4;"),
      callback(L"number Foo(number x) { return x * 5 + 1; }; Foo(Foo(10));"),
      callback(L"// Some comment.\n"
               L"editor.SetVariablePrompt(\"blah\");"),
      callback(L"number y = 0;\n"
               L"void Foo(number x) { if (x > y) Foo(x - 1); }\n"
               L"Foo(10);"),
      callback(L"number y = 0;\n"
               L"void Foo(number x) { while (x > y) x--; }\n"
               L"Foo(10);"),
      callback(L"-5;"),
      callback(L"string Foo(number x, number y, string z) { "
               L"while (x > y) x--; return z; }\n"
               L"Foo(10, 0.5, \"blah\");",
               L"WhileLoopAndReturn"),
      callback(L"string Foo() { string x = \"foo\"; return x; }"),
      callback(L"string x = \"foo\"; x = x + \"bar\" * 2;"),
      callback(L"number x = 10; while (x > 10) x--;"),
      callback(L"for (number i = 0; i < 5; i++) i;"),
      callback(L"VectorLineColumn x = buffer.active_cursors();\n"
               L"x.push_back(LineColumn(0, 10));"
               L"buffer.set_active_cursors(x);"),
      callback(L"sleep(0.001);"),
      callback(L"[](number x) -> number { return 0; }"),
      callback(L"number foo = 5; number foo = 6; foo + 0.0;"),
      callback(L"void Foo(number n, string x) { return; }\n"
               L"void Foo(number n) { Foo(n, \"yes\"); }\n"
               L"void Foo(number n, number y, number z) { Foo(n); }\n"
               L"Foo(1, 2, 3);"),
      callback(L"{"
               L"auto foo = [](number x) -> number { return x * 5; };"
               L"foo(3) * 2;"
               L"\"text\" * 2;"
               L"foo((\"xyz\").size() + 1) - 5;"
               L"number y = 0;"
               L"for (number i = 0; i < 5; i++) { y += foo(i); }"
               L"}"),
  });
}());

const bool buffer_work_queue_tests_registration = tests::Register(
    L"BufferWorkQueue",
    {{.name = L"WorkQueueStaysAlive",
      .callback =
          [] {
            NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();

            // Validates that the work queue in a buffer is correctly connected
            // to the work queue in the editor, including not being destroyed
            // early.
            bool keep_going = true;
            int iterations = 0;
            NonNull<std::shared_ptr<WorkQueue>> work_queue =
                NewBufferForTests(editor.value()).ptr()->work_queue();
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
              editor->work_queue()->Execute();
            }
            keep_going = false;
            editor->work_queue()->Execute();
            CHECK_EQ(iterations, 12);
            editor->work_queue()->Execute();
            CHECK_EQ(iterations, 12);
          }},
     {.name = L"DeleteEditor", .callback = [] {
        std::unique_ptr<EditorState> editor = EditorForTests().get_unique();
        futures::Value<int> value = editor->thread_pool().Run([]() {
          LOG(INFO) << "Thread work starting";
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          LOG(INFO) << "Thread work done, returning";
          return 56;
        });
        LOG(INFO) << "Deleting editor";
        editor = nullptr;
        LOG(INFO) << "Editor deleted.";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        LOG(INFO) << "Checking value";
        CHECK(!value.Get());
      }}});

const bool buffer_positions_tests_registration = tests::Register(
    L"BufferPositions",
    {{.name = L"DeleteCursorLeavingOtherPastRange", .callback = [] {
        NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
        gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
        buffer.ptr()->Set(buffer_variables::name, LazyString{L"tests"});
        for (int i = 0; i < 10; i++) buffer.ptr()->AppendEmptyLine();
        CHECK_EQ(buffer.ptr()->position(), LineColumn(LineNumber(0)));
        CHECK_EQ(buffer.ptr()->contents().size(), LineNumberDelta(10 + 1));

        buffer.ptr()->set_position(LineColumn(LineNumber(222)));
        CHECK_EQ(buffer.ptr()->position(), LineColumn(LineNumber(222)));

        buffer.ptr()->CheckPosition();
        CHECK_EQ(buffer.ptr()->position(), LineColumn(LineNumber(10)));

        CursorsSet::iterator insertion_iterator =
            buffer.ptr()->active_cursors().insert(LineColumn(LineNumber(5)));
        CHECK_EQ(buffer.ptr()->position(), LineColumn(LineNumber(10)));

        buffer.ptr()->active_cursors().set_active(insertion_iterator);
        CHECK_EQ(buffer.ptr()->position(), LineColumn(LineNumber(5)));

        buffer.ptr()->ClearContents();

        CHECK_EQ(buffer.ptr()->contents().size(), LineNumberDelta(1));
        CHECK_EQ(buffer.ptr()->position(), LineColumn(LineNumber(5)));

        buffer.ptr()->DestroyCursor();
        CHECK_EQ(buffer.ptr()->position(), LineColumn(LineNumber(0)));
      }}});

const bool buffer_registry_tests_registration = tests::Register(
    L"BufferRegistry",
    {{.name = L"BufferIsCollected",
      .callback =
          [] {
            NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
            std::optional<gc::Root<OpenBuffer>> buffer_root =
                NewBufferForTests(editor.value());
            gc::WeakPtr<OpenBuffer> buffer_weak =
                buffer_root->ptr().ToWeakPtr();
            editor->CloseBuffer(buffer_root->ptr().value());
            buffer_root = std::nullopt;
            CHECK(buffer_weak.Lock().has_value());
            editor->gc_pool().FullCollect();
            editor->gc_pool().BlockUntilDone();
            CHECK(!buffer_weak.Lock().has_value());
          }},
     {.name = L"FuturePasteBufferSurvives", .callback = [] {
        NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
        std::optional<gc::Root<OpenBuffer>> buffer_root =
            OpenBuffer::New(OpenBuffer::Options{.editor = editor.value(),
                                                .name = FuturePasteBuffer{}});
        gc::WeakPtr<OpenBuffer> buffer_weak = buffer_root->ptr().ToWeakPtr();
        editor->buffer_registry().Add(FuturePasteBuffer{}, buffer_weak);
        editor->CloseBuffer(buffer_root->ptr().value());
        buffer_root = std::nullopt;

        editor->gc_pool().FullCollect();
        editor->gc_pool().BlockUntilDone();
        CHECK(buffer_weak.Lock().has_value());

        editor->buffer_registry().Remove(FuturePasteBuffer{});
        editor->gc_pool().FullCollect();
        editor->gc_pool().BlockUntilDone();
        CHECK(!buffer_weak.Lock().has_value());
      }}});

}  // namespace
}  // namespace afc::editor

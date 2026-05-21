#include <memory>

#include "src/buffer.h"
#include "src/buffer_name.h"
#include "src/buffer_registry.h"
#include "src/concurrent/thread_pool.h"
#include "src/editor.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

using namespace afc::language;
using namespace afc::concurrent;
namespace afc::editor {
namespace {
const bool maybe_add_tests_registration = tests::Register(
    L"BufferRegistry_MaybeAdd",
    {{.name = L"ReentrantSimple",
      .callback =
          [] {
            NonNull<std::unique_ptr<EditorState>> editor =
                EditorForTests(std::nullopt);
            BufferRegistry registry(
                [](const OpenBuffer&, const OpenBuffer&) -> std::weak_ordering {
                  LOG(FATAL) << "Unexpected";
                  std::unreachable();
                },
                [](const OpenBuffer&) { return false; }, [](OpenBuffer&) {});
            gc::Root<OpenBuffer> buffer = registry.MaybeAdd(PasteBuffer{}, [&] {
              // This is the key part: no deadlock should happen:
              auto buffers_output = registry.buffers();
              // To avoid optimizations, we validate something with
              // buffers_output:
              CHECK_EQ(buffers_output.size(), 0ul);
              return OpenBuffer::New(OpenBuffer::Options{
                  .editor = editor.value(), .name = PasteBuffer{}});
            });
            CHECK_EQ(registry.buffers().size(), 1ul);
          }},
     {.name = L"ConcurrentMaybeAdd", .callback = [] {
        NonNull<std::unique_ptr<EditorState>> editor =
            EditorForTests(std::nullopt);
        BufferRegistry registry(
            [](const OpenBuffer&, const OpenBuffer&) -> std::weak_ordering {
              LOG(FATAL) << "Unexpected";
              std::unreachable();
            },
            [](const OpenBuffer&) { return false; }, [](OpenBuffer&) {});

        std::atomic<int> factory_calls{0};
        std::atomic<int> threads_running{0};
        const size_t concurrent_calls = 10;
        std::vector<std::optional<gc::Root<OpenBuffer>>> outputs(
            concurrent_calls, std::nullopt);

        {
          ThreadPool pool(L"BufferRegistry_TestPool", 10);
          auto task = [&](size_t index) {
            LOG(INFO) << "Task starts: " << index;
            threads_running++;
            outputs[index] = registry.MaybeAdd(PasteBuffer{}, [&] {
              LOG(INFO) << "Factory starts: " << index;
              factory_calls++;
              while (threads_running.load() < concurrent_calls)
                std::this_thread::yield();
              LOG(INFO) << "Factory returns: " << index;
              return OpenBuffer::New(OpenBuffer::Options{
                  .editor = editor.value(), .name = PasteBuffer{}});
            });
            LOG(INFO) << "Task returns: " << index;
          };
          for (size_t index = 0; index < concurrent_calls; ++index)
            pool.RunIgnoringResult([index, task] { task(index); });
          while (threads_running.load() < concurrent_calls)
            std::this_thread::yield();
          LOG(INFO) << "Allowing pool to be deleted.";
        }

        CHECK_EQ(factory_calls.load(), 1);
        std::ranges::for_each(outputs, [&outputs](auto& output) {
          CHECK_EQ(&outputs[0]->ptr().value(), &output->ptr().value());
        });
        CHECK_EQ(registry.buffers().size(), 1ul);
      }}});
}  // namespace
}  // namespace afc::editor

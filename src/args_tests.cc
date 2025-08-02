#include <glog/logging.h>

#include <ranges>

#include "src/args.h"
#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/editor.h"
#include "src/infrastructure/execution.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/wstring.h"
#include "src/server.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;

using afc::infrastructure::FileDescriptor;
using afc::infrastructure::Path;
using afc::infrastructure::execution::ExecutionEnvironment;
using afc::infrastructure::execution::ExecutionEnvironmentOptions;
using afc::language::IsError;
using afc::language::ValueOrDie;
using afc::language::lazy_string::LazyString;

namespace afc::editor {
namespace {
bool args_tests_registration = tests::Register(
    L"Args", std::invoke([]() -> std::vector<tests::Test> {
      auto add_test = [](std::wstring name,
                         std::function<CommandLineValues()> args,
                         std::function<bool(EditorState&)> stop) {
        return tests::Test{
            .name = name, .callback = [name, args, stop] {
              language::NonNull<std::unique_ptr<EditorState>> editor =
                  EditorForTests(
                      Path{LazyString{L"/home/xxx-unexistent/.edge"}});
              CHECK_EQ(editor->buffer_registry().buffers().size(), 0ul);
              infrastructure::Path server_address =
                  VALUE_OR_DIE(StartServer(editor.value(), std::nullopt));
              CHECK_EQ(editor->buffer_registry().buffers().size(), 1ul);
              CHECK(!editor->exit_value().has_value());

              size_t iteration = 0;
              ExecutionEnvironment(
                  ExecutionEnvironmentOptions{
                      .stop_check =
                          [&] {
                            // We require at least kRequiredIterations to allow
                            // various initialization tasks to happen.
                            const static size_t kRequiredIterations = 10;
                            return iteration > kRequiredIterations &&
                                   stop(editor.value());
                          },
                      .get_next_alarm =
                          [&] { return editor->WorkQueueNextExecution(); },
                      .on_signals = [] {},
                      .on_iteration =
                          [&](afc::infrastructure::execution::IterationHandler&
                                  handler) {
                            LOG(INFO) << "Iteration: " << iteration;
                            editor->ExecutionIteration(handler);
                            if (iteration == 0) {
                              FileDescriptor client_fd = VALUE_OR_DIE(
                                  SyncConnectToServer(server_address));
                              CHECK(!IsError(SyncSendCommandsToServer(
                                  client_fd, CommandsToRun(args()))));
                            }
                            iteration++;
                            CHECK_LT(iteration, 1000ul);
                          }})
                  .Run();
            }};
      };
      auto get_buffer = [](const BufferName& name, const EditorState& editor) {
        const std::optional<gc::Root<OpenBuffer>> result =
            editor.buffer_registry().Find(name);
        LOG(INFO) << "Checking for: " << name << ": "
                  << (result.has_value() ? "present" : "absent");
        return result;
      };
      auto has_buffer = [get_buffer](const BufferName& name,
                                     const EditorState& editor) -> bool {
        return get_buffer(name, editor).has_value();
      };
      return {add_test(
                  L"DefaultArguments", [] { return CommandLineValues(); },
                  std::bind_front(has_buffer, BufferName{CommandBufferName{
                                                  LazyString{L"💻shell"}}})),
              std::invoke([&] {
                std::vector<LazyString> paths = {LazyString{L"/foo/bar"},
                                                 LazyString{L"/tmp"}};
                return add_test(
                    L"File",
                    [paths] {
                      CommandLineValues output;
                      output.naked_arguments = paths;
                      return output;
                    },
                    [get_buffer, paths](EditorState& editor) {
                      return std::all_of(
                          paths.begin(), paths.end(), [&](LazyString path_str) {
                            auto buffer = get_buffer(
                                BufferName{BufferFileId{
                                    VALUE_OR_DIE(Path::New(path_str))}},
                                editor);
                            return buffer.has_value() && !buffer.value()
                                                              ->work_queue()
                                                              ->NextExecution()
                                                              .has_value();
                          });
                    });
              })};
    }));
}  // namespace
}  // namespace afc::editor

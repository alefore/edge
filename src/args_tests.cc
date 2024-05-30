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
using afc::language::ToByteString;
using afc::language::ValueOrDie;
using afc::language::lazy_string::LazyString;

namespace afc::editor {
namespace {
bool server_tests_registration = tests::Register(
    L"Args", std::invoke([]() -> std::vector<tests::Test> {
      auto add_test = [](std::wstring name,
                         std::function<CommandLineValues()> args,
                         std::function<bool(EditorState&)> stop) {
        return tests::Test{
            .name = name, .callback = [name, args, stop] {
              language::NonNull<std::unique_ptr<EditorState>> editor =
                  EditorForTests();
              CHECK_EQ(editor->buffer_registry().buffers().size(), 0ul);
              infrastructure::Path server_address =
                  ValueOrDie(StartServer(editor.value(), std::nullopt));
              CHECK_EQ(editor->buffer_registry().buffers().size(), 1ul);
              CHECK(!editor->exit_value().has_value());

              size_t iteration = 0;
              ExecutionEnvironment(
                  ExecutionEnvironmentOptions{
                      .stop_check = [&] { return stop(editor.value()); },
                      .get_next_alarm =
                          [&] { return editor->WorkQueueNextExecution(); },
                      .on_signals = [] {},
                      .on_iteration =
                          [&](afc::infrastructure::execution::IterationHandler&
                                  handler) {
                            LOG(INFO) << "Iteration: " << iteration;
                            editor->ExecutionIteration(handler);
                            if (iteration == 0) {
                              FileDescriptor client_fd = ValueOrDie(
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
      auto has_buffer = [](const BufferName& name,
                           const EditorState& editor) -> bool {
        const bool result = editor.buffer_registry().Find(name).has_value();
        LOG(INFO) << "Checking for: " << name << ": "
                  << (result ? "present" : "absent");
        return result;
      };
      return {add_test(
                  L"DefaultArguments", [] { return CommandLineValues(); },
                  std::bind_front(has_buffer, BufferName{CommandBufferName{
                                                  LazyString{L"💻shell"}}})),
              add_test(
                  L"File",
                  [] {
                    CommandLineValues output;
                    output.naked_arguments = {L"/foo/bar", L"/tmp"};
                    return output;
                  },
                  [has_buffer](EditorState& editor) {
                    return has_buffer(BufferName{BufferFileId{ValueOrDie(
                                          Path::FromString(L"/foo/bar"))}},
                                      editor) &&
                           has_buffer(BufferName{BufferFileId{ValueOrDie(
                                          Path::FromString(L"/tmp"))}},
                                      editor);
                  })};
    }));
}  // namespace
}  // namespace afc::editor

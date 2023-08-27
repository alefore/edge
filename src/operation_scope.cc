#include "src/operation_scope.h"

#include <map>

#include "src/buffer.h"
#include "src/buffer_display_data.h"
#include "src/buffer_variables.h"
#include "src/concurrent/protected.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace gc = language::gc;
using language::lazy_string::ColumnNumberDelta;
using language::text::LineColumnDelta;
using language::text::LineNumberDelta;

OperationScopeBufferInformation OperationScope::get(
    const OpenBuffer& buffer) const {
  return data_.lock([&](Map& data) -> const OperationScopeBufferInformation& {
    std::pair<Map::iterator, bool> insert_results =
        data.insert({&buffer, OperationScopeBufferInformation()});
    if (insert_results.second) {
      insert_results.first->second = {
          .screen_lines = buffer.display_data()
                              .view_size()
                              .Get()
                              .value_or(LineColumnDelta{LineNumberDelta{24},
                                                        ColumnNumberDelta{80}})
                              .line,
          .line_marks = buffer.GetLineMarks(),
          .margin_lines_ratio =
              buffer.Read(buffer_variables::margin_lines_ratio)};
    }
    DVLOG(4) << "OperationScope::get(" << &buffer
             << "): Lines: " << insert_results.first->second.screen_lines;
    return insert_results.first->second;
  });
}

namespace {
const bool tests_registration =
    tests::Register(L"OperationScope::get", []() -> std::vector<tests::Test> {
      auto T = [](std::wstring name, auto callback) {
        return tests::Test{
            .name = name, .callback = [callback] {
              std::vector<gc::Root<OpenBuffer>> buffers;
              for (int i = 0; i < 5; i++)
                buffers.push_back(NewBufferForTests());
              for (int i = 0; i < 5; i++)
                buffers[i].ptr().value().display_data().view_size().Set(
                    LineColumnDelta(LineNumberDelta(3 + 10 * i),
                                    ColumnNumberDelta(100)));
              OperationScope operation_scope;
              for (auto& b : buffers) operation_scope.get(b.ptr().value());
              callback(operation_scope, buffers);
            }};
      };

      return {
          T(L"FirstCall",
            [](auto& scope, auto& buffers) {
              CHECK_EQ(scope.get(buffers[1].ptr().value()).screen_lines,
                       LineNumberDelta(13));
            }),
          T(L"Stable",
            [](auto& scope, auto& buffers) {
              buffers[0].ptr().value().display_data().view_size().Set(
                  LineColumnDelta(LineNumberDelta(147), ColumnNumberDelta(80)));
              CHECK_EQ(scope.get(buffers[1].ptr().value()).screen_lines,
                       LineNumberDelta(13));
            }),
          T(L"MultipleBuffers", [](auto& scope, auto& buffers) {
            for (int i = 0; i < 5; i++)
              buffers[i].ptr().value().display_data().view_size().Set(
                  LineColumnDelta(LineNumberDelta(2 + 5 * i),
                                  ColumnNumberDelta(80)));
            for (int i = 0; i < 5; i++)
              CHECK_EQ(scope.get(buffers[i].ptr().value()).screen_lines,
                       LineNumberDelta(3 + 10 * i));
          })};
    }());

}
}  // namespace afc::editor

#include "src/transformation/cursors.h"

#include <glog/logging.h>

#include "src/futures/futures.h"
#include "src/language/lazy_string/append.h"

namespace afc::editor::transformation {
using language::text::LineColumn;

futures::Value<Result> ApplyBase(const Cursors& parameters, Input input) {
  std::vector<LineColumn> positions = {parameters.active};
  bool skipped = false;
  for (const auto& cursor : parameters.cursors) {
    if (!skipped && cursor == parameters.active) {
      skipped = true;
    } else {
      positions.push_back(cursor);
    }
  }
  input.adapter.SetActiveCursors(positions);
  return futures::Past(Result(parameters.active));
}

std::wstring ToStringBase(const Cursors& v) {
  return L"Cursors{.size = " + std::to_wstring(v.cursors.size()) + L"};";
}

Cursors OptimizeBase(Cursors cursors) { return cursors; }

}  // namespace afc::editor::transformation

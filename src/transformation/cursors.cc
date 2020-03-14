#include "src/transformation/cursors.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/lazy_string_append.h"
#include "src/transformation/type.h"

namespace afc::editor::transformation {
futures::Value<Transformation::Result> ApplyBase(const Cursors& parameters,
                                                 Transformation::Input input) {
  CHECK(input.buffer != nullptr);
  vector<LineColumn> positions = {parameters.active};
  bool skipped = false;
  for (const auto& cursor : parameters.cursors) {
    if (!skipped && cursor == parameters.active) {
      skipped = true;
    } else {
      positions.push_back(cursor);
    }
  }
  input.buffer->set_active_cursors(positions);
  return futures::Past(Transformation::Result(input.position));
}
}  // namespace afc::editor::transformation

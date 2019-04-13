#include "src/cursors_transformation.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/transformation_delete.h"

namespace afc {
namespace editor {
namespace {

class SetCursorsTransformation : public Transformation {
 public:
  SetCursorsTransformation(CursorsSet cursors, LineColumn active)
      : cursors_(std::move(cursors)), active_(active) {}

  void Apply(EditorState*, OpenBuffer* buffer, Result* result) const override {
    CHECK(buffer != nullptr);
    CHECK(result != nullptr);
    vector<LineColumn> positions = {active_};
    bool skipped = false;
    for (const auto& cursor : cursors_) {
      if (!skipped && cursor == active_) {
        skipped = true;
      } else {
        positions.push_back(cursor);
      }
    }
    buffer->set_active_cursors(positions);
  }

  unique_ptr<Transformation> Clone() const override {
    return NewSetCursorsTransformation(cursors_, active_);
  }

 private:
  const CursorsSet cursors_;
  const LineColumn active_;
};

}  // namespace

unique_ptr<Transformation> NewSetCursorsTransformation(CursorsSet cursors,
                                                       LineColumn active) {
  return std::make_unique<SetCursorsTransformation>(cursors, active);
}

}  // namespace editor
}  // namespace afc

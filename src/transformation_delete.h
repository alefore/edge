#include <memory>

#include "editor.h"
#include "transformation.h"

namespace afc {
namespace editor {

using std::unique_ptr;

struct DeleteOptions {
  Modifiers modifiers;
  bool copy_to_paste_buffer = true;

  enum class LineEndBehavior { kStop, kDelete };
  LineEndBehavior line_end_behavior = LineEndBehavior::kDelete;
};

std::ostream& operator<<(std::ostream& os, const DeleteOptions& options);

unique_ptr<Transformation> NewDeleteLinesTransformation(DeleteOptions options);
unique_ptr<Transformation> NewDeleteBufferTransformation(DeleteOptions options);
unique_ptr<Transformation> NewDeleteTransformation(DeleteOptions options);

}  // namespace editor
}  // namespace afc

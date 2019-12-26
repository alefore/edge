#include <memory>

#include "src/editor.h"
#include "src/transformation.h"
#include "src/vm/public/environment.h"

namespace afc::editor {

using std::unique_ptr;

struct DeleteOptions {
  std::wstring Serialize() const;

  Modifiers modifiers;
  bool copy_to_paste_buffer = true;

  enum class LineEndBehavior { kStop, kDelete };
  LineEndBehavior line_end_behavior = LineEndBehavior::kDelete;

  // If set, overrides the mode passed when the transformation is executed. This
  // is used by CompositeTransformations that want to effectively erase text
  // even in kPreview mode.
  std::optional<Transformation::Result::Mode> mode;
};

std::ostream& operator<<(std::ostream& os, const DeleteOptions& options);

std::unique_ptr<Transformation> NewDeleteTransformation(DeleteOptions options);

void RegisterDeleteTransformation(vm::Environment* environment);
}  // namespace afc::editor

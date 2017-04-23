#include <memory>

#include "editor.h"
#include "transformation.h"

namespace afc {
namespace editor {

using std::unique_ptr;

struct DeleteOptions {
  Modifiers modifiers;
  bool copy_to_paste_buffer = true;

  // If true, instead of deleting the region affected, just dim it.
  bool preview = false;
};

unique_ptr<Transformation> NewDeleteCharactersTransformation(
    DeleteOptions options);
unique_ptr<Transformation> NewDeleteRegionTransformation(DeleteOptions options);
unique_ptr<Transformation> NewDeleteLinesTransformation(DeleteOptions options);
unique_ptr<Transformation> NewDeleteBufferTransformation(DeleteOptions options);
unique_ptr<Transformation> NewDeleteTransformation(DeleteOptions options);

}  // namespace editor
}  // namespace afc

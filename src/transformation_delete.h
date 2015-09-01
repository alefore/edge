#include <memory>

#include "editor.h"
#include "transformation.h"

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<Transformation> NewDeleteCharactersTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteWordsTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteLinesTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteBufferTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteRegionTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer);

}  // namespace editor
}  // namespace afc

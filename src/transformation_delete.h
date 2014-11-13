#include <memory>

#include "editor.h"
#include "transformation.h"

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<Transformation> NewDeleteCharactersTransformation(
    Direction direction, size_t repetitions, bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteWordsTransformation(
    size_t repetitions, StructureModifier structure_modifier,
    bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteLinesTransformation(
    size_t repetitions, StructureModifier structure_modifier,
    bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteBufferTransformation(
    StructureModifier structure_modifier,
    bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteTransformation(
    Structure structure, StructureModifier structure_modifier,
    Direction direction, size_t repetitions, bool copy_to_paste_buffer);

}  // namespace editor
}  // namespace afc

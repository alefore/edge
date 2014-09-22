#include <memory>

#include "transformation.h"

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<Transformation> NewDeleteCharactersTransformation(
    bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteWordsTransformation(
    bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteLinesTransformation(
    bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteTransformation(bool copy_to_paste_buffer);

}  // namespace editor
}  // namespace afc

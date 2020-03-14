#ifndef __AFC_EDITOR_CURSORS_TRANSFORMATION_H__
#define __AFC_EDITOR_CURSORS_TRANSFORMATION_H__

#include <glog/logging.h>

#include <list>
#include <memory>

#include "src/buffer.h"
#include "src/transformation.h"

namespace afc {
namespace editor {

std::unique_ptr<Transformation> NewSetCursorsTransformation(CursorsSet cursors,
                                                            LineColumn active);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_CURSORS_TRANSFORMATION_H__

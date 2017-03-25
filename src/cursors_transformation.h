#ifndef __AFC_EDITOR_CURSORS_TRANSFORMATION_H__
#define __AFC_EDITOR_CURSORS_TRANSFORMATION_H__

#include <list>
#include <memory>

#include <glog/logging.h>

#include "transformation.h"
#include "buffer.h"

namespace afc {
namespace editor {

std::unique_ptr<Transformation> NewSetCursorsTransformation(
    CursorsSet cursors, LineColumn active);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_CURSORS_TRANSFORMATION_H__

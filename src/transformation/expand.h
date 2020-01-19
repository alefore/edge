#ifndef __AFC_EDITOR_TRANSFORMATION_EXPAND_H__
#define __AFC_EDITOR_TRANSFORMATION_EXPAND_H__

#include <memory>

#include "src/transformation.h"

namespace afc::editor {
std::unique_ptr<Transformation> NewExpandTransformation();
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_EXPAND_H__

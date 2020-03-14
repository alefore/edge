#ifndef __AFC_EDITOR_CURSORS_TRANSFORMATION_H__
#define __AFC_EDITOR_CURSORS_TRANSFORMATION_H__

#include <glog/logging.h>

#include <list>
#include <memory>

#include "src/buffer.h"
#include "src/transformation.h"

namespace afc::editor::transformation {
struct Cursors {
  editor::CursorsSet cursors;
  editor::LineColumn active;
};

futures::Value<Transformation::Result> ApplyBase(const Cursors& parameters,
                                                 Transformation::Input input);
}  // namespace afc::editor::transformation

#endif  // __AFC_EDITOR_CURSORS_TRANSFORMATION_H__

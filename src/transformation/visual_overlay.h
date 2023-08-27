#ifndef __AFC_EDITOR_TRANSFORMATION_VISUAL_OVERLAY_H__
#define __AFC_EDITOR_TRANSFORMATION_VISUAL_OVERLAY_H__

#include <memory>

#include "src/futures/futures.h"
#include "src/language/text/line_column.h"
#include "src/transformation/input.h"
#include "src/transformation/result.h"
#include "src/visual_overlay.h"
#include "src/vm/public/environment.h"

namespace afc::editor::transformation {
struct VisualOverlay {
  VisualOverlayMap visual_overlay_map;
};

futures::Value<Result> ApplyBase(const VisualOverlay& parameters, Input input);
std::wstring ToStringBase(const VisualOverlay& parameters);
VisualOverlay OptimizeBase(VisualOverlay transformation);
}  // namespace afc::editor::transformation

#endif  // __AFC_EDITOR_TRANSFORMATION_VISUAL_OVERLAY_H__

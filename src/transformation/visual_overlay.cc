#include "src/transformation/visual_overlay.h"

#include "src/buffer.h"
#include "src/futures/futures.h"

namespace afc::editor::transformation {

futures::Value<Result> ApplyBase(const VisualOverlay& parameters, Input input) {
  VisualOverlayMap previous_value =
      input.buffer.SetVisualOverlayMap(parameters.visual_overlay_map);
  Result result(input.position);
  result.undo_stack->PushBack(
      VisualOverlay{.visual_overlay_map = previous_value});
  return futures::Past(std::move(result));
}

std::wstring ToStringBase(const VisualOverlay&) { return L"VisualOverlay"; }

VisualOverlay OptimizeBase(VisualOverlay transformation) {
  return transformation;
}

}  // namespace afc::editor::transformation

#ifndef __AFC_EDITOR_TRANSFORMATION_TYPE_H__
#define __AFC_EDITOR_TRANSFORMATION_TYPE_H__

#include "src/transformation_composite.h"
#include "src/transformation_repetitions.h"
#include "src/transformation_stack.h"
#include "src/transformation_variant.h"
#include "src/transformation_visual_overlay.h"

namespace afc::editor::transformation {
void Register(vm::Environment* environment);

void BaseTransformationRegister(vm::Environment* environment);

struct Result;
struct Input;
futures::Value<Result> Apply(Variant base_transformation, const Input& input);

std::wstring ToString(const Variant& transformation);

Variant Optimize(Variant transformation);

}  // namespace afc::editor::transformation
#endif

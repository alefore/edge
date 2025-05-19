#ifndef __AFC_EDITOR_TRANSFORMATION_TYPE_H__
#define __AFC_EDITOR_TRANSFORMATION_TYPE_H__

#include "src/transformation/composite.h"
#include "src/transformation/repetitions.h"
#include "src/transformation/stack.h"
#include "src/transformation/variant.h"
#include "src/transformation/visual_overlay.h"

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

#ifndef __AFC_EDITOR_TRANSFORMATION_TYPE_H__
#define __AFC_EDITOR_TRANSFORMATION_TYPE_H__

#include <optional>
#include <variant>

#include "src/transformation.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/repetitions.h"
#include "src/transformation/set_position.h"
#include "src/vm/public/environment.h"

namespace afc::editor::transformation {
using BaseTransformation =
    std::variant<Delete, Insert, Repetitions, SetPosition>;

void Register(vm::Environment* environment);

void BaseTransformationRegister(vm::Environment* environment);

std::unique_ptr<Transformation> Build(BaseTransformation base_transformation);
}  // namespace afc::editor::transformation
#endif

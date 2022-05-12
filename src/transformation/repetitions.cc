#include "src/transformation/repetitions.h"

#include "src/char_buffer.h"
#include "src/server.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm_transformation.h"

namespace afc {
using language::NonNull;

namespace gc = language::gc;
namespace vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::transformation::Repetitions>> {
  static std::shared_ptr<editor::transformation::Repetitions> get(
      Value* value) {
    CHECK(value != nullptr);
    CHECK(value->type.type == VMType::OBJECT_TYPE);
    CHECK(value->type.object_type == L"RepetitionsTransformationBuilder");
    CHECK(value->user_value != nullptr);
    return std::static_pointer_cast<editor::transformation::Repetitions>(
        value->user_value);
  }
  static NonNull<Value::Ptr> New(
      gc::Pool& pool,
      std::shared_ptr<editor::transformation::Repetitions> value) {
    return Value::NewObject(pool, L"RepetitionsTransformationBuilder",
                            std::shared_ptr<void>(value, value.get()));
  }
  static const VMType vmtype;
};

const VMType
    VMTypeMapper<std::shared_ptr<editor::transformation::Repetitions>>::vmtype =
        VMType::ObjectType(L"RepetitionsTransformationBuilder");
}  // namespace vm
namespace editor::transformation {
futures::Value<Result> ApplyBase(const Repetitions& options, Input input) {
  struct Data {
    size_t index = 0;
    std::unique_ptr<Result> output;
    Repetitions options;
  };
  auto data = std::make_shared<Data>();
  data->output = std::make_unique<Result>(input.position);
  data->options = options;
  return futures::While([data, input]() mutable {
           if (data->index == data->options.repetitions) {
             return futures::Past(futures::IterationControlCommand::kStop);
           }
           data->index++;
           return Apply(*data->options.transformation,
                        input.NewChild(data->output->position))
               .Transform([data](Result result) {
                 bool made_progress = result.made_progress;
                 data->output->MergeFrom(std::move(result));
                 return made_progress && data->output->success
                            ? futures::IterationControlCommand::kContinue
                            : futures::IterationControlCommand::kStop;
               });
         })
      .Transform([data](futures::IterationControlCommand) {
        return std::move(*data->output);
      });
}

std::wstring ToStringBase(const Repetitions& v) {
  return L"Repetitions(" + std::to_wstring(v.repetitions) + L", " +
         ToString(*v.transformation) + L")";
}

Variant OptimizeBase(Repetitions transformation) {
  if (transformation.repetitions == 1)
    return std::move(*transformation.transformation);
  return transformation;
}
}  // namespace editor::transformation
}  // namespace afc

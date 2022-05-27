#include "src/transformation/repetitions.h"

#include "src/char_buffer.h"
#include "src/server.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/transformation/vm.h"

namespace afc {
using language::NonNull;

namespace gc = language::gc;
namespace vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::transformation::Repetitions>> {
  static std::shared_ptr<editor::transformation::Repetitions> get(
      Value& value) {
    // TODO(easy, 2022-05-27): Just return the NonNull.
    return value.get_user_value<editor::transformation::Repetitions>(vmtype)
        .get_shared();
  }
  static gc::Root<Value> New(
      gc::Pool& pool,
      std::shared_ptr<editor::transformation::Repetitions> value) {
    // TODO(2022-05-27, easy): Receive `value` as NonNull.
    return Value::NewObject(
        pool, vmtype.object_type,
        NonNull<std::shared_ptr<editor::transformation::Repetitions>>::Unsafe(
            value));
  }
  static const VMType vmtype;
};

const VMType
    VMTypeMapper<std::shared_ptr<editor::transformation::Repetitions>>::vmtype =
        VMType::ObjectType(
            VMTypeObjectTypeName(L"RepetitionsTransformationBuilder"));
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

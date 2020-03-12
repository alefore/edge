#include "src/transformation/repetitions.h"

#include "src/char_buffer.h"
#include "src/server.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm_transformation.h"

namespace afc {
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
  static Value::Ptr New(
      std::shared_ptr<editor::transformation::Repetitions> value) {
    return Value::NewObject(L"RepetitionsTransformationBuilder",
                            std::shared_ptr<void>(value, value.get()));
  }
  static const VMType vmtype;
};

const VMType
    VMTypeMapper<std::shared_ptr<editor::transformation::Repetitions>>::vmtype =
        VMType::ObjectType(L"RepetitionsTransformationBuilder");
}  // namespace vm
namespace editor::transformation {
futures::Value<Transformation::Result> ApplyBase(const Repetitions& options,
                                                 Transformation::Input input) {
  CHECK(input.buffer != nullptr);
  struct Data {
    size_t index = 0;
    std::unique_ptr<Transformation::Result> output;
    Repetitions options;
  };
  auto data = std::make_shared<Data>();
  data->output = std::make_unique<Transformation::Result>(input.position);
  data->options = options;
  return futures::Transform(
      futures::While([data, input]() mutable {
        if (data->index == data->options.repetitions) {
          return futures::Past(futures::IterationControlCommand::kStop);
        }
        data->index++;
        Transformation::Input current_input(input.buffer);
        current_input.mode = input.mode;
        current_input.position = data->output->position;
        return futures::Transform(
            data->options.transformation->Apply(current_input),
            [data](Transformation::Result result) {
              bool made_progress = result.made_progress;
              data->output->MergeFrom(std::move(result));
              return made_progress && data->output->success
                         ? futures::IterationControlCommand::kContinue
                         : futures::IterationControlCommand::kStop;
            });
      }),
      [data](const futures::IterationControlCommand&) {
        return std::move(*data->output);
      });
}

}  // namespace editor::transformation
}  // namespace afc

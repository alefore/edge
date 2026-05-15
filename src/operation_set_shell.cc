#include "src/infrastructure/extended_char.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_builder.h"
#include "src/operation_bisect.h"
#include "src/transformation_bisect.h"

using namespace afc::language::lazy_string;
using namespace afc::language::text;
using namespace afc::language;
using namespace afc::infrastructure;

namespace afc::editor::operation::commands {
const Description& SetShellDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🌀");
  return output;
}

namespace {

class Impl : public MoveOperationCommand {
  language::lazy_string::SingleLine input_;

 public:
  LineBuilder status() const override {
    LineBuilder output;
    SerializeCall(SetShellDescription().read(), {input_}, output);
    return output;
  }

  transformation::Stack GetTransformation(
      const NonNull<std::shared_ptr<OperationScope>>&,
      transformation::Stack& stack) const override {
    stack.post_transformation_behavior =
        transformation::Stack::PostTransformationBehavior::CommandSystem;
    stack.shell = transformation::ShellCommand(input_.read());
    return transformation::Stack{};
  }

  KeyCommandsMap key_commands_map(Receiver) override {
    KeyCommandsMap cmap;
    cmap.Insert(ControlChar::Backspace,
                {.category = KeyCommandsMap::Category::StringControl,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Backspace")},
                 .active = !input_.empty(),
                 .handler =
                     [this](ExtendedChar) {
                       input_ = input_.Substring(
                           ColumnNumber{0},
                           input_.size() - ColumnNumberDelta{1});
                     }})
        .SetFallback({'\n', ControlChar::Escape, ControlChar::Backspace},
                     [this](ExtendedChar extended_c) {
                       std::visit(overload{[](ControlChar) {},
                                           [&](wchar_t c) {
                                             input_ += SingleLine{LazyString{
                                                 ColumnNumberDelta{1}, c}};
                                           }},
                                  extended_c);
                     });
    return cmap;
  }

  commands::Repetitions* repetitions() override { return nullptr; }
};
}  // namespace
NonNull<std::shared_ptr<MoveOperationCommand>> SetShell() {
  return MakeNonNullShared<Impl>();
}
}  // namespace afc::editor::operation::commands

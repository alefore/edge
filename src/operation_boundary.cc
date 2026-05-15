#include "src/operation_boundary.h"

#include "src/goto_command.h"
#include "src/infrastructure/extended_char.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_builder.h"
#include "src/transformation_bisect.h"

using namespace afc::language::lazy_string;
using namespace afc::language::text;
using namespace afc::language;
using namespace afc::infrastructure;

namespace afc::editor::operation::commands {
const Description& HomeLeftDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🏠👈");
  return output;
}
const Description& HomeRightDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🏠👉");
  return output;
}
const Description& HomeUpDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🏠👆");
  return output;
}
const Description& HomeDownDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🏠👇");
  return output;
}

namespace {
class Impl : public MoveOperationCommand {
  ValueWithDefault<Structure> structure_;
  commands::Repetitions repetitions_;
  Direction direction_;

 public:
  Impl(BoundaryOptions options)
      : structure_(options.structure),
        repetitions_(options.repetitions),
        direction_(options.direction) {}

  LineBuilder status() const override {
    LineBuilder output;
    commands::SerializeCall(
        (direction_ == Direction::Backwards
             ? (structure_ == Structure::Line ? HomeUpDescription()
                                              : HomeRightDescription())
             : (structure_ == Structure::Line ? HomeDownDescription()
                                              : HomeLeftDescription()))
            .read(),
        {commands::StructureToString(structure_.value()).read(),
         repetitions_.ToString()},
        output);
    return output;
  }

  transformation::Variant GetTransformation(
      const NonNull<std::shared_ptr<OperationScope>>&,
      transformation::Stack&) const override {
    // TODO(2026-05-14, P2): Why does we have our own direction (rather
    // than use from `repetitions`)? Maybe fix that and then delete this copy of
    // Repetitions' module's `GetModifiers`?
    auto GetModifiers = [](Structure structure,
                           const commands::Repetitions& repetitions,
                           Direction direction) {
      int repetitions_int = repetitions.get();
      return Modifiers{.structure = structure,
                       .direction = repetitions_int < 0
                                        ? ReverseDirection(direction)
                                        : direction,
                       .repetitions = abs(repetitions_int)};
    };

    return transformation::ModifiersAndComposite{
        .modifiers = GetModifiers(structure_.value(), repetitions_, direction_),
        .transformation = MakeNonNullUnique<GotoTransformation>(0)};
  }

  KeyCommandsMap key_commands_map(Receiver) override {
    KeyCommandsMap cmap;
    if (structure_ == Structure::Line) {
      auto handler = [&](Description description) {
        return KeyCommandsMap::KeyCommand{
            .category = KeyCommandsMap::Category::Repetitions,
            .description = description,
            .handler = [this](ExtendedChar t) {
              int delta = (t == ExtendedChar(L'j') ||
                           t == ExtendedChar(ControlChar::DownArrow))
                              ? 1
                              : -1;
              if (direction_ == Direction::Backwards) {
                delta *= -1;
              }
              repetitions_.sum(delta);
            }};
      };
      cmap.Insert({L'j', ControlChar::DownArrow},
                  handler(Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"👇")}))
          .Insert({L'k', ControlChar::UpArrow},
                  handler(Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"👆")}));
    }

    AddSetStructureChar(cmap, structure_, repetitions_);
    repetitions_.LeftRightKeyCommandsMap(cmap);
    repetitions_.ExtendKeyCommandsMap(cmap);

    if (structure_ == Structure::Char || structure_ == Structure::Line) {
      // Don't let ExtendKeyCommandsMap above handle these; we'd rather preserve
      // the usual meaning (of scrolling by a character).
      cmap.Erase(L'h').Erase(L'l');
    }
    return cmap;
  }

  Repetitions* repetitions() override { return &repetitions_; }
};
}  // namespace
NonNull<std::shared_ptr<MoveOperationCommand>> Boundary(
    BoundaryOptions options) {
  return MakeNonNullShared<Impl>(std::move(options));
}
}  // namespace afc::editor::operation::commands

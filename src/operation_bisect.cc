#include "src/operation_bisect.h"

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
const Description& BisectLeftDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🪓👈");
  return output;
}
const Description& BisectRightDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🪓👉");
  return output;
}
const Description& BisectUpDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🪓👆");
  return output;
}
const Description& BisectDownDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🪓👇");
  return output;
}

namespace {

class Impl : public MoveOperationCommand {
  std::optional<Structure> structure_ = std::nullopt;
  std::vector<Direction> directions_;

 public:
  Impl(BisectOptions options)
      : structure_(options.structure),
        directions_(std::move(options.directions)) {}

  LineBuilder status() const override {
    NonEmptySingleLine backwards = structure_ == Structure::Line
                                       ? NON_EMPTY_SINGLE_LINE_CONSTANT(L"👆")
                                       : NON_EMPTY_SINGLE_LINE_CONSTANT(L"👈");
    NonEmptySingleLine forwards = structure_ == Structure::Line
                                      ? NON_EMPTY_SINGLE_LINE_CONSTANT(L"👇")
                                      : NON_EMPTY_SINGLE_LINE_CONSTANT(L"👉");
    LineBuilder output;
    SerializeCall(
        NON_EMPTY_SINGLE_LINE_CONSTANT(L"🪓"),
        std::vector<SingleLine>{
            StructureToString(structure_).read(),
            Concatenate(directions_ |
                        std::views::transform(
                            [&](const Direction& direction_) -> SingleLine {
                              switch (direction_) {
                                case Direction::Forwards:
                                  return forwards.read();
                                case Direction::Backwards:
                                  return backwards.read();
                              }
                              LOG(FATAL) << "Invalid direction.";
                              std::unreachable();
                            }))},
        output);
    return output;
  }

  transformation::Stack GetTransformation(
      const NonNull<std::shared_ptr<OperationScope>>&,
      transformation::Stack&) const override {
    transformation::Stack transformation;
    transformation.push_back(MakeNonNullUnique<transformation::Bisect>(
        structure_.value_or(Structure::Char), directions_));
    return transformation;
  }

  KeyCommandsMap key_commands_map() override {
    KeyCommandsMap cmap;
    cmap.Insert(
        ControlChar::Backspace,
        {.category = KeyCommandsMap::Category::StringControl,
         .description = Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Pop")},
         .active = !directions_.empty(),
         .handler = [this](ExtendedChar) { return directions_.pop_back(); }});

    if (structure_.value_or(Structure::Char) == Structure::Char) {
      cmap.Insert(L'h', {.category = KeyCommandsMap::Category::Direction,
                         .description = BisectLeftDescription(),
                         .handler =
                             [this](ExtendedChar) {
                               directions_.push_back(Direction::Backwards);
                             }})
          .Insert(L'l', {.category = KeyCommandsMap::Category::Direction,
                         .description = BisectRightDescription(),
                         .handler = [this](ExtendedChar) {
                           directions_.push_back(Direction::Forwards);
                         }});
    }
    if (structure_ == Structure::Line) {
      cmap.Insert(L'k', {.category = KeyCommandsMap::Category::Direction,
                         .description = BisectDownDescription(),
                         .handler =
                             [this](ExtendedChar) {
                               directions_.push_back(Direction::Backwards);
                             }})
          .Insert(L'j', {.category = KeyCommandsMap::Category::Direction,
                         .description = BisectUpDescription(),
                         .handler = [this](ExtendedChar) {
                           directions_.push_back(Direction::Forwards);
                         }});
    }
    return cmap;
  }
};
}  // namespace
NonNull<std::shared_ptr<MoveOperationCommand>> Bisect(BisectOptions options) {
  return MakeNonNullShared<Impl>(std::move(options));
}
}  // namespace afc::editor::operation::commands

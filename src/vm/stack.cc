#include "src/vm/stack.h"

#include "src/language/container.h"
#include "src/language/gc_expanders.h"
#include "src/language/gc_view.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::language::GetValueOrNullOpt;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;

namespace afc::vm {

StackFrameHeader::StackFrameHeader(
    std::vector<std::pair<Identifier, Type>> arguments)
    : arguments_(container::MaterializeUnorderedMap(
          std::views::iota(0uz, arguments.size()) |
          std::views::transform([&arguments](size_t index) {
            const std::pair<Identifier, Type>& data = arguments[index];
            return std::pair{data.first, std::pair(index, data.second)};
          }))) {}

std::optional<std::pair<size_t, Type>> StackFrameHeader::Find(
    const Identifier& identifier) {
  return GetValueOrNullOpt(arguments_, identifier);
}

/* static */ gc::Root<StackFrame> StackFrame::New(
    gc::Pool& pool, std::vector<gc::Ptr<Value>> arguments) {
  return pool.NewRoot(MakeNonNullUnique<StackFrame>(ConstructorAccessTag{},
                                                    std::move(arguments)));
}

StackFrame::StackFrame(ConstructorAccessTag,
                       std::vector<gc::Ptr<Value>> arguments)
    : arguments_(std::move(arguments)) {}

gc::Ptr<Value>& StackFrame::get(size_t index) { return arguments_[index]; }

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> StackFrame::Expand()
    const {
  // TODO(P0, 2026-04-27): Ugh, make this thread-safe.
  return gc::Expand(arguments_);
}

/* static */ gc::Root<Stack> Stack::New(gc::Pool& pool) {
  return pool.NewRoot(MakeNonNullUnique<Stack>(ConstructorAccessTag{}));
}

Stack::Stack(ConstructorAccessTag) {}

gc::Root<Value> Stack::get_from_current_frame(size_t index) const {
  return stack_.lock([&](const auto& stack) {
    CHECK(!stack.empty());
    return stack.back()->get(index).ToRoot();
  });
}

void Stack::set_in_current_frame(size_t index, gc::Ptr<Value> value) {
  stack_.lock([index, value = std::move(value)](auto& stack) {
    CHECK(!stack.empty());
    stack.back()->get(index) = std::move(value);
  });
}

void Stack::Push(gc::Ptr<StackFrame> frame) {
  stack_.lock([&](auto& stack) { stack.push_back(std::move(frame)); });
}

void Stack::Pop() {
  stack_.lock([](auto& stack) { stack.pop_back(); });
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Stack::Expand()
    const {
  return stack_.lock([](const auto& stack) { return gc::Expand(stack); });
}

}  // namespace afc::vm
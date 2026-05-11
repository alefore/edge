#ifndef __AFC_EDITOR_BUFFER_HOOKS_H__
#define __AFC_EDITOR_BUFFER_HOOKS_H__

#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc_util.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"

namespace afc::editor {
struct HookName
    : public language::GhostType<HookName,
                                 language::lazy_string::NonEmptySingleLine> {
  using GhostType::GhostType;
};

template <typename HookResult, typename... Args>
class HookRegistry {
 public:
  using HookCallback = language::gc::WithDependencies<
      std::function<futures::Value<HookResult>(Args...)>>;
  using HookCallbackPtr = language::gc::Ptr<HookCallback>;

 private:
  std::map<HookName, HookCallbackPtr> hooks_ = {};

 public:
  using ConstructorKey = afc::language::AccessKey<HookRegistry>;

  HookRegistry(ConstructorKey) {}

  static language::gc::Root<HookRegistry> New(language::gc::Pool& pool) {
    return pool.NewRoot(MakeNonNullUnique<HookRegistry>(ConstructorKey{}));
  }

  bool empty() const { return hooks_.empty(); }

  language::PossibleError Add(HookName name, HookCallbackPtr hook) {
    if (!hooks_.insert({std::move(name), std::move(hook)}).second)
      return language::Error(L"Hook already existed. Insertion failed.");
    return language::EmptyValue{};
  }

  // Runs all hooks and returns a future with all their results.
  futures::Value<std::vector<HookResult>> Dispatch(Args... args) const {
    return UnwrapVectorFuture(
        hooks_ | std::views::values |
        std::views::transform(
            [&](const HookCallbackPtr& callback) -> futures::Value<HookResult> {
              return callback->value()(args...);
            }) |
        std::ranges::to<std::vector>());
  }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const {
    return hooks_ | std::views::values | language::gc::view::ObjectMetadata |
           std::ranges::to<std::vector>();
  }
};

class BufferHooks {
 public:
  using SaveRegistry = HookRegistry<language::PossibleError>;

 private:
  language::gc::Ptr<SaveRegistry> save_;

 public:
  using ConstructorKey = afc::language::AccessKey<BufferHooks>;

  BufferHooks(ConstructorKey, language::gc::Ptr<SaveRegistry> save)
      : save_(std::move(save)) {}

  static language::gc::Root<BufferHooks> New(language::gc::Pool& pool) {
    return pool.NewRoot(MakeNonNullUnique<BufferHooks>(
        ConstructorKey{}, SaveRegistry::New(pool).ptr()));
  }

  SaveRegistry& save_hook() { return save_.value(); }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const {
    return {save_.object_metadata()};
  }
};
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_HOOKS_H__

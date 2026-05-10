#ifndef __AFC_EDITOR_BUFFER_HOOKS_H__
#define __AFC_EDITOR_BUFFER_HOOKS_H__

#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc_util.h"
#include "src/language/gc_view.h"
#include "src/language/safe_types.h"

namespace afc::editor {
template <typename HookResult, typename... Args>
class HookRegistry {
 public:
  using HookCallback =
      language::gc::WithDependencies<std::function<HookResult(Args...)>>;
  using HookCallbackPtr = language::gc::Ptr<HookCallback>;

 private:
  std::vector<HookCallbackPtr> hooks_ = {};

 public:
  using ConstructorKey = afc::language::AccessKey<HookRegistry>;

  HookRegistry(ConstructorKey) {}

  static language::gc::Root<HookRegistry> New(language::gc::Pool& pool) {
    return pool.NewRoot(MakeNonNullUnique<HookRegistry>(ConstructorKey{}));
  }

  void Add(HookCallbackPtr hook) { hooks_.push_back(std::move(hook)); }

  // Runs all hooks and returns a future with all their results.
  futures::Value<std::vector<HookResult>> Dispatch(Args... args) const {
    return UnwrapVectorFuture(
        language::MakeNonNullShared<std::vector<futures::Value<HookResult>>>(
            hooks_ | std::views::transform([&](HookCallback& callback) {
              return callback.value()(args...);
            })));
  }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const {
    return hooks_ | language::gc::view::ObjectMetadata |
           std::ranges::to<std::vector>();
  }
};

class BufferHooks {
 public:
  using SaveRegistry = HookRegistry<futures::PossibleError>;

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

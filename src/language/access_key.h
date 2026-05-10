// Example Usage:
// class Foo {
//  public:
//   using ConstructorKey = afc::language::AccessKey<Foo>;
//
//   Foo(ConstructorKey, int data) : m_data(data) {}
//
//   static NonNull<std::shared_ptr<Foo>> New(int data) {
//     return MakeNonNullShared<Foo>(ConstructorKey{}, data);
//   }
//
// private:
//   int m_data;
// };

#ifndef __AFC_EDITOR_SRC_LANGUAGE_ACCESS_KEY_H__
#define __AFC_EDITOR_SRC_LANGUAGE_ACCESS_KEY_H__

namespace afc::language {
// This implementation is more complex than it should be because C++23 doesn't
// support variadic friends.
//
// TODO(C++26, 2026-05-10): When C++26 is enabled, use variadic friends
// (typename Args...).
template <typename T>
class AccessKey {
  friend T;
  explicit AccessKey() = default;

 public:
  AccessKey(const AccessKey&) = delete;
  AccessKey(AccessKey&&) = default;
  AccessKey& operator=(const AccessKey&) = delete;
};
}  // namespace afc::language
#endif  // __AFC_EDITOR_SRC_LANGUAGE_ACCESS_KEY_H__

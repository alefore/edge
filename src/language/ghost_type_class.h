#ifndef __AFC_EDITOR_GHOST_TYPE_CLASS_H__
#define __AFC_EDITOR_GHOST_TYPE_CLASS_H__
namespace afc::language {
namespace ghost_type_internal {
template <typename Internal>
concept HasValueType = requires { typename Internal::value_type; };

template <typename Internal, bool = HasValueType<Internal>>
struct ValueType {};

// Specialization of GhostTypeValueType when Internal has value_type
template <typename Internal>
struct ValueType<Internal, true> {
  using value_type = typename Internal::value_type;
};

template <typename Internal>
concept HasSize = requires(Internal i) { i.size(); };

template <typename Internal, typename Key>
concept HasFind = requires(Internal i, Key key) {
  { i.find(key) } -> std::same_as<typename Internal::iterator>;
};

template <typename Internal>
concept HasBegin = requires(Internal i) {
  { i.begin() } -> std::same_as<typename Internal::iterator>;
};

template <typename Internal>
concept HasEnd = requires(Internal i) {
  { i.end() } -> std::same_as<typename Internal::iterator>;
};

template <typename Internal, typename Key>
concept HasSubscriptOperator = requires(Internal i, Key key) {
  { i[key] };
};
}  // namespace ghost_type_internal

template <typename External, typename Internal>
class GhostType : public ghost_type_internal::ValueType<Internal> {
  Internal value;

 public:
  GhostType() = default;
  GhostType(Internal initial_value) : value(std::move(initial_value)) {
    CHECK(!IsError(External::Validate(value)));
  }
  GhostType(const GhostType&) = default;

  static PossibleError Validate(const Internal&) { return Success(); }

  template <typename T = External>
  static ValueOrError<External> New(Internal internal) {
    return std::visit(
        overload{[&internal](EmptyValue) -> ValueOrError<External> {
                   return External(internal);
                 },
                 [](Error error) -> ValueOrError<External> { return error; }},
        T::Validate(internal));
  }

  auto size() const
    requires ghost_type_internal::HasSize<Internal>
  {
    return value.size();
  }

  template <typename Key>
  auto find(const Key& key) const
    requires ghost_type_internal::HasFind<Internal, Key>
  {
    return value.find(key);
  }

  auto begin() const
    requires ghost_type_internal::HasBegin<Internal>
  {
    return value.begin();
  }

  auto end() const
    requires ghost_type_internal::HasEnd<Internal>
  {
    return value.end();
  }

  template <typename Key>
  auto& operator[](const Key& key)
    requires ghost_type_internal::HasSubscriptOperator<Internal, Key>
  {
    return value[key];
  }

  template <typename T = External>
  bool operator<(const T& other) const {
    return value < other.value;
  }

  template <typename T>
  auto operator/(const T& other) const {
    return External::New(value / other);
  }

  inline External& operator*=(double double_value) {
    value *= double_value;
    return *static_cast<External*>(this);
  }

  template <typename OtherExternal, typename OtherInternal>
  inline External& operator*=(
      const GhostType<OtherExternal, OtherInternal>& other) {
    value *= other.value;
    return *static_cast<External*>(this);
  }

  template <typename OtherExternal, typename OtherInternal>
  inline bool operator==(
      const GhostType<OtherExternal, OtherInternal>& other) const {
    return value == other.value;
  }

  std::ostream& operator<<(std::ostream& os) { return os; }

 private:
  template <typename A, typename B>
  friend std::ostream& operator<<(std::ostream& os, const GhostType<A, B>& obj);

  template <typename A, typename B>
  friend class GhostTypeFactory;
};

template <typename External, typename Internal>
inline std::ostream& operator<<(std::ostream& os,
                                const GhostType<External, Internal>& obj) {
  using ::operator<<;
  os << "[" /*name*/ ":" << obj.value << "]";
  return os;
}
}  // namespace afc::language
#endif

// Macros for easily defining ghost types:
//
//   namespace foo::bar {
//     GHOST_TYPE(HistoryFile, std::wstring)
//   }
//   GHOST_TYPE_TOP_LEVEL(foo::bar::HistoryFile);
//
// For convenience, the following entry points are encouraged:
//
//   GHOST_TYPE_CONTAINER(Children, std::vector<Node>);
//   GHOST_TYPE_INT(Count);
//   GHOST_TYPE_DOUBLE(Probability);
//   GHOST_TYPE_SIZE_T(OperationId);
//
// This is based on the principle that code is more readable if the types it
// operates on convey more semantics than just what their underlying
// representation as basic types is (e.g., string, int, etc.). The base
// principle is that the basic types should only be used to define the
// application-specific types.
//
// For example, suppose you have a class that represents the following values as
// strings:
//
// - First name
// - Last name
// - Email
//
// Instead of writing expressions such as:
//
//   string my_first_name;
//   string my_last_name;
//   Person New(string first_name, string last_name, string email);
//   string GetEmail(const Person& person);
//
// We think it's better to use "alias" types (in this case FirstName, LastName
// and Email):
//
//   FirstName my_first_name;
//   LastName my_last_name;
//   Person New(FirstName first_name, LastName last_name, Email email);
//   Email GetEmail(const Person& person);
//
// Of course, in C++ one could simply do:
//
//   using FirstName = string;
//   using LastName = string;
//   using Email = string;
//
// Unfortunately, with this approach, C++ won't detect typing errors if the
// types are used incorrectly, such as in:
//
//   FirstName foo = GetEmail(my_person);
//
// This module proposes that you define "ghost types": a simple structure that
// contains a single field with the underlying data representation, such as:
//
//   class FirstName {
//    public:
//     FirstName(string value) : value(value) {}
//    private:
//     string value;
//   }
//   class LastName { ... }
//   class Email { ... }
//
// The macros provided here enable you to automatically declare various
// desirable operators for such types, so that they can, for example, be
// directly compared (based on the operators for the underlying
// representations).
//
// You may sometimes need to provide accessors exposing the underlying
// representation, like:
//
//   class FirstName {
//    public:
//     ...
//     string get() const { return value; }
//     void set(string update) { value = update; }
//     ...
//   };
//
// However, the methods provided here may be enough that you won't even need to
// expose the internal type in some cases.
#ifndef __AFC_EDITOR_GHOST_TYPE_H__
#define __AFC_EDITOR_GHOST_TYPE_H__

#include <functional>
#include <string>

#define GHOST_TYPE(ClassName, VariableType)                          \
  class ClassName {                                                  \
   public:                                                           \
    using ValueType = VariableType;                                  \
    GHOST_TYPE_CONSTRUCTOR(ClassName, VariableType, value);          \
    GHOST_TYPE_DEFAULT_CONSTRUCTORS(ClassName)                       \
    GHOST_TYPE_EQ(ClassName, value);                                 \
    GHOST_TYPE_ORDER(ClassName, value);                              \
                                                                     \
    const VariableType& read() const& { return value; }              \
    VariableType& read() & { return value; }                         \
    const VariableType&& read() const&& { return std::move(value); } \
    VariableType&& read() && { return std::move(value); }            \
                                                                     \
   private:                                                          \
    GHOST_TYPE_OUTPUT_FRIEND(ClassName, value);                      \
    GHOST_TYPE_HASH_FRIEND(ClassName, value);                        \
    GHOST_TYPE_NUMERIC_LIMTS_FRIEND(ClassName);                      \
    VariableType value;                                              \
  };                                                                 \
                                                                     \
  GHOST_TYPE_OUTPUT(ClassName, value);

#define GHOST_TYPE_TOP_LEVEL(ClassName) \
  GHOST_TYPE_HASH(ClassName)            \
  GHOST_TYPE_NUMERIC_LIMITS(ClassName)

#define INTERNAL_GHOST_TYPE_NUMBER(ClassName, VariableType)              \
  class ClassName {                                                      \
   public:                                                               \
    using ValueType = VariableType;                                      \
    GHOST_TYPE_CONSTRUCTOR(ClassName, VariableType, value);              \
    GHOST_TYPE_DEFAULT_CONSTRUCTORS(ClassName)                           \
    GHOST_TYPE_EQ(ClassName, value);                                     \
    GHOST_TYPE_ORDER(ClassName, value);                                  \
                                                                         \
    const auto& read() const { return value; }                           \
                                                                         \
    bool IsZero() const { return *this == ClassName(); }                 \
                                                                         \
   private:                                                              \
    static VariableType PrepareForOperator(VariableType i) { return i; } \
    GHOST_TYPE_OPERATOR_FRIEND(ClassName, VariableType);                 \
    GHOST_TYPE_OUTPUT_FRIEND(ClassName, value);                          \
    GHOST_TYPE_HASH_FRIEND(ClassName, value);                            \
    GHOST_TYPE_NUMERIC_LIMTS_FRIEND(ClassName);                          \
    VariableType value = VariableType();                                 \
  };                                                                     \
                                                                         \
  GHOST_TYPE_OUTPUT(ClassName, value);

// Defines two types: ClassName and DeltaName. ClassName represents a value and
// DeltaName represents a delta in ClassName.
#define GHOST_TYPE_NUMBER_WITH_DELTA(ClassName, VariableType, DeltaName, \
                                     DeltaNestedType)                    \
  INTERNAL_GHOST_TYPE_NUMBER(DeltaName, DeltaNestedType)                 \
  GHOST_TYPE_NUMBER_OPERATORS_BASE(DeltaName, DeltaNestedType)           \
  GHOST_TYPE_NUMBER_OPERATORS_SELF(DeltaName)                            \
  class ClassName {                                                      \
   public:                                                               \
    using ValueType = VariableType;                                      \
    GHOST_TYPE_CONSTRUCTOR(ClassName, VariableType, value);              \
    GHOST_TYPE_DEFAULT_CONSTRUCTORS(ClassName)                           \
    GHOST_TYPE_EQ(ClassName, value);                                     \
    GHOST_TYPE_ORDER(ClassName, value)                                   \
                                                                         \
    DeltaName ToDelta() const { return DeltaName(read()); }              \
    const VariableType& read() const { return value; }                   \
                                                                         \
    bool IsZero() const { return *this == ClassName(); }                 \
                                                                         \
    template <typename V = ClassName>                                    \
    ClassName MinusHandlingOverflow(DeltaName delta) const {             \
      const size_t& (V::*read_callback)() const = &V::read;              \
      (void)read_callback;                                               \
      return *this - std::min(delta, ToDelta());                         \
    }                                                                    \
                                                                         \
    template <typename V = ClassName>                                    \
    ClassName PlusHandlingOverflow(const DeltaName& delta) const {       \
      const size_t& (V::*read_callback)() const = &V::read;              \
      (void)read_callback;                                               \
      return ToDelta() > -delta ? *this + delta : ClassName(0);          \
    }                                                                    \
                                                                         \
    template <typename V = ClassName>                                    \
    ClassName previous() const {                                         \
      const size_t& (V::*read_callback)() const = &V::read;              \
      (void)read_callback;                                               \
      return *this - DeltaName(1);                                       \
    }                                                                    \
                                                                         \
    template <typename V = ClassName>                                    \
    ClassName next() const {                                             \
      const size_t& (V::*read_callback)() const = &V::read;              \
      (void)read_callback;                                               \
      return *this + DeltaName(1);                                       \
    }                                                                    \
                                                                         \
   private:                                                              \
    static DeltaNestedType PrepareForOperator(DeltaName i) {             \
      return i.read();                                                   \
    }                                                                    \
                                                                         \
    GHOST_TYPE_OPERATOR_FRIEND(ClassName, DeltaName);                    \
    friend ClassName& operator+=(ClassName& a, const DeltaName& value);  \
    friend ClassName& operator-=(ClassName& a, const DeltaName& value);  \
    friend int operator/(const ClassName& a, const DeltaName& b);        \
    friend int operator%(const ClassName& a, const DeltaName& b);        \
    GHOST_TYPE_OUTPUT_FRIEND(ClassName, value);                          \
    GHOST_TYPE_HASH_FRIEND(ClassName, value);                            \
    GHOST_TYPE_NUMERIC_LIMTS_FRIEND(ClassName);                          \
    VariableType value = VariableType();                                 \
  };                                                                     \
  GHOST_TYPE_OUTPUT(ClassName, value)                                    \
  GHOST_TYPE_NUMBER_OPERATORS_BASE(ClassName, DeltaName)                 \
  GHOST_TYPE_NUMBER_OPERATORS_DELTA(ClassName, DeltaName)

#define GHOST_TYPE_DOUBLE(ClassName)                  \
  INTERNAL_GHOST_TYPE_NUMBER(ClassName, double)       \
  GHOST_TYPE_NUMBER_OPERATORS_BASE(ClassName, double) \
  GHOST_TYPE_NUMBER_OPERATORS_SELF(ClassName)

#define GHOST_TYPE_INT(ClassName)                  \
  INTERNAL_GHOST_TYPE_NUMBER(ClassName, int)       \
  GHOST_TYPE_NUMBER_OPERATORS_BASE(ClassName, int) \
  GHOST_TYPE_NUMBER_OPERATORS_SELF(ClassName)

#define GHOST_TYPE_SIZE_T(ClassName)                  \
  INTERNAL_GHOST_TYPE_NUMBER(ClassName, size_t)       \
  GHOST_TYPE_NUMBER_OPERATORS_BASE(ClassName, size_t) \
  GHOST_TYPE_NUMBER_OPERATORS_SELF(ClassName)

#define GHOST_TYPE_CONTAINER(ClassName, VariableType)       \
  class ClassName {                                         \
    using ValueType = VariableType;                         \
                                                            \
   public:                                                  \
    using value_type = ValueType::value_type;               \
    GHOST_TYPE_CONSTRUCTOR(ClassName, VariableType, value); \
    GHOST_TYPE_DEFAULT_CONSTRUCTORS(ClassName)              \
    GHOST_TYPE_EMPTY                                        \
    GHOST_TYPE_CLEAR                                        \
    GHOST_TYPE_SIZE                                         \
    GHOST_TYPE_EQ(ClassName, value);                        \
    GHOST_TYPE_BEGIN_END                                    \
    GHOST_TYPE_INDEX                                        \
    GHOST_TYPE_COUNT                                        \
    GHOST_TYPE_FIND                                         \
    GHOST_TYPE_BACK                                         \
    GHOST_TYPE_PUSH_BACK                                    \
    GHOST_TYPE_POP_BACK                                     \
    GHOST_TYPE_INSERT                                       \
    GHOST_TYPE_ERASE                                        \
    GHOST_TYPE_LOWER_BOUND                                  \
    const VariableType& read() const { return value; }      \
                                                            \
   private:                                                 \
    VariableType value = VariableType();                    \
  };

// Implementation:

#define GHOST_TYPE_CONSTRUCTOR(ClassName, VariableType, variable) \
  template <typename T = VariableType>                            \
  explicit ClassName(VariableType input_variable = T())           \
      : variable(std::move(input_variable)) {}

#define GHOST_TYPE_DEFAULT_CONSTRUCTORS(ClassName) \
  ClassName(ClassName&&) = default;                \
  ClassName(const ClassName&) = default;           \
  ClassName& operator=(const ClassName&) = default;

#define GHOST_TYPE_CONSTRUCTOR_EMPTY(ClassName) ClassName() = default;

#define GHOST_TYPE_OPERATOR_FRIEND(ClassName, VariableType)           \
  friend ClassName operator+(const ClassName& a, VariableType other); \
  friend ClassName operator+(VariableType other, const ClassName& a); \
  friend ClassName operator-(const ClassName& a, VariableType other); \
  friend ClassName operator*(const ClassName& a, VariableType other); \
  friend ClassName operator*(VariableType other, const ClassName& a); \
  friend ClassName operator/(const ClassName& a, VariableType other); \
  friend ClassName& operator+=(ClassName& a, const ClassName& value); \
  friend ClassName& operator-=(ClassName& a, const ClassName& value); \
  friend ClassName& operator*=(ClassName& a, const ClassName& value); \
  friend ClassName& operator++(ClassName& a);                         \
  friend ClassName operator++(ClassName& a, int);                     \
  friend ClassName& operator--(ClassName& a);                         \
  friend ClassName operator--(ClassName& a, int);                     \
  friend ClassName& operator*=(ClassName& a, double value);

#define GHOST_TYPE_NUMBER_OPERATORS_BASE(ClassName, VariableType)            \
  inline ClassName operator+(const ClassName& a, VariableType other) {       \
    return ClassName(a.read() + ClassName::PrepareForOperator(other));       \
  }                                                                          \
                                                                             \
  inline ClassName operator+(VariableType other, const ClassName& a) {       \
    return ClassName(a.read() + ClassName::PrepareForOperator(other));       \
  }                                                                          \
                                                                             \
  inline ClassName operator-(ClassName a) { return ClassName(-a.read()); }   \
                                                                             \
  inline ClassName operator-(const ClassName& a, VariableType other) {       \
    const auto value_other = ClassName::PrepareForOperator(other);           \
    if constexpr (std::is_same<decltype(ClassName::value), size_t>::value) { \
      if (other > VariableType(0)) {                                         \
        CHECK_GE(a.read(),                                                   \
                 static_cast<decltype(ClassName::value)>(value_other));      \
      }                                                                      \
    }                                                                        \
    return ClassName(a.read() - value_other);                                \
  }                                                                          \
                                                                             \
  inline ClassName operator*(const ClassName& a, VariableType other) {       \
    return ClassName(a.read() * ClassName::PrepareForOperator(other));       \
  }                                                                          \
                                                                             \
  inline ClassName operator*(VariableType other, const ClassName& a) {       \
    return ClassName(a.read() * ClassName::PrepareForOperator(other));       \
  }                                                                          \
                                                                             \
  inline ClassName operator/(const ClassName& a, VariableType other) {       \
    return ClassName(a.read() / ClassName::PrepareForOperator(other));       \
  }                                                                          \
                                                                             \
  inline ClassName& operator++(ClassName& a) {                               \
    a.value++;                                                               \
    return a;                                                                \
  }                                                                          \
  inline ClassName operator++(ClassName& a, int) {                           \
    ClassName copy = a;                                                      \
    a.value++;                                                               \
    return copy;                                                             \
  }                                                                          \
  inline ClassName& operator--(ClassName& a) {                               \
    a.value--;                                                               \
    return a;                                                                \
  }                                                                          \
  inline ClassName operator--(ClassName& a, int) {                           \
    ClassName copy = a;                                                      \
    a.value--;                                                               \
    return copy;                                                             \
  }                                                                          \
  inline ClassName& operator*=(ClassName& a, double value) {                 \
    a.value *= value;                                                        \
    return a;                                                                \
  }

#define GHOST_TYPE_NUMBER_OPERATORS_SELF(ClassName)                    \
  inline ClassName operator+(const ClassName& a, const ClassName& b) { \
    return ClassName(a.read() + b.read());                             \
  }                                                                    \
                                                                       \
  inline ClassName operator-(const ClassName& a, const ClassName& b) { \
    return ClassName(a.read() - b.read());                             \
  }                                                                    \
                                                                       \
  inline ClassName operator*(const ClassName& a, const ClassName& b) { \
    return ClassName(a.read() * b.read());                             \
  }                                                                    \
                                                                       \
  inline ClassName::ValueType operator/(const ClassName& a,            \
                                        const ClassName& b) {          \
    return a.read() / b.read();                                        \
  }                                                                    \
                                                                       \
  inline ClassName& operator+=(ClassName& a, const ClassName& value) { \
    a.value += value.read();                                           \
    return a;                                                          \
  }                                                                    \
  inline ClassName& operator-=(ClassName& a, const ClassName& value) { \
    a.value -= value.read();                                           \
    return a;                                                          \
  }                                                                    \
  inline ClassName& operator*=(ClassName& a, const ClassName& value) { \
    a.value *= value.read();                                           \
    return a;                                                          \
  }                                                                    \
  inline int operator%(const ClassName& a, const ClassName& b) {       \
    return static_cast<int>(a.read()) % static_cast<int>(b.read());    \
  }

#define GHOST_TYPE_NUMBER_OPERATORS_DELTA(ClassName, DeltaName)        \
  inline DeltaName operator-(const ClassName& a, const ClassName& b) { \
    return DeltaName(a.read() - b.read());                             \
  }                                                                    \
                                                                       \
  inline int operator/(const ClassName& a, const DeltaName& b) {       \
    return a.read() / ClassName::PrepareForOperator(b);                \
  }                                                                    \
                                                                       \
  inline int operator%(const ClassName& a, const DeltaName& b) {       \
    return a.read() % ClassName::PrepareForOperator(b);                \
  }                                                                    \
                                                                       \
  inline ClassName& operator+=(ClassName& a, const DeltaName& value) { \
    a.value += ClassName::PrepareForOperator(value);                   \
    return a;                                                          \
  }                                                                    \
                                                                       \
  inline ClassName& operator-=(ClassName& a, const DeltaName& value) { \
    a.value -= ClassName::PrepareForOperator(value);                   \
    return a;                                                          \
  }

#define GHOST_TYPE_EMPTY \
  bool empty() const { return value.empty(); }

#define GHOST_TYPE_CLEAR \
  void clear() { value.clear(); }

#define GHOST_TYPE_SIZE \
  size_t size() const { return value.size(); }

#define GHOST_TYPE_EQ(ClassName, variable)        \
  bool operator==(const ClassName& other) const { \
    return variable == other.variable;            \
  }                                               \
  bool operator!=(const ClassName& other) const { return !(*this == other); }

// We use the template type T to use SFINAE to disable operators on ghost types
// of variables that don'the define corresponding orders.
#define GHOST_TYPE_ORDER(ClassName, variable)  \
  template <typename T = ClassName::ValueType> \
  bool operator<(const T& other) const {       \
    return variable < other.variable;          \
  }                                            \
  template <typename T = ClassName::ValueType> \
  bool operator<=(const T& other) const {      \
    return variable <= other.variable;         \
  }                                            \
  template <typename T = ClassName::ValueType> \
  bool operator>(const T& other) const {       \
    return variable > other.variable;          \
  }                                            \
  template <typename T = ClassName::ValueType> \
  bool operator>=(const T& other) const {      \
    return variable >= other.variable;         \
  }

// GHOST_TYPE_BEGIN_END declares `begin` and `end` methods for the ghost type.
// This is useful when the underlying representation has corresponding methods:
//
//   class FeaturesSet {
//    public:
//     ...
//     GHOST_TYPE_BEGIN_END
//
//    private:
//     std::unordered_set<Feature> value;
//   };
//
// This is enough to make this work:
//
//   FeaturesSet features_set;
//   for (const Feature& feature : features_set) ...
//
// This macro assumes that the ghost variable is called `value`.
#define GHOST_TYPE_BEGIN_END                                                 \
  auto begin() const { return value.begin(); }                               \
  auto end() const { return value.end(); }                                   \
                                                                             \
  template <typename V = ValueType,                                          \
            typename V::const_reverse_iterator (V::*F)() const = &V::rbegin> \
  auto rbegin() const {                                                      \
    return (value.*F)();                                                     \
  }                                                                          \
                                                                             \
  template <typename V = ValueType,                                          \
            typename V::reverse_iterator (V::*F)() = &V::rbegin>             \
  auto rbegin() {                                                            \
    return (value.*F)();                                                     \
  }                                                                          \
                                                                             \
  template <typename V = ValueType,                                          \
            typename V::const_reverse_iterator (V::*F)() const = &V::rend>   \
  auto rend() const {                                                        \
    return (value.*F)();                                                     \
  }                                                                          \
                                                                             \
  template <typename V = ValueType,                                          \
            typename V::reverse_iterator (V::*F)() = &V::rend>               \
  auto rend() {                                                              \
    return (value.*F)();                                                     \
  }

#define GHOST_TYPE_HASH_FRIEND(ClassName, variable) \
  friend class std::hash<ClassName>;

// Can't be inside of a namespace; must be at the top-level.
#define GHOST_TYPE_HASH(ClassName)                                      \
  template <>                                                           \
  struct std::hash<ClassName> {                                         \
    std::size_t operator()(const ClassName& self) const noexcept {      \
      return std::hash<std::remove_const<                               \
          std::remove_reference<decltype(self.read())>::type>::type>()( \
          self.read());                                                 \
    }                                                                   \
  };

#define GHOST_TYPE_NUMERIC_LIMTS_FRIEND(ClassName) \
  friend class std::numeric_limits<ClassName>;

#define GHOST_TYPE_NUMERIC_LIMITS(ClassName)           \
  template <>                                          \
  class std::numeric_limits<ClassName> {               \
   public:                                             \
    template <typename T = decltype(ClassName::value)> \
    static ClassName max() {                           \
      return ClassName(std::numeric_limits<T>::max()); \
    };                                                 \
    template <typename T = decltype(ClassName::value)> \
    static ClassName min() {                           \
      return ClassName(std::numeric_limits<T>::min()); \
    };                                                 \
  };

#define GHOST_TYPE_INDEX                                  \
  template <typename KeyType>                             \
  auto& operator[](const KeyType& ghost_type_key) {       \
    return value[ghost_type_key];                         \
  }                                                       \
  template <typename KeyType>                             \
  auto& operator[](const KeyType& ghost_type_key) const { \
    return value[ghost_type_key];                         \
  }

#define GHOST_TYPE_COUNT                                             \
  template <typename V = ValueType, typename KeyType>                \
  auto count(const KeyType& ghost_type_key) {                        \
    typename V::size_type (V::*f)(const KeyType&) const = &V::count; \
    return (value.*f)(ghost_type_key);                               \
  }

#define GHOST_TYPE_FIND                                                  \
  template <typename V = ValueType, typename KeyType>                    \
  auto find(const KeyType& ghost_type_key) const {                       \
    typename V::const_iterator (V::*f)(const KeyType&) const = &V::find; \
    return (value.*f)(ghost_type_key);                                   \
  }                                                                      \
                                                                         \
  template <typename V = ValueType, typename KeyType>                    \
  auto find(const KeyType& ghost_type_key) {                             \
    typename V::iterator (V::*f)(const KeyType&) = &V::find;             \
    return (value.*f)(ghost_type_key);                                   \
  }

// We use the template type V to use SFINAE to disable this expression on ghost
// types of containers that don't include a push_back method (such as
// std::unordered_set).
#define GHOST_TYPE_BACK             \
  template <typename V = ValueType> \
  auto& back() {                    \
    auto& (V::*f)() = &V::back;     \
    return (value.*f)();            \
  }

// We use the template type V to use SFINAE to disable this expression on ghost
// types of containers that don't include a push_back method (such as
// std::unordered_set).
#define GHOST_TYPE_PUSH_BACK                                    \
  template <typename V = ValueType>                             \
  void push_back(const ValueType::value_type& v) {              \
    void (V::*f)(const ValueType::value_type&) = &V::push_back; \
    (value.*f)(v);                                              \
  }

// We use the template type V to use SFINAE to disable this expression on ghost
// types of containers that don't include a pop_back method (such as
// std::unordered_set).
#define GHOST_TYPE_POP_BACK         \
  template <typename V = ValueType> \
  void pop_back() {                 \
    void (V::*f)() = &V::pop_back;  \
    (value.*f)();                   \
  }

// We use the template type V to use SFINAE to disable this expression on ghost
// types of containers that don't include an insert method.
#define GHOST_TYPE_INSERT                                                      \
  template <typename V = ValueType,                                            \
            std::pair<ValueType::iterator, bool> (V::*F)(                      \
                ValueType::value_type&&) = &V::insert>                         \
  auto insert(ValueType::value_type&& v) {                                     \
    return (value.*F)(std::forward<ValueType::value_type>(v));                 \
  }                                                                            \
                                                                               \
  template <typename V = ValueType,                                            \
            ValueType::iterator (V::*F)(ValueType::value_type&&) = &V::insert> \
  auto insert(ValueType::value_type&& v) {                                     \
    return (value.*F)(std::forward<ValueType::value_type>(v));                 \
  }                                                                            \
                                                                               \
  template <typename It, typename V = ValueType,                               \
            ValueType::iterator (V::*F)(It, ValueType::value_type&&) =         \
                &V::insert>                                                    \
  auto insert(It it, ValueType::value_type v) {                                \
    return (value.*F)(std::move(it), std::forward<ValueType::value_type>(v));  \
  }

// We use the template type V to use SFINAE to disable this expression on ghost
// types of containers that don't include an insert method.
#define GHOST_TYPE_ERASE                                                     \
  template <typename It, typename V = ValueType, It (V::*F)(It) = &V::erase> \
  auto erase(It pos) {                                                       \
    return (value.*F)(std::move(pos));                                       \
  }                                                                          \
                                                                             \
  template <typename V = ValueType,                                          \
            ValueType::iterator (V::*F)(ValueType::const_iterator,           \
                                        ValueType::const_iterator) =         \
                &V::erase>                                                   \
  auto erase(ValueType::const_iterator first,                                \
             ValueType::const_iterator last) {                               \
    return (value.*F)(std::move(first), std::move(last));                    \
  }

#define GHOST_TYPE_LOWER_BOUND                                               \
  template <typename Key, typename V = ValueType>                            \
  auto lower_bound(const Key& k) {                                           \
    ValueType::iterator (V::*f)(const Key& k) = &V::lower_bound;             \
    return (value.*f)(k);                                                    \
  }                                                                          \
                                                                             \
  template <typename Key, typename V = ValueType>                            \
  auto lower_bound(const Key& k) const {                                     \
    ValueType::const_iterator (V::*f)(const Key& k) const = &V::lower_bound; \
    return (value.*f)(k);                                                    \
  }

#define GHOST_TYPE_OUTPUT_FRIEND(ClassName, variable)                      \
  friend std::ostream& operator<<(std::ostream& os, const ClassName& obj); \
  friend std::wstring to_wstring(const ClassName& obj);

// We use the template type T to use SFINAE to disable operators on ghost types
// of variables that don'the define corresponding operators.
#define GHOST_TYPE_OUTPUT(ClassName, variable)                              \
  inline std::ostream& operator<<(std::ostream& os, const ClassName& obj) { \
    using ::operator<<;                                                     \
    os << "[" #ClassName ":" << obj.variable << "]";                        \
    return os;                                                              \
  }                                                                         \
  inline std::wstring to_wstring(const ClassName& obj) {                    \
    using afc::language::to_wstring;                                        \
    using std::to_wstring;                                                  \
    return to_wstring(obj.variable);                                        \
  }

namespace afc::language {
std::wstring to_wstring(std::wstring s);
}  // namespace afc::language

#endif  //__AFC_EDITOR_GHOST_TYPE_H__

#include "string.h"

#include <glog/logging.h>

#include <set>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/vm/container.h"
#include "src/vm/environment.h"
#include "src/vm/escape.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

using afc::concurrent::Protected;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::LowerCase;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::UpperCase;
using afc::math::numbers::Number;

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
using language::MakeNonNullUnique;
using language::NonNull;

namespace gc = language::gc;

template <>
const types::ObjectName VMTypeMapper<NonNull<
    std::shared_ptr<Protected<std::vector<LazyString>>>>>::object_type_name =
    types::ObjectName{
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"VectorString")}};

template <>
const types::ObjectName VMTypeMapper<NonNull<
    std::shared_ptr<Protected<std::set<LazyString>>>>>::object_type_name =
    types::ObjectName{Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"SetString")}};

template <typename Callable>
void AddMethod(const Identifier& name, language::gc::Pool& pool,
               Callable callback, gc::Root<ObjectType>& string_type) {
  string_type.ptr()->AddField(name,
                              NewCallback(pool, PurityType{}, callback).ptr());
}

void RegisterStringType(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> string_type = ObjectType::New(pool, types::String{});
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"size"}}}}, pool,
      [](const LazyString& str) { return str.size().read(); }, string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"toint"}}}}, pool,
      [](const LazyString& str) -> futures::ValueOrError<int> {
        try {
          return futures::Past(Success(std::stoi(str.ToString())));
        } catch (const std::out_of_range& ia) {
          return futures::Past(Error{LazyString{L"toint: stoi failure: "} +
                                     LazyString{FromByteString(ia.what())}});
        } catch (const std::invalid_argument& ia) {
          return futures::Past(Error{LazyString{L"toint: stoi failure: "} +
                                     LazyString{FromByteString(ia.what())}});
        }
      },
      string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"empty"}}}}, pool,
      [](const LazyString& str) { return str.empty(); }, string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"tolower"}}}}, pool,
      [](LazyString str) { return LowerCase(str); }, string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"toupper"}}}}, pool,
      [](LazyString str) { return UpperCase(str); }, string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"shell_escape"}}}},
      pool,
      [](LazyString str) {
        return vm::EscapedString(str).ShellEscapedRepresentation();
      },
      string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"substr"}}}}, pool,
      [](const std::wstring& str, size_t pos,
         size_t len) -> futures::ValueOrError<std::wstring> {
        if (static_cast<size_t>(pos + len) > str.size()) {
          return futures::Past(Error{
              LazyString{L"substr: Invalid index (past end of string)."}});
        }
        return futures::Past(Success(str.substr(pos, len)));
      },
      string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"starts_with"}}}},
      pool,
      [](const LazyString& str, const LazyString& prefix) {
        return StartsWith(str, prefix);
      },
      string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"find"}}}}, pool,
      [](const std::wstring& str, const std::wstring& pattern,
         size_t start_pos) {
        size_t pos = str.find(pattern, start_pos);
        return pos == std::wstring::npos ? Number::FromInt64(-1)
                                         : Number::FromSizeT(pos);
      },
      string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"find_last_of"}}}},
      pool,
      [](const std::wstring& str, const std::wstring& pattern,
         size_t start_pos) {
        size_t pos = str.find_last_of(pattern, start_pos);
        return pos == std::wstring::npos ? Number::FromInt64(-1)
                                         : Number::FromSizeT(pos);
      },
      string_type);
  AddMethod(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"find_last_not_of"}}}},
      pool,
      [](const std::wstring& str, const std::wstring& pattern,
         size_t start_pos) {
        size_t pos = str.find_last_not_of(pattern, start_pos);
        return pos == std::wstring::npos ? Number::FromInt64(-1)
                                         : Number::FromSizeT(pos);
      },
      string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"find_first_of"}}}},
      pool,
      [](const std::wstring& str, const std::wstring& pattern,
         size_t start_pos) {
        size_t pos = str.find_first_of(pattern, start_pos);
        return pos == std::wstring::npos ? Number::FromInt64(-1)
                                         : Number::FromSizeT(pos);
      },
      string_type);
  AddMethod(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"find_first_not_of"}}}},
      pool,
      [](const std::wstring& str, const std::wstring& pattern,
         size_t start_pos) {
        size_t pos = str.find_first_not_of(pattern, start_pos);
        return pos == std::wstring::npos ? Number::FromInt64(-1)
                                         : Number::FromSizeT(pos);
      },
      string_type);
  environment.DefineType(string_type.ptr());

  environment.Define(
      Identifier(NON_EMPTY_SINGLE_LINE_CONSTANT(L"string")),
      NewCallback(pool, PurityType{}, [] { return LazyString{}; }));

  container::Export<std::vector<LazyString>>(pool, environment);
  container::Export<std::set<LazyString>>(pool, environment);
}
}  // namespace afc::vm

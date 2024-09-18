#include "string.h"

#include <glog/logging.h>

#include <set>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/vm/container.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

using afc::concurrent::Protected;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
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
    std::shared_ptr<Protected<std::vector<std::wstring>>>>>::object_type_name =
    types::ObjectName{LazyString{L"VectorString"}};

template <>
const types::ObjectName VMTypeMapper<NonNull<
    std::shared_ptr<Protected<std::set<std::wstring>>>>>::object_type_name =
    types::ObjectName{LazyString{L"SetString"}};

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
      [](const std::wstring& str) { return str.size(); }, string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"toint"}}}}, pool,
      [](const std::wstring& str) -> futures::ValueOrError<int> {
        try {
          return futures::Past(Success(std::stoi(str)));
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
      [](const std::wstring& str) { return str.empty(); }, string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"tolower"}}}}, pool,
      [](std::wstring str) {
        for (auto& i : str) i = std::tolower(i, std::locale(""));
        return str;
      },
      string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"toupper"}}}}, pool,
      [](std::wstring str) {
        for (auto& i : str) i = std::toupper(i, std::locale(""));
        return str;
      },
      string_type);
  AddMethod(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"shell_escape"}}}},
      pool, language::ShellEscape, string_type);
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
      [](const std::wstring& str, const std::wstring& prefix) {
        return prefix.size() <= str.size() &&
               (std::mismatch(prefix.begin(), prefix.end(), str.begin())
                    .first == prefix.end());
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

  container::Export<std::vector<std::wstring>>(pool, environment);
  container::Export<std::set<std::wstring>>(pool, environment);
}
}  // namespace afc::vm

#include "string.h"

#include <glog/logging.h>

#include <set>
#include <vector>

#include "../public/callbacks.h"
#include "../public/container.h"
#include "../public/environment.h"
#include "../public/types.h"
#include "../public/value.h"
#include "../public/vm.h"

using afc::language::Error;
using afc::language::FromByteString;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::math::numbers::FromInt;
using afc::math::numbers::FromSizeT;
using afc::math::numbers::ToInt;

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
using language::MakeNonNullUnique;
using language::NonNull;

namespace gc = language::gc;

template <>
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<std::vector<std::wstring>>>>::object_type_name =
    types::ObjectName(L"VectorString");

template <>
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<std::set<std::wstring>>>>::object_type_name =
    types::ObjectName(L"SetString");

template <typename Callable>
void AddMethod(const wstring& name, language::gc::Pool& pool, Callable callback,
               gc::Root<ObjectType>& string_type) {
  string_type.ptr()->AddField(
      name, NewCallback(pool, PurityType::kPure, callback).ptr());
}

void RegisterStringType(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> string_type = ObjectType::New(pool, types::String{});
  AddMethod(
      L"size", pool, [](const wstring& str) { return str.size(); },
      string_type);
  AddMethod(
      L"toint", pool,
      [](const wstring& str) -> futures::ValueOrError<int> {
        try {
          return futures::Past(Success(std::stoi(str)));
        } catch (const std::out_of_range& ia) {
          return futures::Past(
              Error(L"toint: stoi failure: " + FromByteString(ia.what())));
        } catch (const std::invalid_argument& ia) {
          return futures::Past(
              Error(L"toint: stoi failure: " + FromByteString(ia.what())));
        }
      },
      string_type);
  AddMethod(
      L"empty", pool, [](const wstring& str) { return str.empty(); },
      string_type);
  AddMethod(
      L"tolower", pool,
      [](wstring str) {
        for (auto& i : str) {
          i = std::tolower(i, std::locale(""));
        }
        return str;
      },
      string_type);
  AddMethod(
      L"toupper", pool,
      [](wstring str) {
        for (auto& i : str) {
          i = std::toupper(i, std::locale(""));
        }
        return str;
      },
      string_type);
  AddMethod(L"shell_escape", pool, language::ShellEscape, string_type);
  AddMethod(
      L"substr", pool,
      [](const wstring& str, size_t pos,
         size_t len) -> futures::ValueOrError<std::wstring> {
        if (static_cast<size_t>(pos + len) > str.size()) {
          return futures::Past(
              Error(L"substr: Invalid index (past end of string)."));
        }
        return futures::Past(Success(str.substr(pos, len)));
      },
      string_type);
  AddMethod(
      L"starts_with", pool,
      [](const wstring& str, const wstring& prefix) {
        return prefix.size() <= str.size() &&
               (std::mismatch(prefix.begin(), prefix.end(), str.begin())
                    .first == prefix.end());
      },
      string_type);
  AddMethod(
      L"find", pool,
      [](const wstring& str, const wstring& pattern, size_t start_pos) {
        size_t pos = str.find(pattern, start_pos);
        return pos == wstring::npos ? FromInt(-1) : FromSizeT(pos);
      },
      string_type);
  AddMethod(
      L"find_last_of", pool,
      [](const wstring& str, const wstring& pattern, size_t start_pos) {
        size_t pos = str.find_last_of(pattern, start_pos);
        return pos == wstring::npos ? FromInt(-1) : FromSizeT(pos);
      },
      string_type);
  AddMethod(
      L"find_last_not_of", pool,
      [](const wstring& str, const wstring& pattern, size_t start_pos) {
        size_t pos = str.find_last_not_of(pattern, start_pos);
        return pos == wstring::npos ? FromInt(-1) : FromSizeT(pos);
      },
      string_type);
  AddMethod(
      L"find_first_of", pool,
      [](const wstring& str, const wstring& pattern, size_t start_pos) {
        size_t pos = str.find_first_of(pattern, start_pos);
        return pos == wstring::npos ? -1 : pos;
      },
      string_type);
  AddMethod(
      L"find_first_not_of", pool,
      [](const wstring& str, const wstring& pattern, size_t start_pos) {
        size_t pos = str.find_first_not_of(pattern, start_pos);
        return pos == wstring::npos ? -1 : pos;
      },
      string_type);
  environment.DefineType(string_type.ptr());

  container::Export<std::vector<std::wstring>>(pool, environment);
  container::Export<std::set<std::wstring>>(pool, environment);
}
}  // namespace afc::vm

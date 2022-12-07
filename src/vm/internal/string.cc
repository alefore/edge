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

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
using language::MakeNonNullUnique;
using language::NonNull;

namespace gc = language::gc;

template <>
const VMType
    VMTypeMapper<NonNull<std::shared_ptr<std::vector<std::wstring>>>>::vmtype =
        VMType::ObjectType(VMTypeObjectTypeName(L"VectorString"));

template <>
const VMType
    VMTypeMapper<NonNull<std::shared_ptr<std::set<std::wstring>>>>::vmtype =
        VMType::ObjectType(VMTypeObjectTypeName(L"SetString"));

template <typename ReturnType, typename... Args>
void AddMethod(const wstring& name, language::gc::Pool& pool,
               std::function<ReturnType(const wstring&, Args...)> callback,
               ObjectType& string_type) {
  string_type.AddField(name, NewCallback(pool, PurityType::kPure, callback));
}

void RegisterStringType(gc::Pool& pool, Environment& environment) {
  auto string_type =
      MakeNonNullUnique<ObjectType>(VMType::String().object_type);
  AddMethod<int>(L"size", pool,
                 std::function<int(const wstring&)>(
                     [](const wstring& str) { return str.size(); }),
                 string_type.value());
  AddMethod<int>(L"toint", pool,
                 std::function<int(const wstring&)>([](const wstring& str) {
                   try {
                     return std::stoi(str);
                   } catch (const std::invalid_argument& ia) {
                     return 0;
                   }
                 }),
                 string_type.value());
  AddMethod<bool>(L"empty", pool,
                  std::function<bool(const wstring&)>(
                      [](const wstring& str) { return str.empty(); }),
                  string_type.value());
  AddMethod<wstring>(L"tolower", pool,
                     std::function<wstring(const wstring&)>([](wstring str) {
                       for (auto& i : str) {
                         i = std::tolower(i, std::locale(""));
                       }
                       return str;
                     }),
                     string_type.value());
  AddMethod<wstring>(L"toupper", pool,
                     std::function<wstring(const wstring&)>([](wstring str) {
                       for (auto& i : str) {
                         i = std::toupper(i, std::locale(""));
                       }
                       return str;
                     }),
                     string_type.value());
  AddMethod<wstring>(L"shell_escape", pool,
                     std::function<wstring(const wstring&)>([](wstring str) {
                       return language::ShellEscape(str);
                     }),
                     string_type.value());
  AddMethod<wstring, int, int>(
      L"substr", pool,
      std::function<wstring(const wstring&, int, int)>(
          [](const wstring& str, int pos, int len) -> wstring {
            if (pos < 0 || len < 0 ||
                (static_cast<size_t>(pos + len) > str.size())) {
              return L"";
            }
            return str.substr(pos, len);
          }),
      string_type.value());
  AddMethod<bool, const wstring&>(
      L"starts_with", pool,
      std::function<bool(const wstring&, const wstring&)>(
          [](const wstring& str, const wstring& prefix) {
            return prefix.size() <= str.size() &&
                   (std::mismatch(prefix.begin(), prefix.end(), str.begin())
                        .first == prefix.end());
          }),
      string_type.value());
  AddMethod<int, const wstring&, int>(
      L"find", pool,
      std::function<int(const wstring&, const wstring&, int)>(
          [](const wstring& str, const wstring& pattern, int start_pos) {
            size_t pos = str.find(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type.value());
  AddMethod<int, const wstring&, int>(
      L"find_last_of", pool,
      std::function<int(const wstring&, const wstring&, int)>(
          [](const wstring& str, const wstring& pattern, int start_pos) {
            size_t pos = str.find_last_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type.value());
  AddMethod<int, const wstring&, int>(
      L"find_last_not_of", pool,
      std::function<int(const wstring&, const wstring&, int)>(
          [](const wstring& str, const wstring& pattern, int start_pos) {
            size_t pos = str.find_last_not_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type.value());
  AddMethod<int, const wstring&, int>(
      L"find_first_of", pool,
      std::function<int(const wstring&, const wstring&, int)>(
          [](const wstring& str, const wstring& pattern, int start_pos) {
            size_t pos = str.find_first_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type.value());
  AddMethod<int, const wstring&, int>(
      L"find_first_not_of", pool,
      std::function<int(const wstring&, const wstring&, int)>(
          [](const wstring& str, const wstring& pattern, int start_pos) {
            size_t pos = str.find_first_not_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type.value());
  environment.DefineType(std::move(string_type));

  container::Export<std::vector<std::wstring>>(pool, environment);
  container::Export<std::set<std::wstring>>(pool, environment);
}
}  // namespace afc::vm

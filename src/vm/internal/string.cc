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
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<std::vector<std::wstring>>>>::object_type_name =
    types::ObjectName(L"VectorString");

template <>
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<std::set<std::wstring>>>>::object_type_name =
    types::ObjectName(L"SetString");

template <typename ReturnType, typename... Args>
void AddMethod(const wstring& name, language::gc::Pool& pool,
               std::function<ReturnType(const wstring&, Args...)> callback,
               gc::Root<ObjectType>& string_type) {
  string_type.ptr()->AddField(
      name, NewCallback(pool, PurityType::kPure, callback).ptr());
}

void RegisterStringType(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> string_type = ObjectType::New(pool, types::String{});
  AddMethod<int>(L"size", pool,
                 std::function<int(const wstring&)>(
                     [](const wstring& str) { return str.size(); }),
                 string_type);
  AddMethod<int>(L"toint", pool,
                 std::function<int(const wstring&)>([](const wstring& str) {
                   try {
                     return std::stoi(str);
                   } catch (const std::invalid_argument& ia) {
                     return 0;
                   }
                 }),
                 string_type);
  AddMethod<bool>(L"empty", pool,
                  std::function<bool(const wstring&)>(
                      [](const wstring& str) { return str.empty(); }),
                  string_type);
  AddMethod<wstring>(L"tolower", pool,
                     std::function<wstring(const wstring&)>([](wstring str) {
                       for (auto& i : str) {
                         i = std::tolower(i, std::locale(""));
                       }
                       return str;
                     }),
                     string_type);
  AddMethod<wstring>(L"toupper", pool,
                     std::function<wstring(const wstring&)>([](wstring str) {
                       for (auto& i : str) {
                         i = std::toupper(i, std::locale(""));
                       }
                       return str;
                     }),
                     string_type);
  AddMethod<wstring>(L"shell_escape", pool,
                     std::function<wstring(const wstring&)>([](wstring str) {
                       return language::ShellEscape(str);
                     }),
                     string_type);
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
      string_type);
  AddMethod<bool, const wstring&>(
      L"starts_with", pool,
      std::function<bool(const wstring&, const wstring&)>(
          [](const wstring& str, const wstring& prefix) {
            return prefix.size() <= str.size() &&
                   (std::mismatch(prefix.begin(), prefix.end(), str.begin())
                        .first == prefix.end());
          }),
      string_type);
  AddMethod<int, const wstring&, int>(
      L"find", pool,
      std::function<int(const wstring&, const wstring&, int)>(
          [](const wstring& str, const wstring& pattern, int start_pos) {
            size_t pos = str.find(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type);
  AddMethod<int, const wstring&, int>(
      L"find_last_of", pool,
      std::function<int(const wstring&, const wstring&, int)>(
          [](const wstring& str, const wstring& pattern, int start_pos) {
            size_t pos = str.find_last_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type);
  AddMethod<int, const wstring&, int>(
      L"find_last_not_of", pool,
      std::function<int(const wstring&, const wstring&, int)>(
          [](const wstring& str, const wstring& pattern, int start_pos) {
            size_t pos = str.find_last_not_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type);
  AddMethod<int, const wstring&, int>(
      L"find_first_of", pool,
      std::function<int(const wstring&, const wstring&, int)>(
          [](const wstring& str, const wstring& pattern, int start_pos) {
            size_t pos = str.find_first_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type);
  AddMethod<int, const wstring&, int>(
      L"find_first_not_of", pool,
      std::function<int(const wstring&, const wstring&, int)>(
          [](const wstring& str, const wstring& pattern, int start_pos) {
            size_t pos = str.find_first_not_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type);
  environment.DefineType(string_type.ptr());

  container::Export<std::vector<std::wstring>>(pool, environment);
  container::Export<std::set<std::wstring>>(pool, environment);
}
}  // namespace afc::vm

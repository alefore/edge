#include "string.h"

#include <glog/logging.h>

#include "../public/callbacks.h"
#include "../public/environment.h"
#include "../public/set.h"
#include "../public/types.h"
#include "../public/value.h"
#include "../public/vector.h"
#include "../public/vm.h"

namespace afc::vm {
using language::MakeNonNullUnique;
template <>
const VMType VMTypeMapper<std::vector<wstring>*>::vmtype =
    VMType::ObjectType(L"VectorString");

template <>
const VMType VMTypeMapper<std::unique_ptr<std::vector<wstring>>>::vmtype =
    VMType::ObjectType(L"VectorString");

template <>
const VMType VMTypeMapper<std::set<wstring>*>::vmtype =
    VMType::ObjectType(L"SetString");

template <typename ReturnType, typename... Args>
void AddMethod(const wstring& name,
               std::function<ReturnType(wstring, Args...)> callback,
               ObjectType& string_type) {
  string_type.AddField(name, NewCallback(callback));
}

void RegisterStringType(Environment* environment) {
  auto string_type = MakeNonNullUnique<ObjectType>(VMType::String());
  AddMethod<int>(L"size", std::function<int(wstring)>([](wstring str) {
                   return str.size();
                 }),
                 *string_type);
  AddMethod<int>(L"toint", std::function<int(wstring)>([](wstring str) {
                   try {
                     return std::stoi(str);
                   } catch (const std::invalid_argument& ia) {
                     return 0;
                   }
                 }),
                 *string_type);
  AddMethod<bool>(L"empty", std::function<bool(wstring)>([](wstring str) {
                    return str.empty();
                  }),
                  *string_type);
  AddMethod<wstring>(L"tolower",
                     std::function<wstring(wstring)>([](wstring str) {
                       for (auto& i : str) {
                         i = std::tolower(i, std::locale(""));
                       }
                       return str;
                     }),
                     *string_type);
  AddMethod<wstring>(L"toupper",
                     std::function<wstring(wstring)>([](wstring str) {
                       for (auto& i : str) {
                         i = std::toupper(i, std::locale(""));
                       }
                       return str;
                     }),
                     *string_type);
  AddMethod<wstring>(L"shell_escape",
                     std::function<wstring(wstring)>([](wstring str) {
                       return language::ShellEscape(std::move(str));
                     }),
                     *string_type);
  AddMethod<wstring, int, int>(
      L"substr",
      std::function<wstring(wstring, int, int)>(
          [](const wstring& str, int pos, int len) -> wstring {
            if (pos < 0 || len < 0 ||
                (static_cast<size_t>(pos + len) > str.size())) {
              return L"";
            }
            return str.substr(pos, len);
          }),
      *string_type);
  AddMethod<bool, wstring>(
      L"starts_with",
      std::function<bool(wstring, wstring)>([](wstring str, wstring prefix) {
        return prefix.size() <= str.size() &&
               (std::mismatch(prefix.begin(), prefix.end(), str.begin())
                    .first == prefix.end());
      }),
      *string_type);
  AddMethod<int, wstring, int>(
      L"find",
      std::function<int(wstring, wstring, int)>(
          [](wstring str, wstring pattern, int start_pos) {
            size_t pos = str.find(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      *string_type);
  AddMethod<int, wstring, int>(
      L"find_last_of",
      std::function<int(wstring, wstring, int)>(
          [](wstring str, wstring pattern, int start_pos) {
            size_t pos = str.find_last_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      *string_type);
  AddMethod<int, wstring, int>(
      L"find_last_not_of",
      std::function<int(wstring, wstring, int)>(
          [](wstring str, wstring pattern, int start_pos) {
            size_t pos = str.find_last_not_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      *string_type);
  AddMethod<int, wstring, int>(
      L"find_first_of",
      std::function<int(wstring, wstring, int)>(
          [](wstring str, wstring pattern, int start_pos) {
            size_t pos = str.find_first_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      *string_type);
  AddMethod<int, wstring, int>(
      L"find_first_not_of",
      std::function<int(wstring, wstring, int)>(
          [](wstring str, wstring pattern, int start_pos) {
            size_t pos = str.find_first_not_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      *string_type);
  environment->DefineType(L"string", std::move(string_type));

  VMTypeMapper<std::vector<wstring>*>::Export(environment);
  VMTypeMapper<std::set<wstring>*>::Export(environment);
}

}  // namespace afc::vm

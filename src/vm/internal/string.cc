#include "string.h"

#include <cassert>

#include <glog/logging.h>

#include "../public/environment.h"
#include "../public/types.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

template<class T>
struct VMTypeMapper {};

template<>
struct VMTypeMapper<bool> {
  static std::unique_ptr<Value> New(bool value) {
    return Value::NewBool(value);
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<bool>::vmtype = VMType(VMType::VM_BOOLEAN);

template<>
struct VMTypeMapper<int> {
  static int get(Value* value) {
    return value->integer;
  }

  static std::unique_ptr<Value> New(int value) {
    return Value::NewInteger(value);
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<int>::vmtype = VMType(VMType::VM_INTEGER);

template<>
struct VMTypeMapper<wstring> {
  static wstring get(Value* value) {
    return std::move(value->str);
  }

  static std::unique_ptr<Value> New(wstring value) {
    return Value::NewString(value);
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<wstring>::vmtype = VMType(VMType::VM_STRING);

template <typename... Args>
struct AddArgs {
  // Terminates the recursion.
  static void Run(std::vector<VMType>*) {}
};

template <typename Arg0, typename... Args>
struct AddArgs<Arg0, Args...> {
  static void Run(std::vector<VMType>* output) {
    output->push_back(VMTypeMapper<Arg0>::vmtype);
    AddArgs<Args...>::Run(output);
  }
};

template <typename ReturnType>
std::unique_ptr<Value> RunCallback(
    std::function<ReturnType(wstring)> callback,
    const vector<unique_ptr<Value>>& args) {
  CHECK_EQ(args.size(), 1);
  return VMTypeMapper<ReturnType>::New(callback(std::move(args[0]->str)));
}

template <typename ReturnType, typename A0>
std::unique_ptr<Value> RunCallback(
    std::function<ReturnType(wstring, A0)> callback,
    const vector<unique_ptr<Value>>& args) {
  CHECK_EQ(args.size(), 2);
  return VMTypeMapper<ReturnType>::New(callback(
      std::move(args[0]->str),
      VMTypeMapper<A0>::get(args[1].get())));
}

template <typename ReturnType, typename A0, typename A1>
std::unique_ptr<Value> RunCallback(
    std::function<ReturnType(wstring, A0, A1)> callback,
    const vector<unique_ptr<Value>>& args) {
  CHECK_EQ(args.size(), 3);
  return VMTypeMapper<ReturnType>::New(callback(
      std::move(args[0]->str),
      VMTypeMapper<A0>::get(args[1].get()),
      VMTypeMapper<A1>::get(args[2].get())));
}

template <typename ReturnType, typename ...Args>
void AddMethod(const wstring& name,
               std::function<ReturnType(wstring, Args...)> callback,
               ObjectType* string_type) {
  unique_ptr<Value> callback_wrapper(new Value(VMType::FUNCTION));
  callback_wrapper->type.type_arguments.push_back(
      VMTypeMapper<ReturnType>().vmtype);
  callback_wrapper->type.type_arguments.push_back(VMType(VMType::VM_STRING));
  AddArgs<Args...>::Run(&callback_wrapper->type.type_arguments);
  callback_wrapper->callback = [callback](
      vector<unique_ptr<Value>> args, OngoingEvaluation* evaluation) {
    CHECK_EQ(args[0]->type, VMType::VM_STRING);
    evaluation->return_consumer(
        RunCallback<ReturnType, Args...>(callback, args));
  };
  string_type->AddField(name, std::move(callback_wrapper));
}

void RegisterStringType(Environment* environment) {
  unique_ptr<ObjectType> string_type(new ObjectType(VMType::String()));
  AddMethod<int>(L"size",
                 std::function<int(wstring)>(
                     [](wstring str) { return str.size(); }),
                 string_type.get());
  AddMethod<bool>(L"empty",
                  std::function<bool(wstring)>(
                      [](wstring str) { return str.empty(); }),
                  string_type.get());
  AddMethod<wstring>(L"tolower",
                     std::function<wstring(wstring)>([](wstring str) {
                       for (auto& i : str) {
                         i = std::tolower(i, std::locale(""));
                       }
                       return str;
                     }),
                     string_type.get());
  AddMethod<wstring>(L"toupper",
                     std::function<wstring(wstring)>([](wstring str) {
                       for (auto& i : str) {
                         i = std::toupper(i, std::locale(""));
                       }
                       return str;
                     }),
                     string_type.get());
  AddMethod<wstring>(L"shell_escape",
                     std::function<wstring(wstring)>([](wstring str) {
                       wstring output;
                       output.push_back(L'\'');
                       for (auto c : str) {
                         if (c == L'\'') {
                           output.push_back('\\');
                         }
                         output.push_back(c);
                       }
                       output.push_back(L'\'');
                       return output;
                     }),
                     string_type.get());
  AddMethod<wstring, int, int>(
      L"substr",
      std::function<wstring(wstring, int, int)>(
        [](const wstring& str, int pos, int len) -> wstring {
          if (pos < 0 || len < 0
              || (static_cast<size_t>(pos + len) > str.size())) {
            return L"";
          }
          return str.substr(pos, len);
        }),
      string_type.get());
  AddMethod<bool, wstring>(
      L"starts_with",
      std::function<bool(wstring, wstring)>(
          [](wstring str, wstring prefix) {
            return prefix.size() <= str.size()
                && (std::mismatch(prefix.begin(), prefix.end(),
                                  str.begin()).first
                    == prefix.end());
          }),
      string_type.get());
  AddMethod<int, wstring, int>(
      L"find",
      std::function<int(wstring, wstring, int)>(
          [](wstring str, wstring pattern, int start_pos) {
            size_t pos = str.find(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type.get());
  AddMethod<int, wstring, int>(
      L"find_last_of",
      std::function<int(wstring, wstring, int)>(
          [](wstring str, wstring pattern, int start_pos) {
            size_t pos = str.find_last_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type.get());
  AddMethod<int, wstring, int>(
      L"find_last_not_of",
      std::function<int(wstring, wstring, int)>(
          [](wstring str, wstring pattern, int start_pos) {
            size_t pos = str.find_last_not_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type.get());
  AddMethod<int, wstring, int>(
      L"find_first_of",
      std::function<int(wstring, wstring, int)>(
          [](wstring str, wstring pattern, int start_pos) {
            size_t pos = str.find_first_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type.get());
  AddMethod<int, wstring, int>(
      L"find_first_not_of",
      std::function<int(wstring, wstring, int)>(
          [](wstring str, wstring pattern, int start_pos) {
            size_t pos = str.find_first_not_of(pattern, start_pos);
            return pos == wstring::npos ? -1 : pos;
          }),
      string_type.get());
  environment->DefineType(L"string", std::move(string_type));

  environment->Define(L"tostring", Value::NewFunction(
      {VMType::String(), VMType::Integer()},
      [](vector<unique_ptr<Value>> args) {
        CHECK_EQ(args.size(), 1);
        CHECK_EQ(args[0]->type.type, VMType::VM_INTEGER);
        return Value::NewString(std::to_wstring(args[0]->integer));
      }));
}

}  // namespace vm
}  // namespace afc

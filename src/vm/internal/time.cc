#include "time.h"

#include <glog/logging.h>

#include "../public/callbacks.h"
#include "../public/environment.h"
#include "../public/set.h"
#include "../public/types.h"
#include "../public/value.h"
#include "../public/vector.h"
#include "../public/vm.h"
#include "src/language/safe_types.h"
#include "wstring.h"

namespace afc::vm {
using language::NonNull;
using language::Success;

using Time = struct timespec;

// We box it so that the C++ type system can distinguish Time and Duration.
// Otherwise, VMTypeMapper<Time> and VMTypeMapper<Duration> would actually
// clash.
struct Duration {
  Time value = {0, 0};
};

template <>
struct VMTypeMapper<Time> {
  static Time get(Value* value) {
    CHECK(value != nullptr);
    CHECK_EQ(value->type, vmtype);
    CHECK(value->user_value != nullptr);
    return *static_cast<Time*>(value->user_value.get());
  }

  static NonNull<Value::Ptr> New(Time value) {
    return Value::NewObject(vmtype.object_type,
                            shared_ptr<void>(new Time(value), [](void* v) {
                              delete static_cast<Time*>(v);
                            }));
  }

  static const VMType vmtype;
};

template <>
struct VMTypeMapper<Duration> {
  static Duration get(Value* value) {
    CHECK(value != nullptr);
    CHECK_EQ(value->type, vmtype);
    CHECK(value->user_value != nullptr);
    return *static_cast<Duration*>(value->user_value.get());
  }

  static NonNull<Value::Ptr> New(Duration value) {
    return Value::NewObject(vmtype.object_type,
                            shared_ptr<void>(new Duration(value), [](void* v) {
                              delete static_cast<Time*>(v);
                            }));
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<Time>::vmtype = VMType::ObjectType(L"Time");
const VMType VMTypeMapper<Duration>::vmtype = VMType::ObjectType(L"Duration");

template <typename ReturnType, typename... Args>
void AddMethod(const wstring& name,
               std::function<ReturnType(wstring, Args...)> callback,
               ObjectType* string_type) {
  string_type->AddField(name, NewCallback(callback));
}

void RegisterTimeType(Environment* environment) {
  auto time_type = std::make_unique<ObjectType>(L"Time");
  time_type->AddField(
      L"tostring", vm::NewCallback(std::function<wstring(Time)>([](Time t) {
        return std::to_wstring(t.tv_sec) + L"." + std::to_wstring(t.tv_nsec);
      })));
  // TODO: Correctly handle errors (abort evaluation).
  time_type->AddField(
      L"AddDays",
      vm::NewCallback(std::function<Time(Time, int)>([](Time input, int days) {
        struct tm t;
        // TODO: Don't ignore return value.
        localtime_r(&(input.tv_sec), &t);
        t.tm_mday += days;
        return Time{.tv_sec = mktime(&t), .tv_nsec = input.tv_nsec};
      })));
  time_type->AddField(
      L"format",
      Value::NewFunction(
          {VMType::String(), time_type->type(), VMType::String()},
          [](std::vector<NonNull<Value::Ptr>> args,
             Trampoline*) -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            CHECK(args[0]->IsObject());
            Time input =
                language::Pointer(static_cast<Time*>(args[0]->user_value.get()))
                    .Reference();
            CHECK(args[1]->IsString());
            struct tm t;
            localtime_r(&(input.tv_sec), &t);
            char buffer[2048];
            if (strftime(buffer, sizeof(buffer),
                         ToByteString(std::move(args[1]->str)).c_str(),
                         &t) == 0) {
              return futures::Past(language::Error(L"strftime error"));
            }
            return futures::Past(Success(EvaluationOutput::Return(
                Value::NewString(FromByteString(buffer)))));
          }));
  time_type->AddField(L"year",
                      vm::NewCallback(std::function<int(Time)>([](Time input) {
                        struct tm t;
                        // TODO: Don't ignore return value.
                        localtime_r(&(input.tv_sec), &t);
                        return t.tm_year;
                      })));
  environment->DefineType(L"Time", std::move(time_type));
  environment->Define(L"Now", vm::NewCallback([]() {
                        Time output;
                        CHECK_NE(clock_gettime(0, &output), -1);
                        return output;
                      }));
  environment->Define(
      L"ParseTime",
      vm::NewCallback([](std::wstring value, std::wstring format) {
        struct tm t = {};
        if (strptime(ToByteString(value).c_str(), ToByteString(format).c_str(),
                     &t) == nullptr) {
          LOG(ERROR) << "Parsing error: " << value << ": " << format;
          // TODO: Don't ignore return value.
        }
        auto output = mktime(&t);
        if (output == -1) {
          LOG(ERROR) << "mktime error: " << value << ": " << format;
          // TODO: Don't ignore return value.
        }
        return Time{.tv_sec = output, .tv_nsec = 0};
      }));

  auto duration_type = std::make_unique<ObjectType>(L"Duration");
  environment->DefineType(L"Duration", std::move(duration_type));
  environment->Define(L"Seconds", vm::NewCallback([](int input) {
                        return Duration{.value{.tv_sec = input, .tv_nsec = 0}};
                      }));
}

}  // namespace afc::vm

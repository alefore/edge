#include "src/vm/time.h"

#include <glog/logging.h>

#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/vm/callbacks.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace afc::vm {
using language::Error;
using language::FromByteString;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ToByteString;
using language::ValueOrError;

namespace gc = language::gc;

using Time = struct timespec;

// We box it so that the C++ type system can distinguish Time and Duration.
// Otherwise, VMTypeMapper<Time> and VMTypeMapper<Duration> would actually
// clash.
struct Duration {
  Time value = {0, 0};
};

template <>
struct VMTypeMapper<Time> {
  static Time get(Value& value) {
    return value.get_user_value<Time>(object_type_name).value();
  }

  static gc::Root<Value> New(language::gc::Pool& pool, Time value) {
    return Value::NewObject(pool, object_type_name,
                            MakeNonNullShared<Time>(value));
  }

  static const vm::types::ObjectName object_type_name;
};

template <>
struct VMTypeMapper<Duration> {
  static Duration get(Value& value) {
    return value.get_user_value<Duration>(object_type_name).value();
  }

  static gc::Root<Value> New(language::gc::Pool& pool, Duration value) {
    return Value::NewObject(pool, object_type_name,
                            MakeNonNullShared<Duration>(value));
  }

  static const vm::types::ObjectName object_type_name;
};

const vm::types::ObjectName VMTypeMapper<Time>::object_type_name =
    vm::types::ObjectName(L"Time");
const vm::types::ObjectName VMTypeMapper<Duration>::object_type_name =
    vm::types::ObjectName(L"Duration");

template <typename ReturnType, typename... Args>
void AddMethod(const std::wstring& name, gc::Pool& pool,
               std::function<ReturnType(std::wstring, Args...)> callback,
               ObjectType* string_type) {
  string_type->AddField(name, NewCallback(pool, callback).ptr());
}

ValueOrError<struct tm> LocalTime(const time_t* time_input) {
  struct tm output;
  if (localtime_r(time_input, &output) == nullptr) {
    int localtime_error = errno;
    return Error(L"localtime_r failure:" +
                 FromByteString(strerror(localtime_error)));
  }
  return output;
}

void RegisterTimeType(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> time_type =
      ObjectType::New(pool, VMTypeMapper<Time>::object_type_name);
  time_type.ptr()->AddField(
      L"tostring",
      vm::NewCallback(pool, PurityType::kPure,
                      std::function<std::wstring(Time)>([](Time t) {
                        std::wstring decimal = std::to_wstring(t.tv_nsec);
                        if (decimal.length() < 9)
                          decimal.insert(0, 9 - decimal.length(), L'0');
                        return std::to_wstring(t.tv_sec) + L"." + decimal;
                      }))
          .ptr());
  time_type.ptr()->AddField(
      L"AddDays",
      vm::NewCallback(pool, PurityType::kPure,
                      [](Time input, int days) -> futures::ValueOrError<Time> {
                        FUTURES_ASSIGN_OR_RETURN(struct tm t,
                                                 LocalTime(&input.tv_sec));
                        t.tm_mday += days;
                        return futures::Past(Time{.tv_sec = mktime(&t),
                                                  .tv_nsec = input.tv_nsec});
                      })
          .ptr());
  time_type.ptr()->AddField(
      L"format",
      vm::NewCallback(
          pool, PurityType::kPure,
          [](Time input,
             std::wstring format_str) -> futures::ValueOrError<std::wstring> {
            FUTURES_ASSIGN_OR_RETURN(struct tm t, LocalTime(&input.tv_sec));
            char buffer[2048];
            if (strftime(buffer, sizeof(buffer),
                         ToByteString(format_str).c_str(), &t) == 0) {
              return futures::Past(Error(L"strftime error"));
            }
            return futures::Past(FromByteString(buffer));
          })
          .ptr());
  time_type.ptr()->AddField(
      L"year", vm::NewCallback(pool, PurityType::kPure,
                               [](Time input) -> futures::ValueOrError<int> {
                                 FUTURES_ASSIGN_OR_RETURN(
                                     struct tm t, LocalTime(&input.tv_sec));
                                 return futures::Past(t.tm_year);
                               })
                   .ptr());
  environment.Define(L"Now", vm::NewCallback(pool, PurityType::kReader, []() {
                       Time output;
                       CHECK_NE(clock_gettime(0, &output), -1);
                       return output;
                     }));
  environment.Define(
      L"ParseTime",
      vm::NewCallback(
          pool, PurityType::kPure,
          [](std::wstring value,
             std::wstring format) -> futures::ValueOrError<Time> {
            struct tm t = {};
            if (strptime(ToByteString(value).c_str(),
                         ToByteString(format).c_str(), &t) == nullptr)
              return futures::Past(Error(L"strptime error: value: " + value +
                                         L", format: " + format));
            if (time_t output = mktime(&t); output != -1)
              return futures::Past(Time{.tv_sec = output, .tv_nsec = 0});
            return futures::Past(Error(L"mktime error: value: " + value +
                                       L", format: " + format));
          }));

  gc::Root<ObjectType> duration_type =
      ObjectType::New(pool, VMTypeMapper<Duration>::object_type_name);
  duration_type.ptr()->AddField(
      L"days", vm::NewCallback(pool, PurityType::kPure,
                               std::function<int(Duration)>([](Duration input) {
                                 return input.value.tv_sec / (24 * 60 * 60);
                               }))
                   .ptr());
  environment.Define(L"Seconds",
                     vm::NewCallback(pool, PurityType::kPure, [](int input) {
                       return Duration{.value{.tv_sec = input, .tv_nsec = 0}};
                     }));

  environment.Define(
      L"DurationBetween",
      vm::NewCallback(pool, PurityType::kPure, [](Time a, Time b) {
        b.tv_sec -= a.tv_sec;
        if (b.tv_nsec < a.tv_nsec) {
          b.tv_nsec = 1e9 - a.tv_nsec + b.tv_nsec;
          b.tv_sec--;
        } else {
          b.tv_nsec -= a.tv_nsec;
        }
        return Duration{.value = b};
      }));

  environment.DefineType(time_type.ptr());
  environment.DefineType(duration_type.ptr());
}

}  // namespace afc::vm

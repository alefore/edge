#include "src/vm/time.h"

#include <glog/logging.h>

#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/vm/callbacks.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

using afc::language::Error;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::vm {

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
    vm::types::ObjectName{Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Time")}};
const vm::types::ObjectName VMTypeMapper<Duration>::object_type_name =
    vm::types::ObjectName{
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Duration")}};

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
    return Error{LazyString{L"localtime_r failure:"} +
                 LazyString{FromByteString(strerror(localtime_error))}};
  }
  return output;
}

void RegisterTimeType(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> time_type =
      ObjectType::New(pool, VMTypeMapper<Time>::object_type_name);
  time_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"tostring"}}}},
      vm::NewCallback(pool, kPurityTypePure,
                      std::function<std::wstring(Time)>([](Time t) {
                        std::wstring decimal = std::to_wstring(t.tv_nsec);
                        if (decimal.length() < 9)
                          decimal.insert(0, 9 - decimal.length(), L'0');
                        return std::to_wstring(t.tv_sec) + L"." + decimal;
                      }))
          .ptr());
  time_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"AddDays"}}}},
      vm::NewCallback(pool, kPurityTypePure,
                      [](Time input, int days) -> futures::ValueOrError<Time> {
                        FUTURES_ASSIGN_OR_RETURN(struct tm t,
                                                 LocalTime(&input.tv_sec));
                        t.tm_mday += days;
                        return futures::Past(Time{.tv_sec = mktime(&t),
                                                  .tv_nsec = input.tv_nsec});
                      })
          .ptr());
  time_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"format"}}}},
      vm::NewCallback(
          pool, kPurityTypePure,
          [](Time input,
             LazyString format_str) -> futures::ValueOrError<std::wstring> {
            FUTURES_ASSIGN_OR_RETURN(struct tm t, LocalTime(&input.tv_sec));
            char buffer[2048];
            if (strftime(buffer, sizeof(buffer), format_str.ToBytes().c_str(),
                         &t) == 0) {
              return futures::Past(Error{LazyString{L"strftime error"}});
            }
            return futures::Past(FromByteString(buffer));
          })
          .ptr());
  time_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"year"}}}},
      vm::NewCallback(pool, kPurityTypePure,
                      [](Time input) -> futures::ValueOrError<int> {
                        FUTURES_ASSIGN_OR_RETURN(struct tm t,
                                                 LocalTime(&input.tv_sec));
                        return futures::Past(t.tm_year);
                      })
          .ptr());
  environment.Define(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"Now"}}}},
      vm::NewCallback(pool, kPurityTypeReader, []() {
        Time output;
        CHECK_NE(clock_gettime(0, &output), -1);
        return output;
      }));
  environment.Define(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"ParseTime"}}}},
      vm::NewCallback(
          pool, kPurityTypePure,
          [](LazyString value,
             LazyString format) -> futures::ValueOrError<Time> {
            struct tm t = {};
            if (strptime(value.ToBytes().c_str(), format.ToBytes().c_str(),
                         &t) == nullptr)
              return futures::Past(
                  Error{LazyString{L"strptime error: value: "} + value +
                        LazyString{L", format: "} + format});
            if (time_t output = mktime(&t); output != -1)
              return futures::Past(Time{.tv_sec = output, .tv_nsec = 0});
            return futures::Past(Error{LazyString{L"mktime error: value: "} +
                                       value + LazyString{L", format: "} +
                                       format});
          }));

  gc::Root<ObjectType> duration_type =
      ObjectType::New(pool, VMTypeMapper<Duration>::object_type_name);
  duration_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"days"}}}},
      vm::NewCallback(pool, kPurityTypePure,
                      std::function<int(Duration)>([](Duration input) {
                        return input.value.tv_sec / (24 * 60 * 60);
                      }))
          .ptr());
  environment.Define(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"Seconds"}}}},
      vm::NewCallback(pool, kPurityTypePure, [](int input) {
        return Duration{.value{.tv_sec = input, .tv_nsec = 0}};
      }));

  environment.Define(Identifier{NonEmptySingleLine{
                         SingleLine{LazyString{L"DurationBetween"}}}},
                     vm::NewCallback(pool, kPurityTypePure, [](Time a, Time b) {
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

#include "src/vm/internal/time.h"

#include <glog/logging.h>

#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc::vm {
using language::Error;
using language::FromByteString;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ToByteString;

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

  static const VMTypeObjectTypeName object_type_name;
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

  static const VMTypeObjectTypeName object_type_name;
};

const VMTypeObjectTypeName VMTypeMapper<Time>::object_type_name =
    VMTypeObjectTypeName(L"Time");
const VMTypeObjectTypeName VMTypeMapper<Duration>::object_type_name =
    VMTypeObjectTypeName(L"Duration");

template <typename ReturnType, typename... Args>
void AddMethod(const wstring& name, gc::Pool& pool,
               std::function<ReturnType(wstring, Args...)> callback,
               ObjectType* string_type) {
  string_type->AddField(name, NewCallback(pool, callback).ptr());
}

void RegisterTimeType(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> time_type =
      ObjectType::New(pool, VMTypeMapper<Time>::object_type_name);
  time_type.ptr()->AddField(
      L"tostring",
      vm::NewCallback(pool, PurityType::kPure,
                      std::function<wstring(Time)>([](Time t) {
                        std::wstring decimal = std::to_wstring(t.tv_nsec);
                        if (decimal.length() < 9)
                          decimal.insert(0, 9 - decimal.length(), L'0');
                        return std::to_wstring(t.tv_sec) + L"." + decimal;
                      }))
          .ptr());
  // TODO: Correctly handle errors (abort evaluation).
  time_type.ptr()->AddField(
      L"AddDays",
      vm::NewCallback(pool, PurityType::kPure,
                      std::function<Time(Time, int)>([](Time input, int days) {
                        struct tm t;
                        // TODO: Don't ignore return value.
                        localtime_r(&(input.tv_sec), &t);
                        t.tm_mday += days;
                        return Time{.tv_sec = mktime(&t),
                                    .tv_nsec = input.tv_nsec};
                      }))
          .ptr());
  time_type.ptr()->AddField(
      L"format",
      Value::NewFunction(
          pool, PurityType::kPure,
          {{.variant = types::String{}},
           time_type.ptr()->type(),
           {.variant = types::String{}}},
          [](std::vector<gc::Root<Value>> args, Trampoline& trampoline)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            Time input = VMTypeMapper<Time>::get(args[0].ptr().value());
            struct tm t;
            localtime_r(&(input.tv_sec), &t);
            char buffer[2048];
            if (strftime(buffer, sizeof(buffer),
                         ToByteString(std::move(args[1].ptr()->get_string()))
                             .c_str(),
                         &t) == 0) {
              return futures::Past(Error(L"strftime error"));
            }
            return futures::Past(Success(EvaluationOutput::Return(
                Value::NewString(trampoline.pool(), FromByteString(buffer)))));
          })
          .ptr());
  time_type.ptr()->AddField(
      L"year", vm::NewCallback(pool, PurityType::kPure,
                               std::function<int(Time)>([](Time input) {
                                 struct tm t;
                                 // TODO: Don't ignore return value.
                                 localtime_r(&(input.tv_sec), &t);
                                 return t.tm_year;
                               }))
                   .ptr());
  environment.Define(L"Now", vm::NewCallback(pool, PurityType::kUnknown, []() {
                       Time output;
                       CHECK_NE(clock_gettime(0, &output), -1);
                       return output;
                     }));
  environment.Define(
      L"ParseTime",
      vm::Value::NewFunction(
          pool, PurityType::kPure,
          {time_type.ptr()->type(),
           {.variant = types::String{}},
           {.variant = types::String{}}},
          [](std::vector<gc::Root<Value>> args, Trampoline& trampoline)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            std::wstring value = args[0].ptr()->get_string();
            std::wstring format = args[1].ptr()->get_string();
            struct tm t = {};
            if (strptime(ToByteString(value).c_str(),
                         ToByteString(format).c_str(), &t) == nullptr) {
              return futures::Past(Error(L"strptime error: value: " + value +
                                         L", format: " + format));
            }
            time_t output = mktime(&t);
            if (output == -1) {
              return futures::Past(Error(L"mktime error: value: " + value +
                                         L", format: " + format));
            }
            return futures::Past(
                Success(EvaluationOutput::Return(VMTypeMapper<Time>::New(
                    trampoline.pool(), Time{.tv_sec = output, .tv_nsec = 0}))));
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

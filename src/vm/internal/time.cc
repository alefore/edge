#include "src/vm/internal/time.h"

#include <glog/logging.h>

#include "src/language/safe_types.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/set.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vector.h"
#include "src/vm/public/vm.h"
#include "wstring.h"

namespace afc::vm {
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;

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
    CHECK_EQ(value.type, vmtype);
    CHECK(value.user_value != nullptr);
    return *static_cast<Time*>(value.user_value.get());
  }

  static gc::Root<Value> New(language::gc::Pool& pool, Time value) {
    return Value::NewObject(pool, vmtype.object_type,
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

  static gc::Root<Value> New(language::gc::Pool& pool, Duration value) {
    return Value::NewObject(pool, vmtype.object_type,
                            shared_ptr<void>(new Duration(value), [](void* v) {
                              delete static_cast<Time*>(v);
                            }));
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<Time>::vmtype = VMType::ObjectType(L"Time");
const VMType VMTypeMapper<Duration>::vmtype = VMType::ObjectType(L"Duration");

template <typename ReturnType, typename... Args>
void AddMethod(const wstring& name, gc::Pool& pool,
               std::function<ReturnType(wstring, Args...)> callback,
               ObjectType* string_type) {
  string_type->AddField(name, NewCallback(pool, callback));
}

void RegisterTimeType(gc::Pool& pool, Environment& environment) {
  auto time_type = MakeNonNullUnique<ObjectType>(L"Time");
  time_type->AddField(
      L"tostring",
      vm::NewCallback(pool, std::function<wstring(Time)>([](Time t) {
                        return std::to_wstring(t.tv_sec) + L"." +
                               std::to_wstring(t.tv_nsec);
                      })));
  // TODO: Correctly handle errors (abort evaluation).
  time_type->AddField(
      L"AddDays",
      vm::NewCallback(
          pool, std::function<Time(Time, int)>([](Time input, int days) {
            struct tm t;
            // TODO: Don't ignore return value.
            localtime_r(&(input.tv_sec), &t);
            t.tm_mday += days;
            return Time{.tv_sec = mktime(&t), .tv_nsec = input.tv_nsec};
          })));
  time_type->AddField(
      L"format",
      Value::NewFunction(
          pool, {VMType::String(), time_type->type(), VMType::String()},
          [](std::vector<gc::Root<Value>> args, Trampoline& trampoline)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            CHECK(args[0].ptr()->IsObject());
            Time input = language::Pointer(static_cast<Time*>(
                                               args[0].ptr()->user_value.get()))
                             .Reference();
            CHECK(args[1].ptr()->IsString());
            struct tm t;
            localtime_r(&(input.tv_sec), &t);
            char buffer[2048];
            if (strftime(buffer, sizeof(buffer),
                         ToByteString(std::move(args[1].ptr()->str)).c_str(),
                         &t) == 0) {
              return futures::Past(language::Error(L"strftime error"));
            }
            return futures::Past(Success(EvaluationOutput::Return(
                Value::NewString(trampoline.pool(), FromByteString(buffer)))));
          }));
  time_type->AddField(
      L"year", vm::NewCallback(pool, std::function<int(Time)>([](Time input) {
                                 struct tm t;
                                 // TODO: Don't ignore return value.
                                 localtime_r(&(input.tv_sec), &t);
                                 return t.tm_year;
                               })));
  environment.DefineType(L"Time", std::move(time_type));
  environment.Define(L"Now", vm::NewCallback(pool, []() {
                       Time output;
                       CHECK_NE(clock_gettime(0, &output), -1);
                       return output;
                     }));
  environment.Define(
      L"ParseTime",
      vm::NewCallback(pool, [](std::wstring value, std::wstring format) {
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

  auto duration_type = MakeNonNullUnique<ObjectType>(L"Duration");
  environment.DefineType(L"Duration", std::move(duration_type));
  environment.Define(L"Seconds", vm::NewCallback(pool, [](int input) {
                       return Duration{.value{.tv_sec = input, .tv_nsec = 0}};
                     }));
}

}  // namespace afc::vm

#include "src/vm/expression.h"

#include "src/language/container.h"
#include "src/language/once_only_function.h"
#include "src/language/wstring.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
using ::operator<<;

/* static */ gc::Root<Trampoline> Trampoline::New(Options options) {
  gc::Pool& pool = options.environment.pool();
  return pool.NewRoot(MakeNonNullUnique<Trampoline>(
      ConstructorAccessTag{}, std::move(options), Stack::New(pool).ptr()));
}

Trampoline::Trampoline(ConstructorAccessTag, Options options,
                       gc::Ptr<Stack> stack)
    : environment_(std::move(options.environment)),
      stack_(std::move(stack)),
      yield_callback_(std::move(options.yield_callback)) {}

futures::ValueOrError<EvaluationOutput> Trampoline::Bounce(
    const gc::Ptr<Expression>& expression, Type type) {
  if (!expression->SupportsType(type)) {
    LOG(FATAL) << "Expression has types: " << TypesToString(expression->Types())
               << ", expected: " << type;
  }
  static const size_t kMaximumJumps = 100;
  if (++jumps_ < kMaximumJumps || yield_callback_ == nullptr)
    return expression->Evaluate(*this, type);

  futures::Future<language::ValueOrError<EvaluationOutput>> output;
  yield_callback_(OnceOnlyFunction<void()>(
      [this, type, expression_root = expression.ToRoot(),
       consumer = std::move(output.consumer)]() mutable {
        jumps_ = 0;
        Bounce(expression_root.ptr(), type).SetConsumer(std::move(consumer));
      }));
  return std::move(output.value);
}

void Trampoline::SetEnvironment(gc::Ptr<Environment> environment) {
  environment_ = std::move(environment);
}

const gc::Ptr<Environment>& Trampoline::environment() const {
  return environment_;
}

Stack& Trampoline::stack() { return stack_.value(); }

gc::Pool& Trampoline::pool() const { return environment_.pool(); }

std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
Trampoline::Expand() const {
  return {environment_.object_metadata(), stack_.object_metadata()};
}

bool Expression::SupportsType(const Type& type) {
  auto types = Types();
  if (std::find(types.begin(), types.end(), type) != types.end()) {
    return true;
  }
  return container::FindFirstIf(types,
                                [&type](const Type& source) {
                                  CHECK(!(source == type));
                                  return GetImplicitPromotion(source, type) !=
                                         nullptr;
                                })
      .has_value();
}

ValueOrError<std::unordered_set<Type>> CombineReturnTypes(
    std::unordered_set<Type> a, std::unordered_set<Type> b) {
  if (a.empty()) return Success(b);
  if (b.empty()) return Success(a);
  if (a != b) {
    return Error{LazyString{L"Incompatible return types found: "} +
                 ToQuotedSingleLine(*a.cbegin()) + LazyString{L" and "} +
                 ToQuotedSingleLine(*b.cbegin()) + LazyString{L"."}};
  }
  return Success(a);
}

futures::ValueOrError<gc::Root<Value>> Evaluate(
    const gc::Ptr<Expression>& expr, const gc::Ptr<Environment>& environment,
    std::function<void(OnceOnlyFunction<void()>)> yield_callback) {
  gc::Root<Trampoline> trampoline = Trampoline::New(
      Trampoline::Options{.environment = std::move(environment),
                          .yield_callback = std::move(yield_callback)});
  return OnError(trampoline->Bounce(expr, expr->Types()[0])
                     .Transform([trampoline](EvaluationOutput value)
                                    -> language::ValueOrError<gc::Root<Value>> {
                       DVLOG(5)
                           << "Evaluation done: " << value.value.ptr().value();
                       return Success(std::move(value.value));
                     }),
                 [](Error error) {
                   LOG(INFO) << "Evaluation error: " << error;
                   return futures::Past(error);
                 });
}

ImplicitPromotionCallback GetImplicitPromotion(Type original, Type desired) {
  if (original == desired) return [](gc::Root<Value> value) { return value; };

  types::Function* original_function = std::get_if<types::Function>(&original);
  types::Function* desired_function = std::get_if<types::Function>(&desired);

  if (original_function == nullptr || desired_function == nullptr ||
      original_function->inputs.size() != desired_function->inputs.size())
    return nullptr;

  ImplicitPromotionCallback output_callback = GetImplicitPromotion(
      original_function->output.get(), desired_function->output.get());
  if (output_callback == nullptr) return nullptr;

  std::vector<ImplicitPromotionCallback> inputs_callbacks;
  for (size_t i = 0; i < original_function->inputs.size(); i++) {
    // Undo the promotion: we deliberately swap the order of desired and
    // original parameters for the function arguments.
    if (auto argument_callback = GetImplicitPromotion(
            desired_function->inputs[i], original_function->inputs[i]);
        argument_callback != nullptr) {
      inputs_callbacks.push_back(std::move(argument_callback));
    } else {
      return nullptr;
    }
  }

  if ((!desired_function->function_purity.writes_external_outputs &&
       original_function->function_purity.writes_external_outputs) ||
      (!desired_function->function_purity.writes_local_variables &&
       original_function->function_purity.writes_local_variables))
    return nullptr;

  return [output_callback, inputs_callbacks,
          purity = desired_function->function_purity](gc::Root<Value> value) {
    const types::Function& value_function_type =
        std::get<types::Function>(value.ptr()->type());
    return Value::NewFunction(
        value.pool(), purity, value_function_type.output.get(),
        value_function_type.inputs,
        std::bind_front(
            [output_callback, inputs_callbacks](
                gc::Root<Value> original_callback,
                std::vector<gc::Root<Value>> arguments,
                Trampoline& trampoline) {
              CHECK_EQ(inputs_callbacks.size(), arguments.size());
              for (size_t i = 0; i < arguments.size(); ++i) {
                arguments[i] = inputs_callbacks[i](std::move(arguments[i]));
              }
              return original_callback.ptr()
                  ->RunFunction(std::move(arguments), trampoline)
                  .Transform([output_callback](gc::Root<Value> output) {
                    return Success(output_callback(std::move(output)));
                  });
            },
            value));
  };
}
}  // namespace afc::vm

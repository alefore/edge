#include "binary_operator.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "src/vm/internal/compilation.h"

namespace afc {
namespace vm {

BinaryOperator::BinaryOperator(
    unique_ptr<Expression> a, unique_ptr<Expression> b, const VMType type,
    function<void(const Value&, const Value&, Value*)> callback)
    : a_(std::move(a)), b_(std::move(b)), type_(type), operator_(callback) {
  CHECK(a_ != nullptr);
  CHECK(b_ != nullptr);
}

const VMType& BinaryOperator::type() { return type_; }

void BinaryOperator::Evaluate(Trampoline* trampoline) {
  // TODO: Bunch of things here can be turned to unique_ptr.
  auto type_copy = type_;
  auto operator_copy = operator_;
  std::shared_ptr<Expression> a_shared = a_;
  std::shared_ptr<Expression> b_shared = b_;

  trampoline->Bounce(
      a_shared.get(),
      [a_shared, b_shared, type_copy, operator_copy](
          std::unique_ptr<Value> a_value, Trampoline* trampoline) {
        std::shared_ptr<Value> a_value_shared(std::move(a_value));
        trampoline->Bounce(
            b_shared.get(),
            [a_value_shared, b_shared, type_copy, operator_copy](
                std::unique_ptr<Value> b_value, Trampoline* trampoline) {
              auto output = std::make_unique<Value>(type_copy);
              operator_copy(*a_value_shared, *b_value, output.get());
              trampoline->Continue(std::move(output));
            });
      });
}

std::unique_ptr<Expression> BinaryOperator::Clone() {
  return std::make_unique<BinaryOperator>(a_->Clone(), b_->Clone(), type_,
                                          operator_);
}

std::unique_ptr<Expression> NewAdditionExpression(
    Compilation* compilation, std::unique_ptr<Expression> a,
    std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return nullptr;
  }

  if (a->type().type == VMType::VM_STRING &&
      b->type().type == VMType::VM_STRING) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), VMType::String(),
        [](const Value& value_a, const Value& value_b, Value* output) {
          output->str = value_a.str + value_b.str;
        });
  }

  if (a->type().type == VMType::VM_INTEGER &&
      b->type().type == VMType::VM_INTEGER) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), VMType::Integer(),
        [](const Value& value_a, const Value& value_b, Value* output) {
          output->integer = value_a.integer + value_b.integer;
        });
  }

  if ((a->type().type == VMType::VM_INTEGER ||
       a->type().type == VMType::VM_DOUBLE) &&
      (b->type().type == VMType::VM_INTEGER ||
       b->type().type == VMType::VM_DOUBLE)) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), VMType::Double(),
        [](const Value& a, const Value& b, Value* output) {
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              CHECK(false) << "Unexpected type: " << x.type;
            }
          };
          output->double_value = to_double(a) + to_double(b);
        });
  }

  compilation->errors.push_back(L"Unable to add types: \"" +
                                a->type().ToString() + L"\" + \"" +
                                b->type().ToString() + L"\"");
  return nullptr;
}

}  // namespace vm
}  // namespace afc

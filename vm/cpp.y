%name Cpp

%extra_argument { Environment* environment }

%token_type { Value* }

%left EQ.
%left PLUS MINUS.
%left DIVIDE TIMES.
%left LPAREN RPAREN.

%type main { Value* }
%destructor main { delete $$; }

main(A) ::= program(B) . {
  if (B == nullptr) {
    A = nullptr;
  } else {
    A = B;
    B = nullptr;
  }
}

%type program { Value* }
%destructor program { delete $$; }

program(V) ::= statement(S). {
  if (S == nullptr) {
    V = nullptr;
  } else {
    V = S->Evaluate(environment).release();
  }
}

program(A) ::= program(B) statement(C) . {
  if (B == nullptr || C == nullptr) {
    A = nullptr;
  } else {
    A = C->Evaluate(environment).release();
  }
}

%type statement { Expression* }
%destructor statement { delete $$; }

statement(A) ::= expr(B) SEMICOLON . {
  A = B;
  B = nullptr;
}

statement(A) ::= SEMICOLON . {
  A = new ConstantExpression(Value::Void());
}

statement(A) ::= LBRACKET statement_list(L) RBRACKET. {
  A = L;
  L = nullptr;
}

statement(A) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE) ELSE statement(FALSE_CASE). {
  class IfEvaluator : public Expression {
   public:
    IfEvaluator(unique_ptr<Expression> cond, unique_ptr<Expression> true_case,
                unique_ptr<Expression> false_case)
        : cond_(std::move(cond)),
          true_case_(std::move(true_case)),
          false_case_(std::move(false_case)) {
      assert(cond_ != nullptr);
      assert(true_case_ != nullptr);
    }

    const VMType& type() {
      return true_case_->type();
    }

    unique_ptr<Value> Evaluate(Environment* environment) {
      auto cond = std::move(cond_->Evaluate(environment));
      assert(cond->type.type == VMType::VM_BOOLEAN);
      return (cond->boolean ? true_case_ : false_case_)->Evaluate(environment);
    }

   private:
    unique_ptr<Expression> cond_;
    unique_ptr<Expression> true_case_;
    unique_ptr<Expression> false_case_;
  };

  if (CONDITION == nullptr || TRUE_CASE == nullptr || FALSE_CASE == nullptr
      || CONDITION->type().type != VMType::VM_BOOLEAN
      || !(TRUE_CASE->type() == FALSE_CASE->type())) {
    A = nullptr;
  } else {
    A = new IfEvaluator(unique_ptr<Expression>(CONDITION),
                        unique_ptr<Expression>(TRUE_CASE),
                        unique_ptr<Expression>(FALSE_CASE));
    CONDITION = nullptr;
    TRUE_CASE = nullptr;
    FALSE_CASE = nullptr;
  }
}

statement(A) ::= STRING_SYMBOL SYMBOL(N) EQ expr(V) SEMICOLON. {
  class AssignExpression : public Expression {
   public:
    AssignExpression(const string& symbol, unique_ptr<Expression> value)
        : symbol_(symbol), value_(std::move(value)) {}
    const VMType& type() { return VMType::Void(); }
    unique_ptr<Value> Evaluate(Environment* environment) {
      auto value = value_->Evaluate(environment);
      environment->Define(symbol_, std::move(value));
      return Value::Void();
    }
   private:
    const string symbol_;
    unique_ptr<Expression> value_;
  };

  assert(N->type.type == VMType::VM_SYMBOL);
  environment->Define(N->str, unique_ptr<Value>(new Value(V->type().type)));
  A = new AssignExpression(N->str, unique_ptr<Expression>(V));
  V = nullptr;
}

// Statement list.

%type statement_list { Expression * }
%destructor statement_list { delete $$; }

statement_list(L) ::= statement(A). {
  L = A;
  A = nullptr;
}

statement_list(OUT) ::= statement_list(L) statement(A). {
  class AppendExpression : public Expression {
   public:
    AppendExpression(unique_ptr<Expression> e0, unique_ptr<Expression> e1)
        : e0_(std::move(e0)), e1_(std::move(e1)) {}

    const VMType& type() { return e1_->type(); }

    unique_ptr<Value> Evaluate(Environment* environment) {
      e0_->Evaluate(environment);
      return std::move(e1_->Evaluate(environment));
    }

   private:
    unique_ptr<Expression> e0_;
    unique_ptr<Expression> e1_;
  };

  if (L == nullptr || A == nullptr) {
    OUT = nullptr;
  } else {
    OUT = new AppendExpression(unique_ptr<Expression>(L),
                               unique_ptr<Expression>(A));
    L = nullptr;
    A = nullptr;
  }
}


// Expressions

%type expr { Expression* }
%destructor expr { delete $$; }

expr(A) ::= LPAREN expr(B) RPAREN. {
  A = B;
  B = nullptr;
}

expr(OUT) ::= expr(B) LPAREN arguments_list(ARGS) RPAREN. {
  class FunctionCall : public Expression {
   public:
    FunctionCall(unique_ptr<Expression> func,
                 vector<unique_ptr<Expression>>* args)
        : func_(std::move(func)), args_(args) {
      assert(func_ != nullptr);
      assert(args_ != nullptr);
    }

    const VMType& type() {
      return func_->type().type_arguments[0];
    }

    unique_ptr<Value> Evaluate(Environment* environment) {
      auto func = std::move(func_->Evaluate(environment));
      vector<unique_ptr<Value>> values;
      for (const auto& arg : *args_) {
        values.push_back(arg->Evaluate(environment));
      }
      return std::move(func->callback(std::move(values)));
    }

   private:
    unique_ptr<Expression> func_;
    unique_ptr<vector<unique_ptr<Expression>>> args_;
  };

  if (B == nullptr || ARGS == nullptr
      || B->type().type != VMType::FUNCTION
      || B->type().type_arguments.size() != 1 + ARGS->size()) {
    OUT = nullptr;
  } else {
    bool match = true;
    for (size_t i = 0; i < ARGS->size(); i++) {
      assert(1 + i < B->type().type_arguments.size());
      if (!(B->type().type_arguments[1 + i] == ARGS->at(i)->type())) {
        match = false;
        break;
      }
    }
    if (!match) {
      OUT = nullptr;
    } else {
      OUT = new FunctionCall(unique_ptr<Expression>(B), std::move(ARGS));
      B = nullptr;
      ARGS = nullptr;
    }
  }
}


// Arguments list

%type arguments_list { vector<unique_ptr<Expression>>* }
%destructor arguments_list { delete $$; }

arguments_list(OUT) ::= . {
  OUT = new vector<unique_ptr<Expression>>;
}

arguments_list(OUT) ::= non_empty_arguments_list(L). {
  OUT = L;
  L = nullptr;
}

%type non_empty_arguments_list { vector<unique_ptr<Expression>>* }
%destructor non_empty_arguments_list { delete $$; }

non_empty_arguments_list(OUT) ::= expr(E). {
  if (E == nullptr) {
    OUT = nullptr;
  } else {
    OUT = new vector<unique_ptr<Expression>>;
    OUT->push_back(unique_ptr<Expression>(E));
    E = nullptr;
  }
}

non_empty_arguments_list(OUT) ::= non_empty_arguments_list(L) COMMA expr(E). {
  OUT = L;
  OUT->push_back(unique_ptr<Expression>(E));
  L = nullptr;
  E = nullptr;
}


// Basic operators

expr(OUT) ::= expr(A) EQ EQ expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if (A->type().type == VMType::VM_STRING) {
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.str == b.str;
        });
    A = nullptr;
    B = nullptr;
  } else if (B->type().type == VMType::VM_INTEGER) {
    A = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.integer == b.integer;
        });
    A = nullptr;
    B = nullptr;
  } else {
    OUT = nullptr;
  }
}
%endif

expr(A) ::= expr(B) PLUS expr(C). {
  if (B == nullptr || C == nullptr || !(B->type() == C->type())) {
    A = nullptr;
  } else if (B->type().type == VMType::VM_STRING) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::String(),
        [](const Value& a, const Value& b, Value* output) {
          output->str = a.str + b.str;
        });
    B = nullptr;
    C = nullptr;
  } else if (B->type().type == VMType::VM_INTEGER) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::integer_type(),
        [](const Value& a, const Value& b, Value* output) {
          output->integer = a.integer + b.integer;
        });
    B = nullptr;
    C = nullptr;
  } else {
    A = nullptr;
  }
}

expr(A) ::= expr(B) MINUS expr(C). {
  A = new BinaryOperator(
      unique_ptr<Expression>(B),
      unique_ptr<Expression>(C),
      VMType::integer_type(),
      [](const Value& a, const Value& b, Value* output) {
        output->integer = a.integer - b.integer;
      });
  B = nullptr;
  C = nullptr;
}

expr(A) ::= expr(B) TIMES expr(C). {
  if (B == nullptr || C == nullptr || !(B->type() == C->type())) {
    A = nullptr;
  } else if (B->type().type == VMType::VM_INTEGER) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::integer_type(),
        [](const Value& a, const Value& b, Value* output) {
          output->integer = a.integer * b.integer;
        });
    B = nullptr;
    C = nullptr;
  } else {
    A = nullptr;
  }
}

//expr(A) ::= expr(B) DIVIDE expr(C). {
//  A = new Value(VMType::VM_INTEGER);
//  if (C->integer != 0) {
//    A->integer = B->integer / C->integer;
//  } else {
//    std::cout << "divide by zero" << std::endl;
//  }
//}  /* end of DIVIDE */


// Atomic types

expr(A) ::= INTEGER(B). {
  assert(B->type.type == VMType::VM_INTEGER);
  A = new ConstantExpression(unique_ptr<Value>(B));
  B = nullptr;
}

%type string { Value* }
%destructor string { delete $$; }

expr(A) ::= string(B). {
  assert(B->type.type == VMType::VM_STRING);
  A = new ConstantExpression(unique_ptr<Value>(B));
  B = nullptr;
}

string(O) ::= STRING(A). {
  assert(A->type.type == VMType::VM_STRING);
  O = A;
  A = nullptr;
}

string(O) ::= string(A) STRING(B). {
  assert(A->type.type == VMType::VM_STRING);
  assert(B->type.type == VMType::VM_STRING);
  O = A;
  O->str = A->str + B->str;
  A = nullptr;
}

expr(A) ::= SYMBOL(B). {
  assert(B->type.type == VMType::VM_SYMBOL);
  class VariableLookup : public Expression {
   public:
    VariableLookup(const string& symbol, const VMType& type)
        : symbol_(symbol), type_(type) {}
    const VMType& type() {
      return type_;
    }
    unique_ptr<Value> Evaluate(Environment* environment) {
      Value* value = environment->Lookup(symbol_);
      unique_ptr<Value> output = Value::Void();
      *output = *value;
      return std::move(output);
    }
   private:
    const string symbol_;
    const VMType type_;
  };

  Value* result = environment->Lookup(B->str);
  if (result != nullptr) {
    A = new VariableLookup(B->str, result->type);
  } else {
    A = nullptr;
  }
}

expr(A) ::= BOOL(B). {
  assert(B->type.type == VMType::VM_BOOLEAN);
  A = new ConstantExpression(unique_ptr<Value>(B));
  B = nullptr;
}

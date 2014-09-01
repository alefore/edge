%name Cpp

%extra_argument { Environment* environment }

%token_type { Value* }

%left COMMA.
%left EQ.
%left OR.
%left AND.
%left EQUALS.
%left LESS_THAN GREATER_THAN.
%left PLUS MINUS.
%left DIVIDE TIMES.
%left LPAREN RPAREN DOT.

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

statement(A) ::= error. {
  A = new ConstantExpression(Value::Void());
}

statement(OUT) ::= function_declaration_params(FUNC)
    LBRACKET statement_list(BODY) RBRACKET. {
  if (FUNC == nullptr || BODY == nullptr
      || !(FUNC->second->type.type_arguments[0] == BODY->type())) {
    OUT = nullptr;
  } else {
    Expression* body = BODY;
    BODY = nullptr;

    FUNC->second->callback = [body, environment](vector<unique_ptr<Value>> args) {
          assert(args.size() == 0);
          // TODO: Don't leak environment nor body.
          return body->Evaluate(environment);
        };
    environment->Define(FUNC->first, unique_ptr<Value>(FUNC->second));
    FUNC = nullptr;

    environment = environment->parent_environment();
    OUT = new ConstantExpression(Value::Void());
  }
}

%type function_declaration_params { pair<string, Value*>* }

function_declaration_params(OUT) ::= SYMBOL(RETURN_TYPE) SYMBOL(NAME) LPAREN RPAREN . {
  assert(RETURN_TYPE->type == VMType::VM_SYMBOL);
  assert(NAME->type == VMType::VM_SYMBOL);

  const VMType* return_type_def = environment->LookupType(RETURN_TYPE->str);
  if (return_type_def == nullptr) {
    OUT = nullptr;
  } else {
    OUT = new pair<string, Value*>(NAME->str, new Value(VMType::FUNCTION));
    OUT->second->type.type_arguments.push_back(*return_type_def);
    environment->Define(NAME->str, unique_ptr<Value>(new Value(*return_type_def)));
    environment = new Environment(environment);
  }
}

statement(A) ::= SEMICOLON . {
  A = new ConstantExpression(Value::Void());
}

statement(A) ::= LBRACKET statement_list(L) RBRACKET. {
  A = L;
  L = nullptr;
}

statement(OUT) ::= WHILE LPAREN expr(CONDITION) RPAREN statement(BODY). {
  class WhileEvaluator : public Expression {
   public:
    WhileEvaluator(unique_ptr<Expression> cond, unique_ptr<Expression> body)
        : cond_(std::move(cond)), body_(std::move(body)) {
      assert(cond_ != nullptr);
      assert(body_ != nullptr);
    }

    const VMType& type() {
      return VMType::Void();
    }

    unique_ptr<Value> Evaluate(Environment* environment) {
      while (cond_->Evaluate(environment)->boolean) {
        body_->Evaluate(environment);
      }
      return Value::Void();
    }

   private:
    unique_ptr<Expression> cond_;
    unique_ptr<Expression> body_;
  };

  if (CONDITION == nullptr || BODY == nullptr
      || CONDITION->type().type != VMType::VM_BOOLEAN) {
    OUT = nullptr;
  } else {
    OUT = new WhileEvaluator(unique_ptr<Expression>(CONDITION),
                             unique_ptr<Expression>(BODY));
    CONDITION = nullptr;
    BODY = nullptr;
  }
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
      assert(false_case_ != nullptr);
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

statement(A) ::= SYMBOL(TYPE) SYMBOL(NAME) EQ expr(VALUE) SEMICOLON. {
  assert(TYPE->type.type == VMType::VM_SYMBOL);
  assert(NAME->type.type == VMType::VM_SYMBOL);

  if (VALUE == nullptr) {
    A = nullptr;
  } else {
    const VMType* type_def = environment->LookupType(TYPE->str);
    if (type_def == nullptr || !(*type_def == VALUE->type())) {
      A = nullptr;
    } else {
      environment->Define(NAME->str, unique_ptr<Value>(new Value(VALUE->type())));
      A = new AssignExpression(NAME->str, unique_ptr<Expression>(VALUE));
      VALUE = nullptr;
    }
  }
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

expr(OUT) ::= SYMBOL(NAME) EQ expr(VALUE). {
  assert(NAME->type.type == VMType::VM_SYMBOL);

  if (VALUE == nullptr) {
    OUT = nullptr;
  } else {
    auto obj = environment->Lookup(NAME->str);
    if (obj == nullptr || !(obj->type == VALUE->type())) {
      OUT = nullptr;
    } else {
      OUT = new AssignExpression(NAME->str, unique_ptr<Expression>(VALUE));
      VALUE = nullptr;
    }
  }
}

expr(OUT) ::= expr(OBJ) DOT SYMBOL(FIELD) LPAREN arguments_list(ARGS) RPAREN. {
  if (OBJ == nullptr || ARGS == nullptr
      || (OBJ->type().type != VMType::OBJECT_TYPE
          && OBJ->type().type != VMType::VM_STRING)) {
    OUT = nullptr;
  } else {
    string object_type_name;
    switch (OBJ->type().type) {
      case VMType::VM_STRING:
        object_type_name = "string";
        break;
      case VMType::OBJECT_TYPE:
        object_type_name = OBJ->type().object_type;
        break;
      default:
        assert(false);
    }
    const ObjectType* object_type =
        environment->LookupObjectType(object_type_name);
    if (object_type == nullptr) {
      OUT = nullptr;
    } else {
      auto field = object_type->LookupField(FIELD->str);
      if (field == nullptr) {
        OUT = nullptr;
      } else if (field->type.type_arguments.size() != 2 + ARGS->size()) {
        OUT = nullptr;
      } else {
        assert(field->type.type_arguments[1] == OBJ->type());
        bool match = true;
        for (size_t i = 0; i < ARGS->size(); i++) {
          assert(2 + i < field->type.type_arguments.size());
          if (!(field->type.type_arguments[2 + i] == ARGS->at(i)->type())) {
            match = false;
            break;
          }
        }
        if (!match) {
          OUT = nullptr;
        } else {
          unique_ptr<Value> field_copy(new Value(field->type.type));
          *field_copy = *field;
          OUT = new FunctionCall(
              unique_ptr<Expression>(new ConstantExpression(std::move(field_copy))),
              unique_ptr<Expression>(OBJ),
              std::move(ARGS));
          OBJ = nullptr;
          ARGS = nullptr;
        }
      }
    }
  }
}

expr(OUT) ::= expr(B) LPAREN arguments_list(ARGS) RPAREN. {
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
  if (L == nullptr || E == nullptr) {
    OUT = nullptr;
  } else {
    OUT = L;
    OUT->push_back(unique_ptr<Expression>(E));
    L = nullptr;
    E = nullptr;
  }
}


// Basic operators

expr(OUT) ::= expr(A) EQUALS expr(B). {
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

expr(OUT) ::= expr(A) LESS_THAN expr(B). {
  if (A == nullptr
      || B == nullptr
      || A->type().type != VMType::VM_INTEGER
      || B->type().type != VMType::VM_INTEGER) {
    OUT = nullptr;
  } else {
    // TODO: Don't evaluate B if not needed.
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.integer < b.integer;
        });
    A = nullptr;
    B = nullptr;
  }
}

expr(OUT) ::= expr(A) GREATER_THAN expr(B). {
  if (A == nullptr
      || B == nullptr
      || A->type().type != VMType::VM_INTEGER
      || B->type().type != VMType::VM_INTEGER) {
    OUT = nullptr;
  } else {
    // TODO: Don't evaluate B if not needed.
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.integer > b.integer;
        });
    A = nullptr;
    B = nullptr;
  }
}

expr(OUT) ::= expr(A) OR expr(B). {
  if (A == nullptr
      || B == nullptr
      || A->type().type != VMType::VM_BOOLEAN
      || B->type().type != VMType::VM_BOOLEAN) {
    OUT = nullptr;
  } else {
    // TODO: Don't evaluate B if not needed.
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.boolean || b.boolean;
        });
    A = nullptr;
    B = nullptr;
  }
}

expr(OUT) ::= expr(A) AND expr(B). {
  if (A == nullptr
      || B == nullptr
      || A->type().type != VMType::VM_BOOLEAN
      || B->type().type != VMType::VM_BOOLEAN) {
    OUT = nullptr;
  } else {
    // TODO: Don't evaluate B if not needed.
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.boolean && b.boolean;
        });
    A = nullptr;
    B = nullptr;
  }
}

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

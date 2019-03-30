%name Cpp

%extra_argument { Compilation* compilation }

%token_type { Value* }

%left COMMA.
%left QUESTION_MARK.
%left EQ.
%left OR.
%left AND.
%left EQUALS NOT_EQUALS.
%left LESS_THAN LESS_OR_EQUAL GREATER_THAN GREATER_OR_EQUAL.
%left PLUS MINUS.
%left DIVIDE TIMES.
%right NOT.
%left LPAREN RPAREN DOT.
%left ELSE.

main ::= program(P) . {
  compilation->expr.reset(P);
}

main ::= error. {
  compilation->errors.push_back(
      L"Compilation error near: \"" + compilation->last_token + L"\"");
}

%type program { Expression* }
%destructor program { delete $$; }

program(OUT) ::= statement_list(A). {
  OUT = A;
  A = nullptr;
}

%type statement { Expression* }
%destructor statement { delete $$; }

statement(A) ::= expr(B) SEMICOLON . {
  A = B;
  B = nullptr;
}

statement(OUT) ::= RETURN expr(A) SEMICOLON . {
  OUT = NewReturnExpression(compilation, unique_ptr<Expression>(A)).release();
  A = nullptr;
}

statement(OUT) ::= RETURN SEMICOLON . {
  OUT = NewReturnExpression(compilation, NewVoidExpression()).release();
}

statement(OUT) ::= function_declaration_params(FUNC)
    LBRACKET statement_list(BODY) RBRACKET. {
  if (FUNC == nullptr || BODY == nullptr) {
    OUT = nullptr;
  } else {
    // TODO: Use unique_ptr rather than shared_ptr when lambda capture works.
    std::shared_ptr<Expression> body(BODY);
    BODY = nullptr;

    shared_ptr<Environment> func_environment(compilation->environment);
    compilation->environment = compilation->environment->parent_environment();
    compilation->return_types.pop_back();

    const vector<wstring> argument_names(FUNC->argument_names);

    unique_ptr<Value> value(new Value(FUNC->type));
    auto name = FUNC->name;
    value->callback = [compilation, name, body, func_environment, argument_names](
        vector<unique_ptr<Value>> args, Trampoline* trampoline) {
      CHECK_EQ(args.size(), argument_names.size())
          << "Invalid number of arguments for function: " << name;
      for (size_t i = 0; i < args.size(); i++) {
        func_environment->Define(argument_names[i], std::move(args[i]));
      }
      std::function<void(Trampoline*)> original_state = trampoline->Save();
      trampoline->SetEnvironment(func_environment.get());
      trampoline->SetReturnContinuation(
          [original_state](std::unique_ptr<Value> value,
                           Trampoline* trampoline) {
            CHECK(value != nullptr);
            original_state(trampoline);
            trampoline->Return(std::move(value));
          });
      trampoline->Bounce(
          body.get(),
          [body](Value::Ptr value, Trampoline* trampoline) {
            trampoline->Return(std::move(value));
          });
    };
    compilation->environment->Define(FUNC->name, std::move(value));
    OUT = NewVoidExpression().release();
  }
}

%type function_declaration_params { UserFunction* }
%destructor function_declaration_params { delete $$; }

function_declaration_params(OUT) ::= SYMBOL(RETURN_TYPE) SYMBOL(NAME) LPAREN
    function_declaration_arguments(ARGS) RPAREN . {
  assert(RETURN_TYPE->type == VMType::VM_SYMBOL);
  assert(NAME->type == VMType::VM_SYMBOL);

  if (ARGS == nullptr) {
    OUT = nullptr;
  } else {
    const VMType* return_type_def =
        compilation->environment->LookupType(RETURN_TYPE->str);
    if (return_type_def == nullptr) {
      compilation->errors.push_back(
          L"Unknown return type: \"" + RETURN_TYPE->str + L"\"");
      OUT = nullptr;
    } else {
      OUT = new UserFunction();
      OUT->name = NAME->str;
      OUT->type.type = VMType::FUNCTION;
      OUT->type.type_arguments.push_back(*return_type_def);
      for (pair<VMType, wstring> arg : *ARGS) {
        OUT->type.type_arguments.push_back(arg.first);
        OUT->argument_names.push_back(arg.second);
      }
      compilation->environment->Define(
          NAME->str, unique_ptr<Value>(new Value(OUT->type)));
      compilation->environment = new Environment(compilation->environment);
      compilation->return_types.push_back(*return_type_def);
      for (pair<VMType, wstring> arg : *ARGS) {
        compilation->environment
            ->Define(arg.second, unique_ptr<Value>(new Value(arg.first)));
      }
    }
  }
  delete RETURN_TYPE;
  delete NAME;
}

statement(A) ::= SEMICOLON . {
  A = NewVoidExpression().release();
}

statement(A) ::= LBRACKET statement_list(L) RBRACKET. {
  A = L;
  L = nullptr;
}

statement(OUT) ::= WHILE LPAREN expr(CONDITION) RPAREN statement(BODY). {
  OUT = NewWhileExpression(compilation, unique_ptr<Expression>(CONDITION),
                           unique_ptr<Expression>(BODY)).release();
  CONDITION = nullptr;
  BODY = nullptr;
}

statement(A) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE)
    ELSE statement(FALSE_CASE). {
  A = NewIfExpression(
      compilation,
      unique_ptr<Expression>(CONDITION),
      NewAppendExpression(
          unique_ptr<Expression>(TRUE_CASE),
          NewVoidExpression()),
      NewAppendExpression(
          unique_ptr<Expression>(FALSE_CASE),
          NewVoidExpression()))
      .release();
  CONDITION = nullptr;
  TRUE_CASE = nullptr;
  FALSE_CASE = nullptr;
}

statement(A) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE). {
  A = NewIfExpression(
      compilation,
      unique_ptr<Expression>(CONDITION),
      NewAppendExpression(
          unique_ptr<Expression>(TRUE_CASE),
          NewVoidExpression()),
      NewVoidExpression())
      .release();
  CONDITION = nullptr;
  TRUE_CASE = nullptr;
}

statement(A) ::= SYMBOL(TYPE) SYMBOL(NAME) EQ expr(VALUE) SEMICOLON. {
  A = NewAssignExpression(compilation, TYPE->str, NAME->str,
                          unique_ptr<Expression>(VALUE)).release();
  delete TYPE;
  delete NAME;
  VALUE = nullptr;
}

// Statement list.

%type statement_list { Expression * }
%destructor statement_list { delete $$; }

statement_list(L) ::= statement(A). {
  L = A;
  A = nullptr;
}

statement_list(OUT) ::= statement_list(A) statement(B). {
  OUT = NewAppendExpression(unique_ptr<Expression>(A),
                            unique_ptr<Expression>(B)).release();
  A = nullptr;
  B = nullptr;
}

// Arguments in the declaration of a function

%type function_declaration_arguments { vector<pair<VMType, wstring>>* }
%destructor function_declaration_arguments { delete $$; }

function_declaration_arguments(OUT) ::= . {
  OUT = new vector<pair<VMType, wstring>>;
}

function_declaration_arguments(OUT) ::= non_empty_function_declaration_arguments(L). {
  OUT = L;
  L = nullptr;
}

%type non_empty_function_declaration_arguments {
  vector<pair<VMType, wstring>>*
}
%destructor non_empty_function_declaration_arguments { delete $$; }

non_empty_function_declaration_arguments(OUT) ::= SYMBOL(TYPE) SYMBOL(NAME). {
  const VMType* type_def = compilation->environment->LookupType(TYPE->str);
  if (type_def == nullptr) {
    compilation->errors.push_back(L"Unknown type: \"" + TYPE->str + L"\"");
    OUT = nullptr;
  } else {
    OUT = new vector<pair<VMType, wstring>>;
    OUT->push_back(make_pair(*type_def, NAME->str));
  }
  delete TYPE;
  delete NAME;
}

non_empty_function_declaration_arguments(OUT) ::=
    non_empty_function_declaration_arguments(LIST) COMMA SYMBOL(TYPE) SYMBOL(NAME). {
  if (LIST == nullptr) {
    OUT = nullptr;
  } else {
    const VMType* type_def = compilation->environment->LookupType(TYPE->str);
    if (type_def == nullptr) {
      compilation->errors.push_back(L"Unknown type: \"" + TYPE->str + L"\"");
      OUT = nullptr;
    } else {
      OUT = LIST;
      OUT->push_back(make_pair(*type_def, NAME->str));
      LIST = nullptr;
    }
  }
  delete TYPE;
  delete NAME;
}


// Expressions

%type expr { Expression* }
%destructor expr { delete $$; }

expr(A) ::= expr(CONDITION) QUESTION_MARK
    expr(TRUE_CASE) COLON expr(FALSE_CASE). {
  A = NewIfExpression(
      compilation, unique_ptr<Expression>(CONDITION),
      unique_ptr<Expression>(TRUE_CASE), unique_ptr<Expression>(FALSE_CASE))
          .release();
  CONDITION = nullptr;
  TRUE_CASE = nullptr;
  FALSE_CASE = nullptr;
}

expr(A) ::= LPAREN expr(B) RPAREN. {
  A = B;
  B = nullptr;
}

expr(OUT) ::= SYMBOL(NAME) EQ expr(VALUE). {
  OUT = NewAssignExpression(compilation, NAME->str,
                            unique_ptr<Expression>(VALUE)).release();
  VALUE = nullptr;
  delete NAME;
}

expr(OUT) ::= expr(OBJ) DOT SYMBOL(FIELD) LPAREN arguments_list(ARGS) RPAREN. {
  if (OBJ == nullptr || ARGS == nullptr) {
    OUT = nullptr;
  } else {
    wstring object_type_name;
    switch (OBJ->type().type) {
      case VMType::VM_STRING:
        object_type_name = L"string";
        break;
      case VMType::VM_BOOLEAN:
        object_type_name = L"bool";
        break;
      case VMType::VM_DOUBLE:
        object_type_name = L"double";
        break;
      case VMType::VM_INTEGER:
        object_type_name = L"int";
        break;
      case VMType::OBJECT_TYPE:
        object_type_name = OBJ->type().object_type;
        break;
      default:
        break;
    }
    const ObjectType* object_type = object_type_name.empty()
        ? nullptr
        : compilation->environment->LookupObjectType(object_type_name);
    if (object_type_name.empty()) {
      compilation->errors.push_back(
          L"Unable to call methods on primitive type: \""
          + OBJ->type().ToString() + L"\"");
      OUT = nullptr;
    } else if (object_type == nullptr) {
      compilation->errors.push_back(
          L"Unknown type: \"" + OBJ->type().ToString() + L"\"");
      OUT = nullptr;
    } else {
      auto field = object_type->LookupField(FIELD->str);
      if (field == nullptr) {
        compilation->errors.push_back(
            L"Unknown method: \"" + object_type->ToString() + L"::"
            + FIELD->str + L"\"");
        OUT = nullptr;
      } else if (field->type.type_arguments.size() != 2 + ARGS->size()) {
        compilation->errors.push_back(
            L"Invalid number of arguments provided for method \""
            + object_type->ToString() + L"::" + FIELD->str + L"\": Expected "
            + to_wstring(field->type.type_arguments.size() - 2) + L" but found "
            + to_wstring(ARGS->size()));
        OUT = nullptr;
      } else {
        assert(field->type.type_arguments[1] == OBJ->type());
        size_t argument = 0;
        while (argument < ARGS->size()
               && (field->type.type_arguments[2 + argument]
                   == ARGS->at(argument)->type())) {
          argument++;
        }
        if (argument < ARGS->size()) {
          compilation->errors.push_back(
              L"Type mismatch in argument " + to_wstring(argument)
              + L" to method \"" + object_type->ToString() + L"::" + FIELD->str
              + L"\": Expected \""
              + field->type.type_arguments[2 + argument].ToString()
              + L"\" but found \"" + ARGS->at(argument)->type().ToString()
              + L"\"");
          OUT = nullptr;
        } else {
          unique_ptr<Value> field_copy(new Value(field->type.type));
          *field_copy = *field;
          std::vector<std::unique_ptr<Expression>> args;
          args.emplace_back(OBJ);
          OBJ = nullptr;
          for (auto& arg : *ARGS) {
            args.push_back(std::move(arg));
          }
          CHECK(field_copy != nullptr);
          OUT = NewFunctionCall(NewConstantExpression(std::move(field_copy)),
                                std::move(args)).release();
        }
      }
    }
  }
  delete FIELD;
  delete ARGS;
}

expr(OUT) ::= expr(B) LPAREN arguments_list(ARGS) RPAREN. {
  if (B == nullptr || ARGS == nullptr) {
    OUT = nullptr;
  } else if (B->type().type != VMType::FUNCTION) {
    compilation->errors.push_back(
        L"Expected function but found: \"" + B->type().ToString() + L"\"");
    OUT = nullptr;
  } else if (B->type().type_arguments.size() != 1 + ARGS->size()) {
    compilation->errors.push_back(
        L"Invalid number of arguments: Expected "
        + to_wstring(B->type().type_arguments.size() - 1) + L" but found "
        + to_wstring(ARGS->size()));
    OUT = nullptr;
  } else {
    size_t argument = 0;
    while (argument < ARGS->size()
           && (B->type().type_arguments[1 + argument]
               == ARGS->at(argument)->type())) {
      argument++;
    }
    if (argument < ARGS->size()) {
      compilation->errors.push_back(
          L"Type mismatch in argument " + to_wstring(argument)
          + L": Expected \"" + B->type().type_arguments[1 + argument].ToString()
          + L"\" but found \"" + ARGS->at(argument)->type().ToString() + L"\"");
      OUT = nullptr;
    } else {
      OUT = NewFunctionCall(
                    std::unique_ptr<Expression>(B),
                    std::move(*ARGS))
                .release();
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

expr(OUT) ::= NOT expr(A). {
  OUT = NewNegateExpression(
      [](Value* value) { value->boolean = !value->boolean; },
      VMType::Bool(),
      compilation, unique_ptr<Expression>(A)).release();
  A = nullptr;
}

expr(OUT) ::= expr(A) EQUALS expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if (A->type().type == VMType::VM_STRING
             && B->type().type == VMType::VM_STRING) {
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.str == b.str;
        });
    A = nullptr;
    B = nullptr;
  } else if (A->type().type == VMType::VM_INTEGER
             && B->type().type == VMType::VM_INTEGER) {
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.integer == b.integer;
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: \"" + A->type().ToString()
        + L"\" == \"" + B->type().ToString() + L"\"");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) NOT_EQUALS expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if (A->type().type == VMType::VM_STRING
             && B->type().type == VMType::VM_STRING) {
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.str != b.str;
        });
    A = nullptr;
    B = nullptr;
  } else if (A->type().type == VMType::VM_INTEGER
             && B->type().type == VMType::VM_INTEGER) {
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.integer != b.integer;
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: \"" + A->type().ToString()
        + L"\" != \"" + B->type().ToString() + L"\"");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) LESS_THAN expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if ((A->type().type == VMType::VM_INTEGER
              || A->type().type == VMType::VM_DOUBLE)
             && (B->type().type == VMType::VM_INTEGER
                 || B->type().type == VMType::VM_DOUBLE)) {
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          if (a.type.type == VMType::VM_INTEGER && b.type.type == VMType::VM_INTEGER) {
            output->boolean = a.integer < b.integer;
            return;
          }
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              LOG(FATAL) << "Unexpected value of type: " << x.type.ToString();
            }
          };
          output->boolean = to_double(a) < to_double(b);
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: \"" + A->type().ToString()
        + L"\" < \"" + B->type().ToString() + L"\"");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) LESS_OR_EQUAL expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if ((A->type().type == VMType::VM_INTEGER
              || A->type().type == VMType::VM_DOUBLE)
             && (B->type().type == VMType::VM_INTEGER
                 || B->type().type == VMType::VM_DOUBLE)) {
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          if (a.type.type == VMType::VM_INTEGER && b.type.type == VMType::VM_INTEGER) {
            output->boolean = a.integer <= b.integer;
            return;
          }
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              LOG(FATAL) << "Unexpected value of type: " << x.type.ToString();
            }
          };
          output->boolean = to_double(a) <= to_double(b);
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: \"" + A->type().ToString()
        + L"\" < \"" + B->type().ToString() + L"\"");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) GREATER_THAN expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if ((A->type().type == VMType::VM_INTEGER
              || A->type().type == VMType::VM_DOUBLE)
             && (B->type().type == VMType::VM_INTEGER
                 || B->type().type == VMType::VM_DOUBLE)) {
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          if (a.type.type == VMType::VM_INTEGER && b.type.type == VMType::VM_INTEGER) {
            output->boolean = a.integer > b.integer;
            return;
          }
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              LOG(FATAL) << "Unexpected value of type: " << x.type.ToString();
            }
          };
          output->boolean = to_double(a) > to_double(b);
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: \"" + A->type().ToString()
        + L"\" < \"" + B->type().ToString() + L"\"");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) GREATER_OR_EQUAL expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if ((A->type().type == VMType::VM_INTEGER
              || A->type().type == VMType::VM_DOUBLE)
             && (B->type().type == VMType::VM_INTEGER
                 || B->type().type == VMType::VM_DOUBLE)) {
    OUT = new BinaryOperator(
        unique_ptr<Expression>(A),
        unique_ptr<Expression>(B),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          if (a.type.type == VMType::VM_INTEGER && b.type.type == VMType::VM_INTEGER) {
            output->boolean = a.integer >= b.integer;
            return;
          }
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              LOG(FATAL) << "Unexpected value of type: " << x.type.ToString();
            }
          };
          output->boolean = to_double(a) >= to_double(b);
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: \"" + A->type().ToString()
        + L"\" < \"" + B->type().ToString() + L"\"");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) OR expr(B). {
  OUT = NewLogicalExpression(
      false, unique_ptr<Expression>(A), unique_ptr<Expression>(B)).release();
  A = nullptr;
  B = nullptr;
}

expr(OUT) ::= expr(A) AND expr(B). {
  OUT = NewLogicalExpression(
      true, unique_ptr<Expression>(A), unique_ptr<Expression>(B)).release();
  A = nullptr;
  B = nullptr;
}

expr(A) ::= expr(B) PLUS expr(C). {
  if (B == nullptr || C == nullptr) {
    A = nullptr;
  } else if (B->type().type == VMType::VM_STRING && C->type().type == VMType::VM_STRING) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::String(),
        [](const Value& a, const Value& b, Value* output) {
          output->str = a.str + b.str;
        });
    B = nullptr;
    C = nullptr;
  } else if (B->type().type == VMType::VM_INTEGER && C->type().type == VMType::VM_INTEGER) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::Integer(),
        [](const Value& a, const Value& b, Value* output) {
          output->integer = a.integer + b.integer;
        });
    B = nullptr;
    C = nullptr;
  } else if ((B->type().type == VMType::VM_INTEGER
              || B->type().type == VMType::VM_DOUBLE)
             && (C->type().type == VMType::VM_INTEGER
                 || C->type().type == VMType::VM_DOUBLE)) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::Double(),
        [](const Value& a, const Value& b, Value* output) {
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              CHECK(false) << "Unexpected value.";
            }
          };
          output->double_value = to_double(a) + to_double(b);
        });
    B = nullptr;
    C = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to add types: \"" + B->type().ToString()
        + L"\" + \"" + C->type().ToString() + L"\"");
    A = nullptr;
  }
}


expr(A) ::= expr(B) MINUS expr(C). {
  if (B == nullptr || C == nullptr) {
    A = nullptr;
  } else if (B->type().type == VMType::VM_INTEGER && C->type().type == VMType::VM_INTEGER) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::Integer(),
        [](const Value& a, const Value& b, Value* output) {
          output->integer = a.integer - b.integer;
        });
    B = nullptr;
    C = nullptr;
  } else if ((B->type().type == VMType::VM_INTEGER
              || B->type().type == VMType::VM_DOUBLE)
             && (C->type().type == VMType::VM_INTEGER
                 || C->type().type == VMType::VM_DOUBLE)) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::Double(),
        [](const Value& a, const Value& b, Value* output) {
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              CHECK(false) << "Unexpected value.";
            }
          };
          output->double_value = to_double(a) - to_double(b);
        });
    B = nullptr;
    C = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to subtract types: \"" + B->type().ToString()
        + L"\" - \"" + C->type().ToString() + L"\"");
    A = nullptr;
  }
}

expr(OUT) ::= MINUS expr(A). {
  if (A == nullptr) {
    OUT = nullptr;
  } else if (A->type().type == VMType::VM_INTEGER) {
    OUT = NewNegateExpression(
        [](Value* value) { value->integer = -value->integer; },
        VMType::Integer(),
        compilation, unique_ptr<Expression>(A)).release();
    A = nullptr;
  } else if (A->type().type == VMType::VM_DOUBLE) {
    OUT = NewNegateExpression(
        [](Value* value) { value->double_value = -value->double_value; },
        VMType::Double(),
        compilation, unique_ptr<Expression>(A)).release();
    A = nullptr;
  } else {
    compilation->errors.push_back(
        L"Invalid expression: -" + A->type().ToString());
    OUT = nullptr;
  }
}

expr(A) ::= expr(B) TIMES expr(C). {
  if (B == nullptr || C == nullptr) {
    A = nullptr;
  } else if (B->type().type == VMType::VM_INTEGER && C->type().type == VMType::VM_INTEGER) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::Integer(),
        [](const Value& a, const Value& b, Value* output) {
          output->integer = a.integer * b.integer;
        });
    B = nullptr;
    C = nullptr;
  } else if (B->type().type == VMType::VM_STRING && C->type().type == VMType::VM_INTEGER) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::String(),
        [](const Value& b, const Value& c, Value* output) {
          for(int i = 0; i < c.integer; i++) {
            output->str += b.str;
          }
        });
    B = nullptr;
    C = nullptr;
  } else if ((B->type().type == VMType::VM_INTEGER
              || B->type().type == VMType::VM_DOUBLE)
             && (C->type().type == VMType::VM_INTEGER
                 || C->type().type == VMType::VM_DOUBLE)) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::Double(),
        [](const Value& a, const Value& b, Value* output) {
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              CHECK(false) << "Unexpected value.";
            }
          };
          output->double_value = to_double(a) * to_double(b);
        });
    B = nullptr;
    C = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to multiply types: \"" + B->type().ToString()
        + L"\" * \"" + C->type().ToString() + L"\"");
    A = nullptr;
  }
}

expr(A) ::= expr(B) DIVIDE expr(C). {
  if (B == nullptr || C == nullptr) {
    A = nullptr;
  } else if (B->type().type == VMType::VM_INTEGER && C->type().type == VMType::VM_INTEGER) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::Integer(),
        [](const Value& a, const Value& b, Value* output) {
          output->integer = a.integer / b.integer;
        });
    B = nullptr;
    C = nullptr;
  } else if ((B->type().type == VMType::VM_INTEGER
              || B->type().type == VMType::VM_DOUBLE)
             && (C->type().type == VMType::VM_INTEGER
                 || C->type().type == VMType::VM_DOUBLE)) {
    A = new BinaryOperator(
        unique_ptr<Expression>(B),
        unique_ptr<Expression>(C),
        VMType::Double(),
        [](const Value& a, const Value& b, Value* output) {
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              CHECK(false) << "Unexpected value.";
            }
          };
          output->double_value = to_double(a) / to_double(b);
        });
    B = nullptr;
    C = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to divide types: \"" + B->type().ToString()
        + L"\" / \"" + C->type().ToString() + L"\"");
    A = nullptr;
  }
}

// Atomic types

expr(OUT) ::= BOOL(B). {
  OUT = NewConstantExpression(unique_ptr<Value>(B)).release();
  B = nullptr;
}

expr(OUT) ::= INTEGER(I). {
  OUT = NewConstantExpression(unique_ptr<Value>(I)).release();
  I = nullptr;
}

expr(OUT) ::= DOUBLE(I). {
  OUT = NewConstantExpression(unique_ptr<Value>(I)).release();
  I = nullptr;
}

%type string { Value* }
%destructor string { delete $$; }

expr(OUT) ::= string(S). {
  OUT = NewConstantExpression(unique_ptr<Value>(S)).release();
  S = nullptr;
}

string(OUT) ::= STRING(S). {
  assert(S->type.type == VMType::VM_STRING);
  OUT = S;
  S = nullptr;
}

string(OUT) ::= string(A) STRING(B). {
  assert(A->type.type == VMType::VM_STRING);
  assert(B->type.type == VMType::VM_STRING);
  OUT = A;
  OUT->str = A->str + B->str;
  A = nullptr;
}

expr(OUT) ::= SYMBOL(S). {
  assert(S->type.type == VMType::VM_SYMBOL);
  OUT = NewVariableLookup(compilation, S->str).release();
  delete S;
}

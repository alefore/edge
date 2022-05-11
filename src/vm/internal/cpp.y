%name Cpp

%extra_argument { Compilation* compilation }

%token_type { Value* }

%left COMMA.
%left QUESTION_MARK.
%left EQ PLUS_EQ MINUS_EQ TIMES_EQ DIVIDE_EQ PLUS_PLUS MINUS_MINUS.
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

program(OUT) ::= statement_list(A) assignment_statement(B). {
  OUT = NewAppendExpression(compilation, unique_ptr<Expression>(A),
                            unique_ptr<Expression>(B)).release();
  A = nullptr;
  B = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// Statement list
////////////////////////////////////////////////////////////////////////////////

%type statement_list { Expression * }
%destructor statement_list { delete $$; }

statement_list(L) ::= . {
  L = NewVoidExpression().get_unique().release();
}

statement_list(OUT) ::= statement_list(A) statement(B). {
  OUT = NewAppendExpression(compilation, unique_ptr<Expression>(A),
                            unique_ptr<Expression>(B)).release();
  A = nullptr;
  B = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// Statements
////////////////////////////////////////////////////////////////////////////////

%type statement { Expression* }
%destructor statement { delete $$; }

statement(A) ::= assignment_statement(B) SEMICOLON . {
  A = B;
  B = nullptr;
}

statement(OUT) ::= namespace_declaration
    LBRACKET statement_list(A) RBRACKET. {
  OUT = NewNamespaceExpression(
      compilation, std::unique_ptr<Expression>(A)).release();
  A = nullptr;
}

namespace_declaration ::= NAMESPACE SYMBOL(NAME). {
  StartNamespaceDeclaration(compilation, NAME->str);
}

statement(OUT) ::= class_declaration
    LBRACKET statement_list(A) RBRACKET SEMICOLON. {
  if (A == nullptr) {
    OUT = nullptr;
  } else {
    FinishClassDeclaration(compilation,
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(A)));
    A = nullptr;
    OUT = NewVoidExpression().get_unique().release();
  }
}

class_declaration ::= CLASS SYMBOL(NAME) . {
  StartClassDeclaration(compilation, NAME->str);
}

statement(OUT) ::= RETURN expr(A) SEMICOLON . {
  OUT = NewReturnExpression(compilation, unique_ptr<Expression>(A)).release();
  A = nullptr;
}

statement(OUT) ::= RETURN SEMICOLON . {
  OUT = NewReturnExpression(compilation,
      std::move(NewVoidExpression().get_unique())).release();
}

statement(OUT) ::= function_declaration_params(FUNC)
    LBRACKET statement_list(BODY) RBRACKET. {
  if (FUNC == nullptr) {
    OUT = nullptr;
  } else if (BODY == nullptr) {
    // Compilation of the body failed. We should try to restore the environment.
    FUNC->Abort(*compilation);
    OUT = nullptr;
  } else {
    std::wstring error;
    auto value = FUNC->BuildValue(
        *compilation,
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(BODY)),
        &error);
    BODY = nullptr;
    if (value == nullptr) {
      compilation->errors.push_back(error);
      OUT = nullptr;
    } else {
      CHECK(FUNC->name.has_value());
      compilation->environment.value()->Define(
          FUNC->name.value(),
          NonNull<std::unique_ptr<Value>>::Unsafe(std::move(value)));
      OUT = NewVoidExpression().get_unique().release();
    }
  }
}

statement(A) ::= SEMICOLON . {
  A = NewVoidExpression().get_unique().release();
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

statement(OUT) ::=
    FOR LPAREN assignment_statement(INIT) SEMICOLON
        expr(CONDITION) SEMICOLON
        expr(UPDATE)
    RPAREN statement(BODY). {
  OUT = NewForExpression(compilation, std::unique_ptr<Expression>(INIT),
                         std::unique_ptr<Expression>(CONDITION),
                         std::unique_ptr<Expression>(UPDATE),
                         std::unique_ptr<Expression>(BODY)).release();
  INIT = nullptr;
  CONDITION = nullptr;
  UPDATE = nullptr;
  BODY = nullptr;
}

statement(A) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE)
    ELSE statement(FALSE_CASE). {
  A = NewIfExpression(
      compilation,
      unique_ptr<Expression>(CONDITION),
      NewAppendExpression(compilation, unique_ptr<Expression>(TRUE_CASE),
                          std::move(NewVoidExpression().get_unique())),
      NewAppendExpression(compilation, unique_ptr<Expression>(FALSE_CASE),
                          std::move(NewVoidExpression().get_unique())))
      .release();
  CONDITION = nullptr;
  TRUE_CASE = nullptr;
  FALSE_CASE = nullptr;
}

statement(A) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE). {
  A = NewIfExpression(
      compilation,
      unique_ptr<Expression>(CONDITION),
      NewAppendExpression(compilation, unique_ptr<Expression>(TRUE_CASE),
                          std::move(NewVoidExpression().get_unique())),
      std::move(NewVoidExpression().get_unique()))
      .release();
  CONDITION = nullptr;
  TRUE_CASE = nullptr;
}

%type assignment_statement { Expression* }
%destructor assignment_statement { delete $$; }

assignment_statement(A) ::= expr(VALUE) . {
  A = VALUE;
  VALUE = nullptr;
}

// Declaration of a function (signature).
assignment_statement(OUT) ::= function_declaration_params(FUNC). {
  if (FUNC == nullptr) {
    OUT = nullptr;
  } else {
    CHECK(FUNC->name.has_value());
    auto result = NewDefineTypeExpression(
        compilation, L"auto", *FUNC->name, FUNC->type);
    if (result == std::nullopt) {
      OUT = nullptr;
      FUNC->Abort(*compilation);
    } else {
      OUT = NewVoidExpression().get_unique().release();
      FUNC->Done(*compilation);
    }
  }
}

assignment_statement(A) ::= SYMBOL(TYPE) SYMBOL(NAME) . {
  auto result = NewDefineTypeExpression(compilation, TYPE->str, NAME->str, {});
  delete TYPE;
  delete NAME;
  A = result == std::nullopt
      ? nullptr
      : NewVoidExpression().get_unique().release();
}

assignment_statement(A) ::= SYMBOL(TYPE) SYMBOL(NAME) EQ expr(VALUE) . {
  A = NewDefineExpression(compilation, TYPE->str, NAME->str,
                          unique_ptr<Expression>(VALUE)).release();
  delete TYPE;
  delete NAME;
  VALUE = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// Function declaration
////////////////////////////////////////////////////////////////////////////////

%type function_declaration_params { UserFunction* }
%destructor function_declaration_params { delete $$; }

function_declaration_params(OUT) ::= SYMBOL(RETURN_TYPE) SYMBOL(NAME) LPAREN
    function_declaration_arguments(ARGS) RPAREN . {
  CHECK(RETURN_TYPE->IsSymbol());
  CHECK(NAME->IsSymbol());
  OUT = UserFunction::New(*compilation, RETURN_TYPE->str, NAME->str, ARGS)
            .release();
  delete RETURN_TYPE;
  delete NAME;
}

// Arguments in the declaration of a function

%type function_declaration_arguments { vector<pair<VMType, wstring>>* }
%destructor function_declaration_arguments { delete $$; }

function_declaration_arguments(OUT) ::= . {
  OUT = new vector<pair<VMType, wstring>>;
}

function_declaration_arguments(OUT) ::=
    non_empty_function_declaration_arguments(L). {
  OUT = L;
  L = nullptr;
}

%type non_empty_function_declaration_arguments {
  vector<pair<VMType, wstring>>*
}
%destructor non_empty_function_declaration_arguments { delete $$; }

non_empty_function_declaration_arguments(OUT) ::= SYMBOL(TYPE) SYMBOL(NAME). {
  const VMType* type_def =
      compilation->environment.value()->LookupType(TYPE->str);
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
    non_empty_function_declaration_arguments(LIST) COMMA SYMBOL(TYPE)
    SYMBOL(NAME). {
  if (LIST == nullptr) {
    OUT = nullptr;
  } else {
    const VMType* type_def =
        compilation->environment.value()->LookupType(TYPE->str);
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

%type lambda_declaration_params { UserFunction* }
%destructor lambda_declaration_params { delete $$; }

// Lambda expression
lambda_declaration_params(OUT) ::= LBRACE RBRACE
    LPAREN function_declaration_arguments(ARGS) RPAREN
    MINUS GREATER_THAN SYMBOL(RETURN_TYPE) . {
  CHECK(RETURN_TYPE->IsSymbol());
  OUT = UserFunction::New(*compilation, RETURN_TYPE->str, std::nullopt, ARGS)
            .release();
  delete RETURN_TYPE;
}

////////////////////////////////////////////////////////////////////////////////
// Expressions
////////////////////////////////////////////////////////////////////////////////

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

expr(OUT) ::= lambda_declaration_params(FUNC)
    LBRACKET statement_list(BODY) RBRACKET . {
  if (FUNC == nullptr) {
    OUT = nullptr;
  } else if (BODY == nullptr) {
    FUNC->Abort(*compilation);
    OUT = nullptr;
  } else {
    std::wstring error;
    auto value = FUNC->BuildExpression(
        *compilation,
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(BODY)),
        &error);
    BODY = nullptr;

    if (value == nullptr) {
      compilation->errors.push_back(error);
      OUT = nullptr;
    } else {
      OUT = value.release();
    }
  }
}

expr(OUT) ::= SYMBOL(NAME) EQ expr(VALUE). {
  OUT = NewAssignExpression(compilation, NAME->str,
                            unique_ptr<Expression>(VALUE)).release();
  VALUE = nullptr;
  delete NAME;
}

expr(OUT) ::= SYMBOL(NAME) PLUS_EQ expr(VALUE). {
  OUT = NewAssignExpression(
            compilation, NAME->str, NewBinaryExpression(
                compilation,
                NewVariableLookup(compilation, {NAME->str}),
                std::unique_ptr<Expression>(VALUE),
                [](wstring a, wstring b) { return Success(a + b); },
                [](int a, int b) { return Success(a + b); },
                [](double a, double b) { return Success(a + b); },
                nullptr)).release();
  NAME = nullptr;
  VALUE = nullptr;
}

expr(OUT) ::= SYMBOL(NAME) MINUS_EQ expr(VALUE). {
  OUT = NewAssignExpression(
            compilation, NAME->str, NewBinaryExpression(
                compilation,
                NewVariableLookup(compilation, {NAME->str}),
                std::unique_ptr<Expression>(VALUE),
                nullptr,
                [](int a, int b) { return Success(a - b); },
                [](double a, double b) { return Success(a - b); },
                nullptr)).release();
  NAME = nullptr;
  VALUE = nullptr;
}

expr(OUT) ::= SYMBOL(NAME) TIMES_EQ expr(VALUE). {
  OUT = NewAssignExpression(
            compilation, NAME->str, NewBinaryExpression(
                compilation,
                NewVariableLookup(compilation, {NAME->str}),
                std::unique_ptr<Expression>(VALUE),
                nullptr,
                [](int a, int b) { return Success(a * b); },
                [](double a, double b) { return Success(a * b); },
                [](wstring a, int b) -> language::ValueOrError<wstring> {
                  wstring output;
                  for(int i = 0; i < b; i++) {
                    try {
                      output += a;
                    } catch (const std::bad_alloc& e) {
                      output = L"";
                      return Error(L"Bad Alloc");
                    } catch (const std::length_error& e) {
                      output = L"";
                      return Error(L"Length Error");
                    }
                  }
                  return Success(output);
                })).release();
  NAME = nullptr;
  VALUE = nullptr;
}

expr(OUT) ::= SYMBOL(NAME) DIVIDE_EQ expr(VALUE). {
  OUT = NewAssignExpression(
            compilation, NAME->str, NewBinaryExpression(
                compilation,
                NewVariableLookup(compilation, {NAME->str}),
                std::unique_ptr<Expression>(VALUE),
                nullptr,
                [](int a, int b) {
                  return b == 0 ? Error(L"Division by zero") : Success(a / b);
                },
                [](double a, double b) {
                  return b == 0 ? Error(L"Division by zero") : Success(a / b);
                },
                nullptr)).release();
  NAME = nullptr;
  VALUE = nullptr;
}

expr(OUT) ::= SYMBOL(NAME) PLUS_PLUS. {
  auto var = NewVariableLookup(compilation, {NAME->str});
  if (var == nullptr) {
    OUT = nullptr;
  } else if (var->IsInteger() || var->IsDouble()) {
    auto type = var->IsInteger() ? VMType::Integer() : VMType::Double();
    OUT = NewAssignExpression(
              compilation, NAME->str,
              std::make_unique<BinaryOperator>(
                  NewVoidExpression(),
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(var)),
                  type,
                  var->IsInteger()
                      ? [](const Value&, const Value& a, Value* output) {
                          output->integer = a.integer + 1;
                          return Success();
                        }
                      : [](const Value&, const Value& a, Value* output) {
                          output->double_value = a.double_value + 1.0;
                          return Success();
                        })).release();
  } else {
    compilation->errors.push_back(
        L"++: Type not supported: " + TypesToString(var->Types()));
    OUT = nullptr;
  }
  NAME = nullptr;
}

expr(OUT) ::= SYMBOL(NAME) MINUS_MINUS. {
  auto var = NewVariableLookup(compilation, {NAME->str});
  if (var == nullptr) {
    OUT = nullptr;
  } else if (var->IsInteger() || var->IsDouble()) {
    auto type = var->IsInteger() ? VMType::Integer() : VMType::Double();
    OUT = NewAssignExpression(
              compilation, NAME->str,
              std::make_unique<BinaryOperator>(
                  NewVoidExpression(),
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(var)),
                  type,
                  var->IsInteger()
                      ? [](const Value&, const Value& a, Value* output) {
                          output->integer = a.integer - 1;
                          return Success();
                        }
                      : [](const Value&, const Value& a, Value* output) {
                          output->double_value = a.double_value - 1.0;
                          return Success();
                        })).release();
  } else {
    compilation->errors.push_back(
        L"--: Type not supported: " + TypesToString(var->Types()));
    OUT = nullptr;
  }
  NAME = nullptr;
}

expr(OUT) ::= expr(B) LPAREN arguments_list(ARGS) RPAREN. {
  if (B == nullptr || ARGS == nullptr) {
    OUT = nullptr;
  } else {
      OUT = NewFunctionCall(compilation,
                    NonNull<std::unique_ptr<Expression>>::Unsafe(
                        std::unique_ptr<Expression>(B)),
                    std::move(*ARGS))
                .release();
      B = nullptr;
      ARGS = nullptr;
  }
}


// Arguments list

%type arguments_list { vector<language::NonNull<unique_ptr<Expression>>>* }
%destructor arguments_list { delete $$; }

arguments_list(OUT) ::= . {
  OUT = new vector<language::NonNull<unique_ptr<Expression>>>;
}

arguments_list(OUT) ::= non_empty_arguments_list(L). {
  OUT = L;
  L = nullptr;
}

%type non_empty_arguments_list {
   vector<language::NonNull<unique_ptr<Expression>>>*
}
%destructor non_empty_arguments_list { delete $$; }

non_empty_arguments_list(OUT) ::= expr(E). {
  if (E == nullptr) {
    OUT = nullptr;
  } else {
    OUT = new vector<NonNull<unique_ptr<Expression>>>();
    OUT->push_back(language::NonNull<std::unique_ptr<Expression>>::Unsafe(
        unique_ptr<Expression>(E)));
    E = nullptr;
  }
}

non_empty_arguments_list(OUT) ::= non_empty_arguments_list(L) COMMA expr(E). {
  if (L == nullptr || E == nullptr) {
    OUT = nullptr;
  } else {
    OUT = L;
    OUT->push_back(language::NonNull<std::unique_ptr<Expression>>::Unsafe(
        unique_ptr<Expression>(E)));
    L = nullptr;
    E = nullptr;
  }
}


// Basic operators

expr(OUT) ::= NOT expr(A). {
  OUT = NewNegateExpression(
      [](Value& value) { value.boolean = !value.boolean; },
      VMType::Bool(),
      compilation, unique_ptr<Expression>(A)).release();
  A = nullptr;
}

expr(OUT) ::= expr(A) EQUALS expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if (A->IsString() && B->IsString()) {
    OUT = new BinaryOperator(
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(A)),
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(B)),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.str == b.str;
          return Success();
        });
    A = nullptr;
    B = nullptr;
  } else if (A->IsInteger() && B->IsInteger()) {
    OUT = new BinaryOperator(
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(A)),
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(B)),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.integer == b.integer;
          return Success();
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: " + TypesToString(A->Types())
        + L" and " + TypesToString(B->Types()) + L".");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) NOT_EQUALS expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if (A->IsString() && B->IsString()) {
    OUT = new BinaryOperator(
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(A)),
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(B)),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.str != b.str;
          return Success();
        });
    A = nullptr;
    B = nullptr;
  } else if (A->IsInteger() && B->IsInteger()) {
    OUT = new BinaryOperator(
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(A)),
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(B)),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          output->boolean = a.integer != b.integer;
          return Success();
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: " + TypesToString(A->Types())
        + L" and " + TypesToString(B->Types()) + L".");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) LESS_THAN expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if ((A->IsInteger() || A->IsDouble())
             && (B->IsInteger() || B->IsDouble())) {
    OUT = new BinaryOperator(
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(A)),
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            std::unique_ptr<Expression>(B)),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          if (a.type.type == VMType::VM_INTEGER
              && b.type.type == VMType::VM_INTEGER) {
            output->boolean = a.integer < b.integer;
            return Success();
          }
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              LOG(FATAL) << "Unexpected value of type: " << x.type.ToString();
              return 0.0;
            }
          };
          output->boolean = to_double(a) < to_double(b);
          return Success();
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: " + TypesToString(A->Types())
        + L" and " + TypesToString(B->Types()) + L".");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) LESS_OR_EQUAL expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if ((A->IsInteger() || A->IsDouble())
             && (B->IsInteger() || B->IsDouble())) {
    OUT = new BinaryOperator(
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            unique_ptr<Expression>(A)),
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            unique_ptr<Expression>(B)),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          if (a.type.type == VMType::VM_INTEGER
              && b.type.type == VMType::VM_INTEGER) {
            output->boolean = a.integer <= b.integer;
            return Success();
          }
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              LOG(FATAL) << "Unexpected value of type: " << x.type.ToString();
              return 0.0;
            }
          };
          output->boolean = to_double(a) <= to_double(b);
          return Success();
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: " + TypesToString(A->Types())
        + L" and " + TypesToString(B->Types()) + L".");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) GREATER_THAN expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if ((A->IsInteger() || A->IsDouble())
             && (B->IsInteger() || B->IsDouble())) {
    OUT = new BinaryOperator(
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            unique_ptr<Expression>(A)),
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            unique_ptr<Expression>(B)),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          if (a.type.type == VMType::VM_INTEGER
              && b.type.type == VMType::VM_INTEGER) {
            output->boolean = a.integer > b.integer;
            return Success();
          }
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              LOG(FATAL) << "Unexpected value of type: " << x.type.ToString();
              return 0.0;
            }
          };
          output->boolean = to_double(a) > to_double(b);
          return Success();
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: " + TypesToString(A->Types())
        + L" and " + TypesToString(B->Types()) + L".");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) GREATER_OR_EQUAL expr(B). {
  if (A == nullptr || B == nullptr) {
    OUT = nullptr;
  } else if ((A->IsInteger() || A->IsDouble())
             && (B->IsInteger() || B->IsDouble())) {
    OUT = new BinaryOperator(
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            unique_ptr<Expression>(A)),
        NonNull<std::unique_ptr<Expression>>::Unsafe(
            unique_ptr<Expression>(B)),
        VMType::Bool(),
        [](const Value& a, const Value& b, Value* output) {
          if (a.type.type == VMType::VM_INTEGER && b.type.type == VMType::VM_INTEGER) {
            output->boolean = a.integer >= b.integer;
            return Success();
          }
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              LOG(FATAL) << "Unexpected value of type: " << x.type.ToString();
              return 0.0;
            }
          };
          output->boolean = to_double(a) >= to_double(b);
          return Success();
        });
    A = nullptr;
    B = nullptr;
  } else {
    compilation->errors.push_back(
        L"Unable to compare types: " + TypesToString(A->Types())
        + L" and " + TypesToString(B->Types()) + L".");
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) OR expr(B). {
  OUT = NewLogicalExpression(compilation, false, unique_ptr<Expression>(A),
                             unique_ptr<Expression>(B))
            .release();
  A = nullptr;
  B = nullptr;
}

expr(OUT) ::= expr(A) AND expr(B). {
  OUT = NewLogicalExpression(compilation, true, unique_ptr<Expression>(A),
                             unique_ptr<Expression>(B))
            .release();
  A = nullptr;
  B = nullptr;
}

expr(OUT) ::= expr(A) PLUS expr(B). {
  OUT = NewBinaryExpression(
            compilation, std::unique_ptr<Expression>(A),
            std::unique_ptr<Expression>(B),
            [](wstring a, wstring b) { return Success(a + b); },
            [](int a, int b) { return Success(a + b); },
            [](double a, double b) { return Success(a + b); },
            nullptr).release();
  A = nullptr;
  B = nullptr;
}


expr(OUT) ::= expr(A) MINUS expr(B). {
  OUT = NewBinaryExpression(
            compilation, std::unique_ptr<Expression>(A),
            std::unique_ptr<Expression>(B), nullptr,
            [](int a, int b) { return Success(a - b); },
            [](double a, double b) { return Success(a - b); },
            nullptr).release();
  A = nullptr;
  B = nullptr;
}

expr(OUT) ::= MINUS expr(A). {
  if (A == nullptr) {
    OUT = nullptr;
  } else if (A->IsInteger()) {
    OUT = NewNegateExpression(
        [](Value& value) { value.integer = -value.integer; },
        VMType::Integer(),
        compilation, unique_ptr<Expression>(A)).release();
    A = nullptr;
  } else if (A->IsDouble()) {
    OUT = NewNegateExpression(
        [](Value& value) { value.double_value = -value.double_value; },
        VMType::Double(),
        compilation, unique_ptr<Expression>(A)).release();
    A = nullptr;
  } else {
    compilation->errors.push_back(
        L"Invalid expression: -: " + TypesToString(A->Types()));
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) TIMES expr(B). {
  OUT = NewBinaryExpression(
            compilation, std::unique_ptr<Expression>(A),
            std::unique_ptr<Expression>(B), nullptr,
            [](int a, int b) { return Success(a * b); },
            [](double a, double b) { return Success(a * b); },
            [](wstring a, int b) -> language::ValueOrError<wstring> {
              wstring output;
              for(int i = 0; i < b; i++) {
                try {
                  output += a;
                } catch (const std::bad_alloc& e) {
                  output = L"";
                  return Error(L"Bad Alloc");
                } catch (const std::length_error& e) {
                  output = L"";
                  return Error(L"Length Error");
                }
              }
              return Success(output);
            }).release();
  A = nullptr;
  B = nullptr;
}

expr(OUT) ::= expr(A) DIVIDE expr(B). {
  OUT = NewBinaryExpression(
            compilation, std::unique_ptr<Expression>(A),
            std::unique_ptr<Expression>(B), nullptr,
            [](int a, int b) {
              return b == 0 ? Error(L"Division by zero") : Success(a / b);
            },
            [](double a, double b) {
              return b == 0 ? Error(L"Division by zero") : Success(a / b);
            },
            nullptr).release();
  A = nullptr;
  B = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// Atomic Expressions
////////////////////////////////////////////////////////////////////////////////

expr(OUT) ::= BOOL(B). {
  OUT = NewConstantExpression(NonNull<std::unique_ptr<Value>>::Unsafe(
      std::unique_ptr<Value>(B))).get_unique().release();
  B = nullptr;
}

expr(OUT) ::= INTEGER(I). {
  OUT = NewConstantExpression(NonNull<std::unique_ptr<Value>>::Unsafe(
      std::unique_ptr<Value>(I))).get_unique().release();
  I = nullptr;
}

expr(OUT) ::= DOUBLE(I). {
  OUT = NewConstantExpression(NonNull<std::unique_ptr<Value>>::Unsafe(
      std::unique_ptr<Value>(I))).get_unique().release();
  I = nullptr;
}

%type string { Value* }
%destructor string { delete $$; }

expr(OUT) ::= string(S). {
  OUT = NewConstantExpression(NonNull<std::unique_ptr<Value>>::Unsafe(
      std::unique_ptr<Value>(S))).get_unique().release();
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

expr(OUT) ::= non_empty_symbols_list(N) . {
  OUT = NewVariableLookup(compilation, std::move(*N)).release();
}

%type non_empty_symbols_list { std::list<std::wstring>* }
%destructor non_empty_symbols_list { delete $$; }

non_empty_symbols_list(OUT) ::= SYMBOL(S). {
  CHECK_EQ(S->type.type, VMType::VM_SYMBOL);
  OUT = new std::list<std::wstring>({std::move(S->str)});
}

non_empty_symbols_list(OUT) ::=
    SYMBOL(S) DOUBLECOLON non_empty_symbols_list(L). {
  CHECK_NE(L, nullptr);
  CHECK_EQ(S->type.type, VMType::VM_SYMBOL);
  L->push_front(S->str);
  OUT = std::move(L);
}

expr(OUT) ::= expr(OBJ) DOT SYMBOL(FIELD). {
  if (OBJ == nullptr) {
    OUT = nullptr;
  } else {
    OUT = NewMethodLookup(compilation, std::unique_ptr<Expression>(OBJ),
                          FIELD->str).release();
    OBJ = nullptr;
  }
  delete FIELD;
}

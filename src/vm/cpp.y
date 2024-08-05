%name Cpp

%extra_argument { Compilation* compilation }

%token_type { std::optional<gc::Root<Value>>* }
%token_destructor { delete $$; }

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
  compilation->AddError(Error{LazyString{L"Compilation error near: \""} +
                              compilation->last_token + LazyString{L"\""}});
}

%type program { Expression* }
%destructor program { delete $$; }

program(OUT) ::= statement_list(A). {
  OUT = A;
}

program(OUT) ::= statement_list(A) assignment_statement(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  OUT =
      ToUniquePtr(NewAppendExpression(*compilation, std::move(a), std::move(b)))
          .release();
}

////////////////////////////////////////////////////////////////////////////////
// Statement list
////////////////////////////////////////////////////////////////////////////////

%type statement_list { Expression * }
%destructor statement_list { delete $$; }

statement_list(L) ::= . {
  L = NewVoidExpression(compilation->pool).get_unique().release();
}

statement_list(OUT) ::= statement_list(A) statement(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  OUT =
      ToUniquePtr(NewAppendExpression(*compilation, std::move(a), std::move(b)))
          .release();
}

////////////////////////////////////////////////////////////////////////////////
// Statements
////////////////////////////////////////////////////////////////////////////////

%type statement { Expression* }
%destructor statement { delete $$; }

statement(A) ::= assignment_statement(B) SEMICOLON . {
  A = B;
}

statement(OUT) ::= namespace_declaration
    LBRACKET statement_list(A) RBRACKET. {
  std::unique_ptr<Expression> a(A);

  OUT = NewNamespaceExpression(*compilation, std::move(a)).release();
}

namespace_declaration ::= NAMESPACE SYMBOL(NAME). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  CHECK(name != nullptr);
  CHECK(name->has_value());
  StartNamespaceDeclaration(*compilation,
      std::move(name->value().ptr()->get_symbol()));
}

statement(OUT) ::= class_declaration
    LBRACKET statement_list(A) RBRACKET SEMICOLON. {
  std::unique_ptr<Expression> a(A);

  if (a == nullptr) {
    OUT = nullptr;
  } else {
    FinishClassDeclaration(
        *compilation,
        NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)));
    OUT = NewVoidExpression(compilation->pool).get_unique().release();
  }
}

class_declaration ::= CLASS SYMBOL(NAME) . {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  StartClassDeclaration(
      *compilation, types::ObjectName(
          std::move(name)->value().ptr()->get_symbol().read().ToString()));
}

statement(OUT) ::= RETURN expr(A) SEMICOLON . {
  std::unique_ptr<Expression> a(A);

  OUT = NewReturnExpression(std::move(a)).release();
}

statement(OUT) ::= RETURN SEMICOLON . {
  OUT = NewReturnExpression(NewVoidExpression(compilation->pool).get_unique())
            .release();
}

statement(OUT) ::= function_declaration_params(FUNC)
    LBRACKET statement_list(BODY) RBRACKET. {
  std::unique_ptr<UserFunction> func(FUNC);
  std::unique_ptr<Expression> body(BODY);

  OUT = nullptr;
  if (func == nullptr) {
    // Pass.
  } else if (body == nullptr) {
    // Compilation of the body failed. We should try to restore the environment.
    func->Abort(*compilation);
  } else {
    std::visit(
        overload{
            IgnoreErrors{},
            [&](gc::Root<Value> value) {
              CHECK(func->name.has_value());
              compilation->environment.ptr()->Define(
                  Identifier(func->name.value()), std::move(value));
              OUT = NewVoidExpression(compilation->pool).get_unique().release();
            }},
        compilation->RegisterErrors(func->BuildValue(
            *compilation,
            NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(body)))));
  }
}

statement(A) ::= SEMICOLON . {
  A = NewVoidExpression(compilation->pool).get_unique().release();
}

statement(A) ::= nesting_lbracket statement_list(L) nesting_rbracket. {
  A = L;
}

nesting_lbracket ::= LBRACKET. {
  compilation->environment = Environment::New(compilation->environment.ptr());
}

nesting_rbracket ::= RBRACKET. {
  // This is safe: nesting_rbracket always follows a corresponding
  // nesting_lbracket.
  CHECK(compilation->environment.ptr()->parent_environment().has_value());
  compilation->environment =
      compilation->environment.ptr()->parent_environment()->ToRoot();
}

statement(OUT) ::= WHILE LPAREN expr(CONDITION) RPAREN statement(BODY). {
  std::unique_ptr<Expression> condition(CONDITION);
  std::unique_ptr<Expression> body(BODY);

  OUT = ToUniquePtr(NewWhileExpression(*compilation, std::move(condition),
                                       std::move(body)))
            .release();
}

statement(OUT) ::=
    FOR LPAREN assignment_statement(INIT) SEMICOLON
        expr(CONDITION) SEMICOLON
        expr(UPDATE)
    RPAREN statement(BODY). {
  std::unique_ptr<Expression> init(INIT);
  std::unique_ptr<Expression> condition(CONDITION);
  std::unique_ptr<Expression> update(UPDATE);
  std::unique_ptr<Expression> body(BODY);

  OUT = ToUniquePtr(NewForExpression(*compilation, std::move(init),
                                     std::move(condition), std::move(update),
                                     std::move(body)))
            .release();
}

statement(A) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE)
    ELSE statement(FALSE_CASE). {
  std::unique_ptr<Expression> condition(CONDITION);
  std::unique_ptr<Expression> true_case(TRUE_CASE);
  std::unique_ptr<Expression> false_case(FALSE_CASE);

  A = ToUniquePtr(NewIfExpression(
                      *compilation, std::move(condition),
                      ToUniquePtr(NewAppendExpression(
                          *compilation, std::move(true_case),
                          NewVoidExpression(compilation->pool).get_unique())),
                      ToUniquePtr(NewAppendExpression(
                          *compilation, std::move(false_case),
                          NewVoidExpression(compilation->pool).get_unique()))))
          .release();
}

statement(A) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE). {
  std::unique_ptr<Expression> condition(CONDITION);
  std::unique_ptr<Expression> true_case(TRUE_CASE);

  A = ToUniquePtr(
          NewIfExpression(
              *compilation, std::move(condition),
              ToUniquePtr(NewAppendExpression(
                  *compilation, std::move(true_case),
                      NewVoidExpression(compilation->pool).get_unique())),
              NewVoidExpression(compilation->pool).get_unique()))
          .release();
}

%type assignment_statement { Expression* }
%destructor assignment_statement { delete $$; }

assignment_statement(A) ::= expr(VALUE) . {
  A = VALUE;
}

// Declaration of a function (signature).
assignment_statement(OUT) ::= function_declaration_params(FUNC). {
  std::unique_ptr<UserFunction> func(FUNC);

  if (func == nullptr) {
    OUT = nullptr;
  } else {
    CHECK(func->name.has_value());
    std::optional<Type> result = NewDefineTypeExpression(
        *compilation, Identifier{LazyString{L"auto"}}, *func->name, func->type);
    if (result == std::nullopt) {
      OUT = nullptr;
      func->Abort(*compilation);
    } else {
      OUT = NewVoidExpression(compilation->pool).get_unique().release();
      func->Done(*compilation);
    }
  }
}

assignment_statement(OUT) ::= SYMBOL(TYPE) SYMBOL(NAME) . {
  std::unique_ptr<std::optional<gc::Root<Value>>> type(TYPE);
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  auto result = NewDefineTypeExpression(
      *compilation, type->value().ptr()->get_symbol(),
      name->value().ptr()->get_symbol(), {});
  OUT = result == std::nullopt
        ? nullptr
        : NewVoidExpression(compilation->pool).get_unique().release();
}

assignment_statement(A) ::= SYMBOL(TYPE) SYMBOL(NAME) EQ expr(VALUE) . {
  std::unique_ptr<std::optional<gc::Root<Value>>> type(TYPE);
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::unique_ptr<Expression> value(VALUE);

  A = NewDefineExpression(*compilation, type->value().ptr()->get_symbol(),
                          name->value().ptr()->get_symbol(), std::move(value))
          .release();
}

////////////////////////////////////////////////////////////////////////////////
// Function declaration
////////////////////////////////////////////////////////////////////////////////

%type function_declaration_params { UserFunction* }
%destructor function_declaration_params { delete $$; }

function_declaration_params(OUT) ::= SYMBOL(RETURN_TYPE) SYMBOL(NAME) LPAREN
    function_declaration_arguments(ARGS) RPAREN . {
  std::unique_ptr<std::optional<gc::Root<Value>>> return_type(RETURN_TYPE);
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::unique_ptr<std::vector<std::pair<Type, Identifier>>> args(ARGS);

  CHECK(return_type->value().ptr()->IsSymbol());
  CHECK(name->value().ptr()->IsSymbol());
  OUT =
      UserFunction::New(*compilation, return_type->value().ptr()->get_symbol(),
                        name->value().ptr()->get_symbol(), std::move(args))
          .release();
}

// Arguments in the declaration of a function

%type function_declaration_arguments { vector<pair<Type, Identifier>>* }
%destructor function_declaration_arguments { delete $$; }

function_declaration_arguments(OUT) ::= . {
  OUT = new vector<pair<Type, Identifier>>();
}

function_declaration_arguments(OUT) ::=
    non_empty_function_declaration_arguments(L). {
  OUT = L;
}

%type non_empty_function_declaration_arguments {
  vector<pair<Type, Identifier>>*
}
%destructor non_empty_function_declaration_arguments { delete $$; }

non_empty_function_declaration_arguments(OUT) ::= SYMBOL(TYPE) SYMBOL(NAME). {
  std::unique_ptr<std::optional<gc::Root<Value>>> type(TYPE);
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  const Type* type_def = compilation->environment.ptr()->LookupType(
      // TODO(easy, 2023-12-22): Make `get_symbol` return an Identifier.
      Identifier(type->value().ptr()->get_symbol()));
  if (type_def == nullptr) {
    compilation->AddError(Error{LazyString{L"Unknown type: \""} +
                                type->value().ptr()->get_symbol().read() +
                                LazyString{L"\""}});
    OUT = nullptr;
  } else {
    OUT = new std::vector<std::pair<Type, Identifier>>();
    OUT->push_back(
        std::make_pair(*type_def, name->value().ptr()->get_symbol()));
  }
}

non_empty_function_declaration_arguments(OUT) ::=
    non_empty_function_declaration_arguments(LIST) COMMA SYMBOL(TYPE)
    SYMBOL(NAME). {
  std::unique_ptr<vector<pair<Type, Identifier>>> list(LIST);
  std::unique_ptr<std::optional<gc::Root<Value>>> type(TYPE);
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  if (list == nullptr) {
    OUT = nullptr;
  } else {
    const Type* type_def = compilation->environment.ptr()->LookupType(
        // TODO(easy, 2023-12-22): Make `get_symbol` return an Identifier.
        Identifier(type->value().ptr()->get_symbol()));
    if (type_def == nullptr) {
    compilation->AddError(Error{LazyString{L"Unknown type: \""} +
                          type->value().ptr()->get_symbol().read() +
                          LazyString{L"\""}});
      OUT = nullptr;
    } else {
      OUT = list.release();
      OUT->push_back(
          std::make_pair(*type_def, name->value().ptr()->get_symbol()));
    }
  }
}

%type lambda_declaration_params { UserFunction* }
%destructor lambda_declaration_params { delete $$; }

// Lambda expression
lambda_declaration_params(OUT) ::= LBRACE RBRACE
    LPAREN function_declaration_arguments(ARGS) RPAREN
    MINUS GREATER_THAN SYMBOL(RETURN_TYPE) . {
  std::unique_ptr<std::vector<std::pair<Type, Identifier>>> args(ARGS);
  std::unique_ptr<std::optional<gc::Root<Value>>> return_type(RETURN_TYPE);

  CHECK(return_type->value().ptr()->IsSymbol());
  OUT = UserFunction::New(
                *compilation, return_type->value().ptr()->get_symbol(),
                std::nullopt, std::move(args))
            .release();
}

////////////////////////////////////////////////////////////////////////////////
// Expressions
////////////////////////////////////////////////////////////////////////////////

%type expr { Expression* }
%destructor expr { delete $$; }

expr(A) ::= expr(CONDITION) QUESTION_MARK
    expr(TRUE_CASE) COLON expr(FALSE_CASE). {
  std::unique_ptr<Expression> condition(CONDITION);
  std::unique_ptr<Expression> true_case(TRUE_CASE);
  std::unique_ptr<Expression> false_case(FALSE_CASE);

  A = ToUniquePtr(NewIfExpression(*compilation, std::move(condition),
                                  std::move(true_case), std::move(false_case)))
          .release();
}

expr(A) ::= LPAREN expr(B) RPAREN. {
  A = B;
}

expr(OUT) ::= lambda_declaration_params(FUNC)
    LBRACKET statement_list(BODY) RBRACKET . {
  std::unique_ptr<UserFunction> func(FUNC);
  std::unique_ptr<Expression> body(BODY);

  OUT = nullptr;
  if (func == nullptr) {
    // Pass.
  } else if (body == nullptr) {
    func->Abort(*compilation);
  } else {
    std::visit(overload{IgnoreErrors{},
                        [&](NonNull<std::unique_ptr<Expression>> value) {
                          OUT = value.release().get();
                        }},
               compilation->RegisterErrors(func->BuildExpression(
                   *compilation, NonNull<std::unique_ptr<Expression>>::Unsafe(
                                     std::move(body)))));
  }
}

expr(OUT) ::= SYMBOL(NAME) EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::unique_ptr<Expression> value(VALUE);

  OUT = NewAssignExpression(*compilation, name->value().ptr()->get_symbol(),
                            std::move(value))
            .release();
}

expr(OUT) ::= SYMBOL(NAME) PLUS_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::unique_ptr<Expression> value(VALUE);

  OUT = NewAssignExpression(
            *compilation, name->value().ptr()->get_symbol(),
            NewBinaryExpression(
                *compilation,
                NewVariableLookup(*compilation,
                                  {name->value().ptr()->get_symbol()}),
                std::move(value),
                [](LazyString a, LazyString b) { return Success(a + b); },
                [](numbers::Number a, numbers::Number b) {
                  return std::move(a) + std::move(b);
                },
                nullptr))
            .release();
}

expr(OUT) ::= SYMBOL(NAME) MINUS_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::unique_ptr<Expression> value(VALUE);

  OUT = NewAssignExpression(
            *compilation, name->value().ptr()->get_symbol(),
            NewBinaryExpression(
                *compilation,
                NewVariableLookup(*compilation,
                                  {name->value().ptr()->get_symbol()}),
                std::move(value), nullptr,
                [](numbers::Number a, numbers::Number b) {
                  return std::move(a) - std::move(b);
                },
                nullptr))
            .release();
}

expr(OUT) ::= SYMBOL(NAME) TIMES_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::unique_ptr<Expression> value(VALUE);

  OUT = NewAssignExpression(
            *compilation, name->value().ptr()->get_symbol(),
            NewBinaryExpression(
                *compilation,
                NewVariableLookup(*compilation,
                                  {name->value().ptr()->get_symbol()}),
                std::move(value), nullptr,
                [](numbers::Number a, numbers::Number b) {
                  return std::move(a) * std::move(b);
                },
                [](LazyString a, int b) -> language::ValueOrError<LazyString> {
                  LazyString output;
                  for (int i = 0; i < b; i++) {
                    try {
                      output += a;
                    } catch (const std::bad_alloc& e) {
                      return Error{LazyString{L"Bad Alloc"}};
                    } catch (const std::length_error& e) {
                      return Error{LazyString{L"Length Error"}};
                    }
                  }
                  return Success(output);
                }))
            .release();
}

expr(OUT) ::= SYMBOL(NAME) DIVIDE_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::unique_ptr<Expression> value(VALUE);

  OUT = NewAssignExpression(
            *compilation, name->value().ptr()->get_symbol(),
            NewBinaryExpression(
                *compilation,
                NewVariableLookup(*compilation,
                                  {name->value().ptr()->get_symbol()}),
                std::move(value), nullptr,
                [](numbers::Number a, numbers::Number b) {
                  return std::move(a) / std::move(b);
                },
                nullptr))
            .release();
}

expr(OUT) ::= SYMBOL(NAME) PLUS_PLUS. {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  auto var =
      NewVariableLookup(*compilation, {name->value().ptr()->get_symbol()});
  if (var == nullptr) {
    OUT = nullptr;
  } else if (var->IsNumber()) {
    OUT = NewAssignExpression(
              *compilation, name->value().ptr()->get_symbol(),
              std::unique_ptr<Expression>(
                  ValueOrDie(
                      BinaryOperator::New(
                          NewVoidExpression(compilation->pool),
                          NonNull<std::unique_ptr<Expression>>::Unsafe(
                              std::move(var)),
                          types::Number{},
                          [](gc::Pool& pool, const Value&, const Value& a) {
                            return Value::NewNumber(
                                pool,
                                numbers::Number(a.get_number())
                                    + numbers::Number::FromInt64(1));
                          }))
                      .get_unique()))
              .release();
  } else {
    compilation->AddError(Error{
        LazyString{L"++: Type not supported: "} + TypesToString(var->Types())});
    OUT = nullptr;
  }
}

expr(OUT) ::= SYMBOL(NAME) MINUS_MINUS. {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  auto var =
      NewVariableLookup(*compilation, {name->value().ptr()->get_symbol()});
  if (var == nullptr) {
    OUT = nullptr;
  } else if (var->IsNumber()) {
    OUT = NewAssignExpression(
              *compilation, name->value().ptr()->get_symbol(),
              std::unique_ptr<Expression>(
                  ValueOrDie(
                      BinaryOperator::New(
                          NewVoidExpression(compilation->pool),
                          NonNull<std::unique_ptr<Expression>>::Unsafe(
                              std::move(var)),
                          types::Number{},
                          [](gc::Pool& pool, const Value&, const Value& a) {
                            return Value::NewNumber(
                                pool,
                                numbers::Number(a.get_number())
                                    - numbers::Number::FromInt64(1));
                          }))
                      .get_unique()))
              .release();
  } else {
    compilation->AddError(Error{
        LazyString{L"--: Type not supported: "} + TypesToString(var->Types())});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(B) LPAREN arguments_list(ARGS) RPAREN. {
  std::unique_ptr<Expression> b(B);
  std::unique_ptr<vector<language::NonNull<shared_ptr<Expression>>>> args(ARGS);

  if (b == nullptr || args == nullptr) {
    OUT = nullptr;
  } else {
      OUT = NewFunctionCall(
                *compilation,
                NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
                std::move(*args))
                .release();
  }
}


// Arguments list

%type arguments_list { vector<language::NonNull<shared_ptr<Expression>>>* }
%destructor arguments_list { delete $$; }

arguments_list(OUT) ::= . {
  OUT = new vector<language::NonNull<shared_ptr<Expression>>>();
}

arguments_list(OUT) ::= non_empty_arguments_list(L). {
  OUT = L;
}

%type non_empty_arguments_list {
   vector<language::NonNull<shared_ptr<Expression>>>*
}
%destructor non_empty_arguments_list { delete $$; }

non_empty_arguments_list(OUT) ::= expr(E). {
  std::unique_ptr<Expression> e(E);

  if (e == nullptr) {
    OUT = nullptr;
  } else {
    OUT = new vector<NonNull<shared_ptr<Expression>>>();
    OUT->push_back(
        language::NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(e)));
  }
}

non_empty_arguments_list(OUT) ::= non_empty_arguments_list(L) COMMA expr(E). {
  std::unique_ptr<std::vector<NonNull<shared_ptr<Expression>>>> l(L);
  std::unique_ptr<Expression> e(E);

  if (l == nullptr || e == nullptr) {
    OUT = nullptr;
  } else {
    OUT = l.release();
    OUT->push_back(
        language::NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(e)));
  }
}


// Basic operators

expr(OUT) ::= NOT expr(A). {
  std::unique_ptr<Expression> a(A);
  OUT = NewNegateExpressionBool(*compilation, std::move(a)).release();
}

expr(OUT) ::= expr(A) EQUALS expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  if (a == nullptr || b == nullptr) {
    OUT = nullptr;
  } else if (a->IsString() && b->IsString()) {
    OUT = ToUniquePtr(
              compilation->RegisterErrors(BinaryOperator::New(
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
                  types::Bool{},
                  [](gc::Pool& pool, const Value& a_str, const Value& b_str) {
                    return Value::NewBool(
                        pool, a_str.get_string() == b_str.get_string());
                  })))
              .release();
  } else if (a->IsNumber() && b->IsNumber()) {
    OUT = ToUniquePtr(
              compilation->RegisterErrors(BinaryOperator::New(
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
                  types::Bool{},
                  [precision = compilation->numbers_precision](
                      gc::Pool& pool, const Value& a_value,
                      const Value& b_value) -> ValueOrError<gc::Root<Value>> {
                    return Value::NewBool(
                        pool, a_value.get_number() == b_value.get_number());
                  })))
              .release();
  } else {
    compilation->AddError(Error{LazyString{L"Unable to compare types: "} +
                                TypesToString(a->Types()) +
                                LazyString{L" == "} +
                                TypesToString(b->Types()) +
                                LazyString{L"."}});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) NOT_EQUALS expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  if (a == nullptr || b == nullptr) {
    OUT = nullptr;
  } else if (a->IsString() && b->IsString()) {
    OUT =
        ToUniquePtr(
            compilation->RegisterErrors(BinaryOperator::New(
                NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
                NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
                types::Bool{},
                [](gc::Pool& pool, const Value& a_value, const Value& b_value) {
                  return Value::NewBool(
                      pool, a_value.get_string() != b_value.get_string());
                })))
            .release();
  } else if (a->IsNumber() && b->IsNumber()) {
    OUT = ToUniquePtr(
              compilation->RegisterErrors(BinaryOperator::New(
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
                  types::Bool{},
                  [precision = compilation->numbers_precision](
                      gc::Pool& pool, const Value& a_value,
                      const Value& b_value) -> ValueOrError<gc::Root<Value>> {
                    return Value::NewBool(
                        pool, a_value.get_number() != b_value.get_number());
                  })))
              .release();
  } else {
    compilation->AddError(Error{LazyString{L"Unable to compare types: "} +
                                TypesToString(a->Types()) +
                                LazyString{L" == "} +
                                TypesToString(b->Types()) +
                                LazyString{L"."}});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) LESS_THAN expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  if (a == nullptr || b == nullptr) {
    OUT = nullptr;
  } else if (a->IsNumber() && b->IsNumber()) {
    OUT = ToUniquePtr(
              compilation->RegisterErrors(BinaryOperator::New(
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
                  types::Bool{},
                  [precision = compilation->numbers_precision](
                      gc::Pool& pool, const Value& a_value,
                      const Value& b_value) -> ValueOrError<gc::Root<Value>> {
                    return Value::NewBool(
                        pool, a_value.get_number() < b_value.get_number());
                  })))
              .release();
  } else {
    compilation->AddError(Error{LazyString{L"Unable to compare types: "} +
                                TypesToString(a->Types()) +
                                LazyString{L" <= "} +
                                TypesToString(b->Types()) + LazyString{L"."}});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) LESS_OR_EQUAL expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  if (a == nullptr || b == nullptr) {
    OUT = nullptr;
  } else if (a->IsNumber() && b->IsNumber()) {
    OUT = ToUniquePtr(
              compilation->RegisterErrors(BinaryOperator::New(
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
                  types::Bool{},
                  [precision = compilation->numbers_precision](
                      gc::Pool& pool, const Value& a_value,
                      const Value& b_value) -> ValueOrError<gc::Root<Value>> {
                    return Value::NewBool(
                        pool, a_value.get_number() <= b_value.get_number());
                  })))
              .release();
  } else {
    compilation->AddError(Error{LazyString{L"Unable to compare types: "} +
                                TypesToString(a->Types()) +
                                LazyString{L" <= "} +
                                TypesToString(b->Types()) + LazyString{L"."}});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) GREATER_THAN expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  if (a == nullptr || b == nullptr) {
    OUT = nullptr;
  } else if (a->IsNumber() && b->IsNumber()) {
    OUT = ToUniquePtr(
              compilation->RegisterErrors(BinaryOperator::New(
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
                  types::Bool{},
                  [precision = compilation->numbers_precision](
                      gc::Pool& pool, const Value& a_value,
                      const Value& b_value) -> ValueOrError<gc::Root<Value>> {
                    return Value::NewBool(
                        pool, a_value.get_number() > b_value.get_number());
                  })))
              .release();
  } else {
    compilation->AddError(Error{LazyString{L"Unable to compare types: "} +
                                TypesToString(a->Types()) +
                                LazyString{L" <= "} +
                                TypesToString(b->Types()) +
                                LazyString{L"."}});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) GREATER_OR_EQUAL expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  if (a == nullptr || b == nullptr) {
    OUT = nullptr;
  } else if (a->IsNumber() && b->IsNumber()) {
    OUT = ToUniquePtr(
              compilation->RegisterErrors(BinaryOperator::New(
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
                  NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
                  types::Bool{},
                  [precision = compilation->numbers_precision](
                      gc::Pool& pool, const Value& a_value,
                      const Value& b_value) -> ValueOrError<gc::Root<Value>> {
                    return Value::NewBool(
                        pool, a_value.get_number() >= b_value.get_number());
                  })))
              .release();
  } else {
    compilation->AddError(Error{LazyString{L"Unable to compare types: "} +
                                TypesToString(a->Types()) +
                                LazyString{L" <= "} +
                                TypesToString(b->Types()) +
                                LazyString{L"."}});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) OR expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  OUT = ToUniquePtr(NewLogicalExpression(*compilation, false, std::move(a),
                                         std::move(b)))
            .release();
}

expr(OUT) ::= expr(A) AND expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  OUT = ToUniquePtr(NewLogicalExpression(*compilation, true, std::move(a),
                                         std::move(b)))
            .release();
}

expr(OUT) ::= expr(A) PLUS expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  OUT = NewBinaryExpression(
            *compilation, std::move(a), std::move(b),
            [](LazyString a_str, LazyString b_str) {
              return Success(a_str + b_str);
            },
            [](numbers::Number a_number, numbers::Number b_number) {
              return std::move(a_number) + std::move(b_number);
            },
            nullptr)
            .release();
}

expr(OUT) ::= expr(A) MINUS expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  OUT = NewBinaryExpression(
            *compilation, std::move(a), std::move(b), nullptr,
            [](numbers::Number a_number, numbers::Number b_number) {
              return std::move(a_number) - std::move(b_number);
            },
            nullptr)
            .release();
}

expr(OUT) ::= MINUS expr(A). {
  std::unique_ptr<Expression> a(A);
  if (a == nullptr) {
    OUT = nullptr;
  } else if (a->IsNumber()) {
    OUT = NewNegateExpressionNumber(*compilation, std::move(a)).release();
  } else {
    compilation->AddError(Error{
        LazyString{L"Invalid expression: -: "} + TypesToString(a->Types())});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) TIMES expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  OUT = NewBinaryExpression(
            *compilation, std::move(a), std::move(b), nullptr,
            [](numbers::Number a_number, numbers::Number b_number) {
              return std::move(a_number) * std::move(b_number);
            },
            [](LazyString a_str, int b_int)
                -> language::ValueOrError<LazyString> {
              LazyString output;
              for (int i = 0; i < b_int; i++) {
                try {
                  output += a_str;
                } catch (const std::bad_alloc& e) {
                  return Error{LazyString{L"Bad Alloc"}};
                } catch (const std::length_error& e) {
                  return Error{LazyString{L"Length Error"}};
                }
              }
              return Success(output);
            })
            .release();
}

expr(OUT) ::= expr(A) DIVIDE expr(B). {
  std::unique_ptr<Expression> a(A);
  std::unique_ptr<Expression> b(B);

  OUT = NewBinaryExpression(
            *compilation, std::move(a), std::move(b), nullptr,
            [](numbers::Number a_number, numbers::Number b_number) {
              return std::move(a_number) / std::move(b_number);
            },
            nullptr)
            .release();
}

////////////////////////////////////////////////////////////////////////////////
// Atomic Expressions
////////////////////////////////////////////////////////////////////////////////

expr(OUT) ::= BOOL(B). {
  // TODO(easy, 2022-05-13): Add a Value::IsBool check.
  CHECK(B->value().ptr()->type == Type(types::Bool{}));
  OUT = NewConstantExpression(std::move(B->value())).get_unique().release();
  delete B;
}

expr(OUT) ::= NUMBER(I). {
  CHECK(I->value().ptr()->IsNumber());
  OUT = NewConstantExpression(std::move(I->value())).get_unique().release();
  delete I;
}

%type string { gc::Root<Value>* }
%destructor string { delete $$; }

expr(OUT) ::= string(S). {
  CHECK(S->ptr()->IsString());
  OUT = NewConstantExpression(std::move(*S)).get_unique().release();
  delete S;
}

string(OUT) ::= STRING(S). {
  CHECK(S->value().ptr()->IsString());
  OUT = std::make_unique<gc::Root<Value>>(std::move(S->value())).release();
  delete S;
}

string(OUT) ::= string(A) STRING(B). {
  CHECK(std::holds_alternative<types::String>(A->ptr()->type));
  CHECK(std::holds_alternative<types::String>(B->value().ptr()->type));
  OUT = std::make_unique<gc::Root<Value>>(Value::NewString(compilation->pool,
      LazyString{std::move(A->ptr()->get_string())}
      + LazyString{std::move(B->value().ptr()->get_string())})).release();
  delete A;
  delete B;
}

expr(OUT) ::= non_empty_symbols_list(N) . {
  OUT = NewVariableLookup(*compilation, std::move(*N)).release();
  delete N;
}

%type non_empty_symbols_list { std::list<Identifier>* }
%destructor non_empty_symbols_list { delete $$; }

non_empty_symbols_list(OUT) ::= SYMBOL(S). {
  CHECK(
      std::holds_alternative<types::Symbol>(S->value().ptr()->type));
  OUT = new std::list<Identifier>(
      {std::move(S->value().ptr()->get_symbol())});
  delete S;
}

non_empty_symbols_list(OUT) ::=
    SYMBOL(S) DOUBLECOLON non_empty_symbols_list(L). {
  CHECK_NE(L, nullptr);
  CHECK(
      std::holds_alternative<types::Symbol>(S->value().ptr()->type));
  L->push_front(std::move(S->value().ptr()->get_symbol()));
  OUT = L;
  delete S;
}

expr(OUT) ::= expr(OBJ) DOT SYMBOL(FIELD). {
  OUT = NewMethodLookup(*compilation, std::unique_ptr<Expression>(OBJ),
                        FIELD->value().ptr()->get_symbol()).release();
  delete FIELD;
}

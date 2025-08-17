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
  RULE_VAR(p, P);
  compilation->expr = language::OptionalFrom(std::move(p));
}

main ::= error. {
  compilation->AddError(Error{LazyString{L"Compilation error near: \""} +
                              compilation->last_token + LazyString{L"\""}});
}

%type program {
  // Never nullptr.
  RootExpressionOrError*
}
%destructor program { delete $$; }

program(OUT) ::= statement_list(A). {
  OUT = A;
}

program(OUT) ::= statement_list(A) assignment_statement(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(NewAppendExpression(*compilation, ToPtr(std::move(a)),
                                       ToPtr(std::move(b))));
}

////////////////////////////////////////////////////////////////////////////////
// Statement list
////////////////////////////////////////////////////////////////////////////////

%type statement_list {
  // Never nullptr.
  RootExpressionOrError*
}
%destructor statement_list { delete $$; }

statement_list(L) ::= . {
  L = RuleReturn(Success(NewVoidExpression(compilation->pool)));
}

statement_list(OUT) ::= statement_list(A) statement(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(NewAppendExpression(*compilation, ToPtr(std::move(a)),
                                       ToPtr(std::move(b))));
}

////////////////////////////////////////////////////////////////////////////////
// Statements
////////////////////////////////////////////////////////////////////////////////

%type statement {
  // Never nullptr.
  RootExpressionOrError*
}
%destructor statement { delete $$; }

statement(A) ::= assignment_statement(B) SEMICOLON . {
  A = B;
}

statement(OUT) ::= namespace_declaration
    LBRACKET statement_list(A) RBRACKET. {
  RULE_VAR(a, A);

  OUT = RuleReturn(NewNamespaceExpression(
      *compilation, language::OptionalFrom(std::move(a))));
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
  RULE_VAR(a, A);

  OUT = RuleReturn(std::visit(
      overload {
          [](Error error) -> RootExpressionOrError { return error; },
          [&](gc::Root<Expression> expr) -> RootExpressionOrError {
            FinishClassDeclaration(*compilation, expr);
            return NewVoidExpression(compilation->pool);
          }},
      std::move(a)));
}

class_declaration ::= CLASS SYMBOL(NAME) . {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  StartClassDeclaration(
      *compilation, types::ObjectName(
          std::move(name)->value().ptr()->get_symbol()));
}

statement(OUT) ::= RETURN expr(A) SEMICOLON . {
  RULE_VAR(a, A);

  OUT = RuleReturn(NewReturnExpression(ToPtr(std::move(a))));
}

statement(OUT) ::= RETURN SEMICOLON . {
  OUT = RuleReturn(
      NewReturnExpression(NewVoidExpression(compilation->pool).ptr()));
}

statement(OUT) ::= function_declaration_params(FUNC)
    LBRACKET statement_list(BODY) RBRACKET. {
  std::unique_ptr<UserFunction> func(FUNC);
  RULE_VAR(body, BODY);

  auto out = (std::invoke([&] -> RootExpressionOrError {
    if (func == nullptr) return Error{LazyString{L"Missing function."}};
    DECLARE_OR_RETURN(gc::Root<Expression> body_expr, body);
    DECLARE_OR_RETURN(
        gc::Root<Value> value,
        compilation->RegisterErrors(func->BuildValue(body_expr.ptr())));
    compilation->environment->parent_environment().value()->Define(
        Identifier(func->name().value()), std::move(value));
    return NewVoidExpression(compilation->pool);
  }));
  if (func != nullptr && IsError(out)) func->Abort();
  OUT = RuleReturn(std::move(out));
}

statement(OUT) ::= SEMICOLON . {
  OUT = RuleReturn(Success(NewVoidExpression(compilation->pool)));
}

statement(A) ::= nesting_lbracket statement_list(L) nesting_rbracket. {
  A = L;
}

nesting_lbracket ::= LBRACKET. {
  compilation->environment = Environment::New(compilation->environment).ptr();
}

nesting_rbracket ::= RBRACKET. {
  // This is safe: nesting_rbracket always follows a corresponding
  // nesting_lbracket.
  CHECK(compilation->environment->parent_environment().has_value());
  compilation->environment =
      compilation->environment->parent_environment().value();
}

statement(OUT) ::= WHILE LPAREN expr(CONDITION) RPAREN statement(BODY). {
  RULE_VAR(condition, CONDITION);
  RULE_VAR(body, BODY);

  OUT = RuleReturn(
      NewWhileExpression(*compilation, ToPtr(condition), ToPtr(body)));
}

statement(OUT) ::=
    FOR LPAREN assignment_statement(INIT) SEMICOLON
        expr(CONDITION) SEMICOLON
        expr(UPDATE)
    RPAREN statement(BODY). {
  RULE_VAR(init, INIT);
  RULE_VAR(condition, CONDITION);
  RULE_VAR(update, UPDATE);
  RULE_VAR(body, BODY);

  OUT = RuleReturn(NewForExpression(*compilation, ToPtr(init), ToPtr(condition),
                                    ToPtr(update), ToPtr(body)));
}

statement(OUT) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE)
    ELSE statement(FALSE_CASE). {
  RULE_VAR(condition, CONDITION);
  RULE_VAR(true_case, TRUE_CASE);
  RULE_VAR(false_case, FALSE_CASE);

  gc::Root<Expression> void_expression = NewVoidExpression(compilation->pool);

  OUT = RuleReturn(NewIfExpression(
      *compilation, ToPtr(condition),
      ToPtr(NewAppendExpression(*compilation, ToPtr(std::move(true_case)),
                                void_expression.ptr())),
      ToPtr(NewAppendExpression(*compilation, ToPtr(std::move(false_case)),
                                void_expression.ptr()))));
}

statement(OUT) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE). {
  RULE_VAR(condition, CONDITION);
  RULE_VAR(true_case, TRUE_CASE);

  gc::Root<Expression> void_expression = NewVoidExpression(compilation->pool);

  OUT = RuleReturn(NewIfExpression(
      *compilation, ToPtr(condition),
      ToPtr(NewAppendExpression(*compilation, ToPtr(std::move(true_case)),
                                void_expression.ptr())),
      void_expression.ptr()));
}

%type assignment_statement {
  // Never nullptr.
  RootExpressionOrError*
}
%destructor assignment_statement { delete $$; }

assignment_statement(A) ::= expr(VALUE) . {
  RULE_VAR(value, VALUE);
  A = RuleReturn(value);
}

// Declaration of a function (signature).
assignment_statement(OUT) ::= function_declaration_params(FUNC). {
  std::unique_ptr<UserFunction> func(FUNC);

  if (func == nullptr) {
    OUT = RuleReturn(
              RootExpressionOrError(Error{LazyString{L"Func missing."}}));
  } else {
    OUT = RuleReturn(std::visit(
        overload{[&](Type) -> RootExpressionOrError {
                   return NewVoidExpression(compilation->pool);
                 },
                 [&](Error error) -> RootExpressionOrError {
                   func->Abort();
                   return compilation->AddError(error);
                 }},
        DefineUninitializedVariable(
            compilation->environment.value(),
            Identifier{NonEmptySingleLine{SingleLine{LazyString{L"auto"}}}},
            *func->name(), func->type())));
  }
}

assignment_statement(OUT) ::= SYMBOL(TYPE) SYMBOL(NAME) . {
  std::unique_ptr<std::optional<gc::Root<Value>>> type(TYPE);
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  // TODO(easy, 2023-12-22): Make `get_symbol` return an Identifier.
  Identifier type_identifier(type->value().ptr()->get_symbol());

  OUT = RuleReturn(std::visit(
      overload{
          [&](Error) -> RootExpressionOrError {
            return compilation->AddError(
                Error{LazyString{L"Need to explicitly initialize variable "} +
                      QuoteExpr(language::lazy_string::ToSingleLine(
                          name->value().ptr()->get_symbol())) +
                      LazyString{L" of type "} +
                      QuoteExpr(language::lazy_string::ToSingleLine(
                          type_identifier))});
          },
          [&](gc::Root<Expression> constructor) -> RootExpressionOrError {
            return NewDefineExpression(
                *compilation, type->value().ptr()->get_symbol(),
                name->value().ptr()->get_symbol(),
                ToPtr(NewFunctionCall(*compilation, constructor.ptr(), {})));
          }},
      NewVariableLookup(*compilation, {type_identifier})));
}

assignment_statement(OUT) ::= SYMBOL(TYPE) SYMBOL(NAME) EQ expr(VALUE) . {
  std::unique_ptr<std::optional<gc::Root<Value>>> type(TYPE);
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  RULE_VAR(value, VALUE);

  OUT = RuleReturn(
      NewDefineExpression(*compilation, type->value().ptr()->get_symbol(),
                          name->value().ptr()->get_symbol(), ToPtr(value)));
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

%type function_declaration_arguments {
  std::vector<std::pair<Type, Identifier>>*
}
%destructor function_declaration_arguments { delete $$; }

function_declaration_arguments(OUT) ::= . {
  OUT = new std::vector<std::pair<Type, Identifier>>();
}

function_declaration_arguments(OUT) ::=
    non_empty_function_declaration_arguments(L). {
  OUT = L;
}

%type non_empty_function_declaration_arguments {
  std::vector<std::pair<Type, Identifier>>*
}
%destructor non_empty_function_declaration_arguments { delete $$; }

non_empty_function_declaration_arguments(OUT) ::= SYMBOL(TYPE) SYMBOL(NAME). {
  std::unique_ptr<std::optional<gc::Root<Value>>> type(TYPE);
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  const Type* type_def = compilation->environment->LookupType(
      // TODO(easy, 2023-12-22): Make `get_symbol` return an Identifier.
      Identifier(type->value().ptr()->get_symbol()));
  if (type_def == nullptr) {
    compilation->AddError(Error{
        LazyString{L"Unknown type: "} +
        QuoteExpr(language::lazy_string::ToSingleLine(
            type->value().ptr()->get_symbol())) +
        LazyString{L"."}});
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
  std::unique_ptr<std::vector<std::pair<Type, Identifier>>> list(LIST);
  std::unique_ptr<std::optional<gc::Root<Value>>> type(TYPE);
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  if (list == nullptr) {
    OUT = nullptr;
  } else {
    const Type* type_def = compilation->environment->LookupType(
        // TODO(easy, 2023-12-22): Make `get_symbol` return an Identifier.
        Identifier(type->value().ptr()->get_symbol()));
    if (type_def == nullptr) {
      compilation->AddError(Error{
          LazyString{L"Unknown type: \""} +
          ToLazyString(type->value().ptr()->get_symbol()) +
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

%type expr {
  // Never nullptr.
  RootExpressionOrError*
}
%destructor expr { delete $$; }

expr(OUT) ::= expr(CONDITION) QUESTION_MARK
    expr(TRUE_CASE) COLON expr(FALSE_CASE). {
  RULE_VAR(condition, CONDITION);
  RULE_VAR(true_case, TRUE_CASE);
  RULE_VAR(false_case, FALSE_CASE);

  OUT = RuleReturn(NewIfExpression(*compilation, ToPtr(condition),
                                   ToPtr(true_case), ToPtr(false_case)));
}

expr(A) ::= LPAREN expr(B) RPAREN. {
  A = B;
}

expr(OUT) ::= lambda_declaration_params(FUNC)
    LBRACKET statement_list(BODY) RBRACKET . {
  std::unique_ptr<UserFunction> func(FUNC);
  RULE_VAR(body, BODY);

  OUT = RuleReturn(std::invoke([&] -> RootExpressionOrError {
    if (func == nullptr) return Error{LazyString{L"Function missing."}};
    return std::visit(
        overload{[&](Error error) -> RootExpressionOrError {
                   func->Abort();
                   return error;
                 },
                 [&](gc::Root<Expression> body_expr) -> RootExpressionOrError {
                   return compilation->RegisterErrors(
                       func->BuildExpression(body_expr.ptr()));
                 }},
        body);
  }));
}

expr(OUT) ::= SYMBOL(NAME) EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  RULE_VAR(value, VALUE);

  OUT = RuleReturn(NewAssignExpression(
      *compilation, name->value().ptr()->get_symbol(), ToPtr(value)));
}

expr(OUT) ::= SYMBOL(NAME) PLUS_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  RULE_VAR(value, VALUE);

  OUT = RuleReturn(NewAssignExpression(
      *compilation, name->value().ptr()->get_symbol(),
      ToPtr(NewBinaryExpression(
          *compilation,
          ToPtr(NewVariableLookup(*compilation,
                                  {name->value().ptr()->get_symbol()})),
          ToPtr(value),
          [](LazyString a, LazyString b) { return Success(a + b); },
          [](numbers::Number a, numbers::Number b) {
            return std::move(a) + std::move(b);
          },
          nullptr))));
}

expr(OUT) ::= SYMBOL(NAME) MINUS_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  RULE_VAR(value, VALUE);

  OUT = RuleReturn(NewAssignExpression(
      *compilation, name->value().ptr()->get_symbol(),
      ToPtr(NewBinaryExpression(
          *compilation,
          ToPtr(NewVariableLookup(*compilation,
                                  {name->value().ptr()->get_symbol()})),
          ToPtr(value), nullptr,
          [](numbers::Number a, numbers::Number b) {
            return std::move(a) - std::move(b);
          },
          nullptr))));
}

expr(OUT) ::= SYMBOL(NAME) TIMES_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name{NAME};
  RULE_VAR(value, VALUE);

  OUT = RuleReturn(NewAssignExpression(
      *compilation, name->value().ptr()->get_symbol(),
      ToPtr(NewBinaryExpression(
          *compilation,
          ToPtr(NewVariableLookup(
              *compilation, {name->value().ptr()->get_symbol()})),
          ToPtr(value), nullptr,
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
          }))));
}

expr(OUT) ::= SYMBOL(NAME) DIVIDE_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  RULE_VAR(value, VALUE);

  OUT = RuleReturn(NewAssignExpression(
      *compilation, name->value().ptr()->get_symbol(),
      ToPtr(NewBinaryExpression(
          *compilation,
          ToPtr(NewVariableLookup(*compilation,
                                  {name->value().ptr()->get_symbol()})),
          ToPtr(value), nullptr,
          [](numbers::Number a, numbers::Number b) {
            return std::move(a) / std::move(b);
          },
          nullptr))));
}

expr(OUT) ::= SYMBOL(NAME) PLUS_PLUS. {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  OUT = RuleReturn(std::invoke([&] -> RootExpressionOrError {
    DECLARE_OR_RETURN(
        gc::Root<Expression> var,
        NewVariableLookup(*compilation, {name->value().ptr()->get_symbol()}));
    if (!var->IsNumber())
      return compilation->AddError(
          Error{name->value().ptr()->get_symbol() +
                LazyString{L"++: Type not supported: "} +
                TypesToString(std::move(var)->Types())});
    return NewAssignExpression(
        *compilation, name->value().ptr()->get_symbol(),
        ToPtr(BinaryOperator::New(
            NewVoidExpression(compilation->pool).ptr(), var.ptr(),
            types::Number{}, [](gc::Pool& pool, const Value&, const Value& a) {
              return Value::NewNumber(pool, numbers::Number(a.get_number()) +
                                                numbers::Number::FromInt64(1));
            })));
  }));
}

expr(OUT) ::= SYMBOL(NAME) MINUS_MINUS. {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  OUT = RuleReturn(std::invoke([&] -> RootExpressionOrError {
    DECLARE_OR_RETURN(
        gc::Root<Expression> var,
        NewVariableLookup(*compilation, {name->value().ptr()->get_symbol()}));
    if (!var->IsNumber())
      return compilation->AddError(
          Error{name->value().ptr()->get_symbol() +
                LazyString{L"--: Type not supported: "} +
                TypesToString(std::move(var)->Types())});
    return NewAssignExpression(
        *compilation, name->value().ptr()->get_symbol(),
        ToPtr(BinaryOperator::New(
            NewVoidExpression(compilation->pool).ptr(), var.ptr(),
            types::Number{}, [](gc::Pool& pool, const Value&, const Value& a) {
              return Value::NewNumber(pool, numbers::Number(a.get_number()) -
                                                numbers::Number::FromInt64(1));
            })));
  }));
}

expr(OUT) ::= expr(B) LPAREN arguments_list(ARGS) RPAREN. {
  RULE_VAR(b, B);
  std::unique_ptr<std::vector<gc::Root<Expression>>> args(ARGS);

  OUT = RuleReturn(std::invoke([&] -> RootExpressionOrError {
    if (args == nullptr)
      return Error{LazyString{L"Error in arguments list."}};
    DECLARE_OR_RETURN(gc::Root<Expression> b_expr, b);
    return NewFunctionCall(
                *compilation, ToPtr(b),
                container::MaterializeVector(*args | gc::view::Ptr));
  }));
}


// Arguments list

%type arguments_list {
  std::vector<gc::Root<Expression>>*
}
%destructor arguments_list { delete $$; }

arguments_list(OUT) ::= . {
  OUT = new std::vector<gc::Root<Expression>>();
}

arguments_list(OUT) ::= non_empty_arguments_list(L). {
  OUT = L;
}

%type non_empty_arguments_list {
   std::vector<gc::Root<Expression>>*
}
%destructor non_empty_arguments_list { delete $$; }

non_empty_arguments_list(OUT) ::= expr(E). {
  RULE_VAR(e, E);

  if (IsError(e))
    OUT = nullptr;
  else
    OUT = new std::vector<gc::Root<Expression>>({VALUE_OR_DIE(std::move(e))});
}

non_empty_arguments_list(OUT) ::= non_empty_arguments_list(L) COMMA expr(E). {
  std::unique_ptr<std::vector<gc::Root<Expression>>> l(L);
  RULE_VAR(e, E);

  if (l == nullptr || IsError(e)) {
    OUT = nullptr;
  } else {
    OUT = l.release();
    OUT->push_back(VALUE_OR_DIE(std::move(e)));
  }
}


// Basic operators

expr(OUT) ::= NOT expr(A). {
  RULE_VAR(a, A);

  OUT = RuleReturn(NewNegateExpressionBool(*compilation, ToPtr(a)));
}

expr(OUT) ::= expr(A) EQUALS expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(ExpressionEquals(*compilation, ToPtr(a), ToPtr(b)));
}

expr(OUT) ::= expr(A) NOT_EQUALS expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(NewNegateExpressionBool(
      *compilation, ToPtr(ExpressionEquals(*compilation, ToPtr(a), ToPtr(b)))));
}

expr(OUT) ::= expr(A) LESS_THAN expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(std::invoke([&] -> RootExpressionOrError {
    DECLARE_OR_RETURN(gc::Root<Expression> a_expr, a);
    DECLARE_OR_RETURN(gc::Root<Expression> b_expr, b);
    if (!a_expr->IsNumber() || !b_expr->IsNumber())
      return compilation->AddError(
          Error{LazyString{L"Unable to compare types: "} +
                TypesToString(a_expr->Types()) + LazyString{L" < "} +
                TypesToString(b_expr->Types()) + LazyString{L"."}});

    return compilation->RegisterErrors(BinaryOperator::New(
        ToPtr(a_expr), ToPtr(b_expr), types::Bool{},
        [precision = compilation->numbers_precision](
            gc::Pool& pool, const Value& a_value,
            const Value& b_value) -> ValueOrError<gc::Root<Value>> {
          return Value::NewBool(pool,
                                a_value.get_number() < b_value.get_number());
        }));
  }));
}

expr(OUT) ::= expr(A) LESS_OR_EQUAL expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(std::invoke([&] -> RootExpressionOrError {
    DECLARE_OR_RETURN(gc::Root<Expression> a_expr, a);
    DECLARE_OR_RETURN(gc::Root<Expression> b_expr, b);
    if (!a_expr->IsNumber() || !b_expr->IsNumber())
      return compilation->AddError(
          Error{LazyString{L"Unable to compare types: "} +
                TypesToString(a_expr->Types()) + LazyString{L" <= "} +
                TypesToString(b_expr->Types()) + LazyString{L"."}});

    return compilation->RegisterErrors(BinaryOperator::New(
        ToPtr(a_expr), ToPtr(b_expr), types::Bool{},
        [precision = compilation->numbers_precision](
            gc::Pool& pool, const Value& a_value,
            const Value& b_value) -> ValueOrError<gc::Root<Value>> {
          return Value::NewBool(pool,
                                a_value.get_number() <= b_value.get_number());
        }));
  }));
}

expr(OUT) ::= expr(A) GREATER_THAN expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(std::invoke([&] -> RootExpressionOrError {
    DECLARE_OR_RETURN(gc::Root<Expression> a_expr, a);
    DECLARE_OR_RETURN(gc::Root<Expression> b_expr, b);
    if (!a_expr->IsNumber() || !b_expr->IsNumber())
      return compilation->AddError(
          Error{LazyString{L"Unable to compare types: "} +
                TypesToString(a_expr->Types()) + LazyString{L" > "} +
                TypesToString(b_expr->Types()) + LazyString{L"."}});

    return compilation->RegisterErrors(BinaryOperator::New(
        ToPtr(a_expr), ToPtr(b_expr), types::Bool{},
        [precision = compilation->numbers_precision](
            gc::Pool& pool, const Value& a_value,
            const Value& b_value) -> ValueOrError<gc::Root<Value>> {
          return Value::NewBool(pool,
                                a_value.get_number() > b_value.get_number());
        }));
  }));
}

expr(OUT) ::= expr(A) GREATER_OR_EQUAL expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(std::invoke([&] -> RootExpressionOrError {
    DECLARE_OR_RETURN(gc::Root<Expression> a_expr, a);
    DECLARE_OR_RETURN(gc::Root<Expression> b_expr, b);
    if (!a_expr->IsNumber() || !b_expr->IsNumber())
      return compilation->AddError(
          Error{LazyString{L"Unable to compare types: "} +
                TypesToString(a_expr->Types()) + LazyString{L" >= "} +
                TypesToString(b_expr->Types()) + LazyString{L"."}});

    return compilation->RegisterErrors(BinaryOperator::New(
        ToPtr(a_expr), ToPtr(b_expr), types::Bool{},
        [precision = compilation->numbers_precision](
            gc::Pool& pool, const Value& a_value,
            const Value& b_value) -> ValueOrError<gc::Root<Value>> {
          return Value::NewBool(pool,
                                a_value.get_number() >= b_value.get_number());
        }));
  }));
}

expr(OUT) ::= expr(A) OR expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT =
      RuleReturn(NewLogicalExpression(*compilation, false, ToPtr(a), ToPtr(b)));
}

expr(OUT) ::= expr(A) AND expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT =
      RuleReturn(NewLogicalExpression(*compilation, true, ToPtr(a), ToPtr(b)));
}

expr(OUT) ::= expr(A) PLUS expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(NewBinaryExpression(
      *compilation, ToPtr(a), ToPtr(b),
      [](LazyString a_str, LazyString b_str) { return Success(a_str + b_str); },
      [](numbers::Number a_number, numbers::Number b_number) {
        return std::move(a_number) + std::move(b_number);
      },
      nullptr));
}

expr(OUT) ::= expr(A) MINUS expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(NewBinaryExpression(
      *compilation, ToPtr(a), ToPtr(b),
      [](LazyString a_str, LazyString b_str) { return Success(a_str + b_str); },
      [](numbers::Number a_number, numbers::Number b_number) {
        return std::move(a_number) - std::move(b_number);
      },
      nullptr));
}

expr(OUT) ::= MINUS expr(A). {
  RULE_VAR(a, A);

  OUT = RuleReturn(std::invoke([&] -> RootExpressionOrError {
    DECLARE_OR_RETURN(gc::Root<Expression> a_expr, a);
    if (!a_expr->IsNumber())
      return compilation->AddError(
          Error{LazyString{L"Invalid expression: -: "} +
                TypesToString(a_expr->Types())});
    return NewNegateExpressionNumber(*compilation, ToPtr(a_expr));
  }));
}

expr(OUT) ::= expr(A) TIMES expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(NewBinaryExpression(
      *compilation, ToPtr(a), ToPtr(b), nullptr,
      [](numbers::Number a_number, numbers::Number b_number) {
        return std::move(a_number) * std::move(b_number);
      },
      [](LazyString a_str, int b_int) -> language::ValueOrError<LazyString> {
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
      }));
}

expr(OUT) ::= expr(A) DIVIDE expr(B). {
  RULE_VAR(a, A);
  RULE_VAR(b, B);

  OUT = RuleReturn(NewBinaryExpression(
      *compilation, ToPtr(a), ToPtr(b), nullptr,
      [](numbers::Number a_number, numbers::Number b_number) {
        return std::move(a_number) / std::move(b_number);
      },
      nullptr));
}

////////////////////////////////////////////////////////////////////////////////
// Atomic Expressions
////////////////////////////////////////////////////////////////////////////////

expr(OUT) ::= BOOL(B). {
  gc::Root<Value> b{B->value()};
  CHECK(b->IsBool());
  OUT = RuleReturn(Success(NewConstantExpression(b.ptr())));
  delete B;
}

expr(OUT) ::= NUMBER(I). {
  gc::Root<Value> i{I->value()};
  CHECK(i->IsNumber());
  OUT = RuleReturn(Success(NewConstantExpression(i.ptr())));
  delete I;
}

%type string { gc::Root<Value>* }
%destructor string { delete $$; }

expr(OUT) ::= string(S). {
  std::unique_ptr<gc::Root<Value>> s{S};
  CHECK((*s)->IsString());
  OUT = RuleReturn(Success(NewConstantExpression(s->ptr())));
}

string(OUT) ::= STRING(S). {
  CHECK(S->value().ptr()->IsString());
  OUT = std::make_unique<gc::Root<Value>>(std::move(S->value())).release();
  delete S;
}

string(OUT) ::= string(A) STRING(B). {
  CHECK(std::holds_alternative<types::String>(A->ptr()->type()));
  CHECK(std::holds_alternative<types::String>(B->value().ptr()->type()));
  OUT = std::make_unique<gc::Root<Value>>(Value::NewString(compilation->pool,
      LazyString{std::move(A->ptr()->get_string())}
      + LazyString{std::move(B->value().ptr()->get_string())})).release();
  delete A;
  delete B;
}

expr(OUT) ::= non_empty_symbols_list(N) . {
  std::unique_ptr<std::list<Identifier>> n{N};

  OUT = RuleReturn(NewVariableLookup(*compilation, std::move(*N)));
}

%type non_empty_symbols_list { std::list<Identifier>* }
%destructor non_empty_symbols_list { delete $$; }

non_empty_symbols_list(OUT) ::= SYMBOL(S). {
  CHECK(std::holds_alternative<types::Symbol>(S->value().ptr()->type()));
  OUT = new std::list<Identifier>(
      {std::move(S->value().ptr()->get_symbol())});
  delete S;
}

non_empty_symbols_list(OUT) ::=
    SYMBOL(S) DOUBLECOLON non_empty_symbols_list(L). {
  CHECK_NE(L, nullptr);
  CHECK(std::holds_alternative<types::Symbol>(S->value().ptr()->type()));
  L->push_front(std::move(S->value().ptr()->get_symbol()));
  OUT = L;
  delete S;
}

expr(OUT) ::= expr(OBJ) DOT SYMBOL(FIELD). {
  RULE_VAR(obj, OBJ);

  OUT = RuleReturn(NewMethodLookup(*compilation, ToPtr(obj),
                                   FIELD->value().ptr()->get_symbol()));
  delete FIELD;
}

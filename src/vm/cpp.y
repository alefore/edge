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
  std::unique_ptr<std::optional<gc::Root<Expression>>> p{P};
  if (p->has_value())
    // TODO(trivial, 2025-08-04): Avoid the new root?
    compilation->expr = p->value().ptr().ToRoot();
  else
    compilation->expr = std::nullopt;
}

main ::= error. {
  compilation->AddError(Error{LazyString{L"Compilation error near: \""} +
                              compilation->last_token + LazyString{L"\""}});
}

%type program {
  // Never nullptr (but maybe std::nullopt).
  std::optional<gc::Root<Expression>>*
}
%destructor program { delete $$; }

program(OUT) ::= statement_list(A). {
  OUT = A;
}

program(OUT) ::= statement_list(A) assignment_statement(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
      NewAppendExpression(*compilation, OptionalRootToPtr(std::move(a)),
                          OptionalRootToPtr(std::move(b)))));
}

////////////////////////////////////////////////////////////////////////////////
// Statement list
////////////////////////////////////////////////////////////////////////////////

%type statement_list {
  // Never nullptr (but maybe std::nullopt).
  std::optional<gc::Root<Expression>>*
}
%destructor statement_list { delete $$; }

statement_list(L) ::= . {
  L = new std::optional<gc::Root<Expression>>(
      NewVoidExpression(compilation->pool));
}

statement_list(OUT) ::= statement_list(A) statement(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
      NewAppendExpression(*compilation, OptionalRootToPtr(std::move(a)),
                          OptionalRootToPtr(std::move(b)))));
}

////////////////////////////////////////////////////////////////////////////////
// Statements
////////////////////////////////////////////////////////////////////////////////

%type statement {
  // Never nullptr (but maybe std::nullopt).
  std::optional<gc::Root<Expression>>*
}
%destructor statement { delete $$; }

statement(A) ::= assignment_statement(B) SEMICOLON . {
  A = B;
}

statement(OUT) ::= namespace_declaration
    LBRACKET statement_list(A) RBRACKET. {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
      NewNamespaceExpression(*compilation, std::move(a))));
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
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);

  if (a == std::nullopt) {
    OUT = nullptr;
  } else {
    FinishClassDeclaration(*compilation, std::move(a).value());
    OUT = new std::optional<gc::Root<Expression>>(
        NewVoidExpression(compilation->pool));
  }
}

class_declaration ::= CLASS SYMBOL(NAME) . {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  StartClassDeclaration(
      *compilation, types::ObjectName(
          std::move(name)->value().ptr()->get_symbol()));
}

statement(OUT) ::= RETURN expr(A) SEMICOLON . {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);

  OUT = new std::optional<gc::Root<Expression>>(
      NewReturnExpression(std::move(a)));
}

statement(OUT) ::= RETURN SEMICOLON . {
  OUT = new std::optional<gc::Root<Expression>>(
      NewReturnExpression(NewVoidExpression(compilation->pool)));
}

statement(OUT) ::= function_declaration_params(FUNC)
    LBRACKET statement_list(BODY) RBRACKET. {
  std::unique_ptr<UserFunction> func(FUNC);
  std::optional<gc::Root<Expression>> body = MoveOutAndDelete(BODY);

  OUT = nullptr;
  if (func == nullptr) {
    // Pass.
  } else if (body == std::nullopt) {
    func->Abort();
  } else {
    std::visit(
        overload{
            [&](Error) { func->Abort(); },
            [&](gc::Root<Value> value) {
              compilation->environment->parent_environment().value()->Define(
                  Identifier(func->name().value()), std::move(value));
              OUT = new std::optional<gc::Root<Expression>>(
                  NewVoidExpression(compilation->pool));
            }},
        compilation->RegisterErrors(func->BuildValue(body->ptr())));
  }
}

statement(OUT) ::= SEMICOLON . {
  OUT = new std::optional<gc::Root<Expression>>(
            NewVoidExpression(compilation->pool));
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
  std::optional<gc::Root<Expression>> condition = MoveOutAndDelete(CONDITION);
  std::optional<gc::Root<Expression>> body = MoveOutAndDelete(BODY);

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
      NewWhileExpression(*compilation, std::move(condition), std::move(body))));
}

statement(OUT) ::=
    FOR LPAREN assignment_statement(INIT) SEMICOLON
        expr(CONDITION) SEMICOLON
        expr(UPDATE)
    RPAREN statement(BODY). {
  std::unique_ptr<std::optional<gc::Root<Expression>>> init(INIT);
  std::optional<gc::Root<Expression>> condition = MoveOutAndDelete(CONDITION);
  std::optional<gc::Root<Expression>> update = MoveOutAndDelete(UPDATE);
  std::optional<gc::Root<Expression>> body = MoveOutAndDelete(BODY);

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
      NewForExpression(*compilation, std::move(*init), std::move(condition),
                       std::move(update), std::move(body))));
}

statement(OUT) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE)
    ELSE statement(FALSE_CASE). {
  std::optional<gc::Root<Expression>> condition = MoveOutAndDelete(CONDITION);
  std::optional<gc::Root<Expression>> true_case = MoveOutAndDelete(TRUE_CASE);
  std::optional<gc::Root<Expression>> false_case = MoveOutAndDelete(FALSE_CASE);

  gc::Root<Expression> void_expression = NewVoidExpression(compilation->pool);

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
          NewIfExpression(
              *compilation, OptionalRootToPtr(condition),
              OptionalRootToPtr(language::OptionalFrom(NewAppendExpression(
                  *compilation, OptionalRootToPtr(std::move(true_case)),
                  void_expression.ptr()))),
              OptionalRootToPtr(language::OptionalFrom(NewAppendExpression(
                  *compilation, OptionalRootToPtr(std::move(false_case)),
                  void_expression.ptr()))))));
}

statement(OUT) ::= IF LPAREN expr(CONDITION) RPAREN statement(TRUE_CASE). {
  std::optional<gc::Root<Expression>> condition = MoveOutAndDelete(CONDITION);
  std::optional<gc::Root<Expression>> true_case = MoveOutAndDelete(TRUE_CASE);

  gc::Root<Expression> void_expression = NewVoidExpression(compilation->pool);

  OUT = new std::optional<gc::Root<Expression>>(
      language::OptionalFrom(NewIfExpression(
          *compilation, OptionalRootToPtr(condition),
          OptionalRootToPtr(language::OptionalFrom(NewAppendExpression(
              *compilation, OptionalRootToPtr(std::move(true_case)),
              void_expression.ptr()))),
          OptionalRootToPtr(void_expression))));
}

%type assignment_statement {
  // Never nullptr (but maybe std::nullopt).
  std::optional<gc::Root<Expression>>*
}
%destructor assignment_statement { delete $$; }

assignment_statement(A) ::= expr(VALUE) . {
  std::optional<gc::Root<Expression>> value = MoveOutAndDelete(VALUE);

  A = new std::optional<gc::Root<Expression>>(std::move(value));
}

// Declaration of a function (signature).
assignment_statement(OUT) ::= function_declaration_params(FUNC). {
  std::unique_ptr<UserFunction> func(FUNC);

  if (func == nullptr) {
    OUT = nullptr;
  } else {
    OUT = new std::optional<gc::Root<Expression>>(std::visit(
        overload{[&](Type) -> std::optional<gc::Root<Expression>> {
                   return NewVoidExpression(compilation->pool);
                 },
                 [&](Error error) -> std::optional<gc::Root<Expression>> {
                   compilation->AddError(error);
                   func->Abort();
                   return std::nullopt;
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

  ValueOrError<gc::Root<Expression>> constructor =
      NewVariableLookup(*compilation, {type_identifier});

  if (std::holds_alternative<Error>(constructor)) {
    compilation->AddError(Error{
        LazyString{L"Need to explicitly initialize variable "} +
        QuoteExpr(language::lazy_string::ToSingleLine(
            name->value().ptr()->get_symbol())) +
        LazyString{L" of type "} +
        QuoteExpr(language::lazy_string::ToSingleLine(
            type_identifier))});
    OUT = nullptr;
  } else {
    OUT = new std::optional<gc::Root<Expression>>(
              NewDefineExpression(
                  *compilation, type->value().ptr()->get_symbol(),
                  name->value().ptr()->get_symbol(),
                  NewFunctionCall(*compilation,
                                  VALUE_OR_DIE(std::move(constructor)).ptr(),
                                  {})));
  }
}

assignment_statement(A) ::= SYMBOL(TYPE) SYMBOL(NAME) EQ expr(VALUE) . {
  std::unique_ptr<std::optional<gc::Root<Value>>> type(TYPE);
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::optional<gc::Root<Expression>> value = MoveOutAndDelete(VALUE);

  A = new std::optional<gc::Root<Expression>>(
      NewDefineExpression(*compilation, type->value().ptr()->get_symbol(),
                          name->value().ptr()->get_symbol(), std::move(value)));
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

  const Type* type_def = compilation->environment.ptr()->LookupType(
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
    const Type* type_def = compilation->environment.ptr()->LookupType(
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
  // Never nullptr (but maybe std::nullopt).
  std::optional<gc::Root<Expression>>*
}
%destructor expr { delete $$; }

expr(A) ::= expr(CONDITION) QUESTION_MARK
    expr(TRUE_CASE) COLON expr(FALSE_CASE). {
  std::optional<gc::Root<Expression>> condition = MoveOutAndDelete(CONDITION);
  std::optional<gc::Root<Expression>> true_case = MoveOutAndDelete(TRUE_CASE);
  std::optional<gc::Root<Expression>> false_case = MoveOutAndDelete(FALSE_CASE);

  A = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
          NewIfExpression(
              *compilation,
              OptionalRootToPtr(condition),
              OptionalRootToPtr(true_case),
              OptionalRootToPtr(false_case))));
}

expr(A) ::= LPAREN expr(B) RPAREN. {
  A = B;
}

expr(OUT) ::= lambda_declaration_params(FUNC)
    LBRACKET statement_list(BODY) RBRACKET . {
  std::unique_ptr<UserFunction> func(FUNC);
  std::optional<gc::Root<Expression>> body = MoveOutAndDelete(BODY);

  OUT = nullptr;
  if (func == nullptr) {
    // Pass.
  } else if (body == std::nullopt) {
    func->Abort();
  } else {
    OUT = new std::optional<gc::Root<Expression>>(
              language::OptionalFrom(compilation->RegisterErrors(
                  func->BuildExpression(body->ptr()))));
  }
}

expr(OUT) ::= SYMBOL(NAME) EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::optional<gc::Root<Expression>> value = MoveOutAndDelete(VALUE);

  OUT = new std::optional<gc::Root<Expression>>(
            NewAssignExpression(
                *compilation, name->value().ptr()->get_symbol(),
                std::move(value)));
}

expr(OUT) ::= SYMBOL(NAME) PLUS_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::optional<gc::Root<Expression>> value = MoveOutAndDelete(VALUE);

  OUT = new std::optional<gc::Root<Expression>>(
      NewAssignExpression(
          *compilation, name->value().ptr()->get_symbol(),
          language::OptionalFrom(NewBinaryExpression(
              *compilation,
              language::OptionalFrom(NewVariableLookup(
                  *compilation, {name->value().ptr()->get_symbol()})),
              std::move(value),
              [](LazyString a, LazyString b) { return Success(a + b); },
              [](numbers::Number a, numbers::Number b) {
                return std::move(a) + std::move(b);
              },
              nullptr))));
}

expr(OUT) ::= SYMBOL(NAME) MINUS_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::optional<gc::Root<Expression>> value = MoveOutAndDelete(VALUE);

  OUT = new std::optional<gc::Root<Expression>>(NewAssignExpression(
      *compilation, name->value().ptr()->get_symbol(),
      language::OptionalFrom(NewBinaryExpression(
          *compilation,
          language::OptionalFrom(NewVariableLookup(
              *compilation, {name->value().ptr()->get_symbol()})),
          std::move(value), nullptr,
          [](numbers::Number a, numbers::Number b) {
            return std::move(a) - std::move(b);
          },
          nullptr))));
}

expr(OUT) ::= SYMBOL(NAME) TIMES_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name{NAME};
  std::optional<gc::Root<Expression>> value = MoveOutAndDelete(VALUE);

  OUT = new std::optional<gc::Root<Expression>>(NewAssignExpression(
      *compilation, name->value().ptr()->get_symbol(),
      language::OptionalFrom(NewBinaryExpression(
          *compilation,
          language::OptionalFrom(NewVariableLookup(
              *compilation, {name->value().ptr()->get_symbol()})),
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
          }))));
}

expr(OUT) ::= SYMBOL(NAME) DIVIDE_EQ expr(VALUE). {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);
  std::optional<gc::Root<Expression>> value = MoveOutAndDelete(VALUE);

  OUT = new std::optional<gc::Root<Expression>>(
            NewAssignExpression(
                *compilation, name->value().ptr()->get_symbol(),
                language::OptionalFrom(
                    NewBinaryExpression(
                        *compilation,
                        language::OptionalFrom(
                            NewVariableLookup(*compilation,
                                          {name->value().ptr()->get_symbol()})),
                        std::move(value), nullptr,
                        [](numbers::Number a, numbers::Number b) {
                          return std::move(a) / std::move(b);
                        },
                        nullptr))));
}

expr(OUT) ::= SYMBOL(NAME) PLUS_PLUS. {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  ValueOrError<gc::Root<Expression>> var =
      NewVariableLookup(*compilation, {name->value().ptr()->get_symbol()});
  if (IsError(var)) {
    OUT = nullptr;
  } else if (std::get<gc::Root<Expression>>(var)->IsNumber()) {
    OUT = new std::optional<gc::Root<Expression>>(
              NewAssignExpression(
                  *compilation, name->value().ptr()->get_symbol(),
                  VALUE_OR_DIE(BinaryOperator::New(
                      NewVoidExpression(compilation->pool).ptr(),
                      VALUE_OR_DIE(std::move(var)).ptr(),
                      types::Number{},
                      [](gc::Pool& pool, const Value&, const Value& a) {
                        return Value::NewNumber(
                            pool, numbers::Number(a.get_number()) +
                                      numbers::Number::FromInt64(1));
                      }))));
  } else {
    compilation->AddError(Error{
        LazyString{L"++: Type not supported: "}
        + TypesToString(VALUE_OR_DIE(std::move(var))->Types())});
    OUT = nullptr;
  }
}

expr(OUT) ::= SYMBOL(NAME) MINUS_MINUS. {
  std::unique_ptr<std::optional<gc::Root<Value>>> name(NAME);

  ValueOrError<gc::Root<Expression>> var =
      NewVariableLookup(*compilation, {name->value().ptr()->get_symbol()});
  if (IsError(var)) {
    OUT = nullptr;
  } else if (std::get<gc::Root<Expression>>(var)->IsNumber()) {
    OUT = new std::optional<gc::Root<Expression>>(
              NewAssignExpression(
                  *compilation, name->value().ptr()->get_symbol(),
                  VALUE_OR_DIE(BinaryOperator::New(
                      NewVoidExpression(compilation->pool).ptr(),
                      VALUE_OR_DIE(std::move(var)).ptr(),
                      types::Number{},
                      [](gc::Pool& pool, const Value&, const Value& a) {
                        return Value::NewNumber(
                            pool, numbers::Number(a.get_number()) -
                                      numbers::Number::FromInt64(1));
                      }))));
  } else {
    compilation->AddError(Error{
        LazyString{L"--: Type not supported: "}
        + TypesToString(VALUE_OR_DIE(std::move(var))->Types())});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(B) LPAREN arguments_list(ARGS) RPAREN. {
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);
  std::unique_ptr<std::vector<gc::Root<Expression>>> args(ARGS);

  OUT = new std::optional<gc::Root<Expression>>(
      (!b.has_value() || args == nullptr)
          ? std::nullopt
          : NewFunctionCall(
                *compilation, std::move(b)->ptr(),
                container::MaterializeVector(*args | gc::view::Ptr)));
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
  std::optional<gc::Root<Expression>> e = MoveOutAndDelete(E);

  if (e == std::nullopt) {
    OUT = nullptr;
  } else {
    OUT = new std::vector<gc::Root<Expression>>({std::move(e).value()});
  }
}

non_empty_arguments_list(OUT) ::= non_empty_arguments_list(L) COMMA expr(E). {
  std::unique_ptr<std::vector<gc::Root<Expression>>> l(L);
  std::optional<gc::Root<Expression>> e = MoveOutAndDelete(E);

  if (l == nullptr || !e.has_value()) {
    OUT = nullptr;
  } else {
    OUT = l.release();
    OUT->push_back(std::move(e).value());
  }
}


// Basic operators

expr(OUT) ::= NOT expr(A). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
      NewNegateExpressionBool(*compilation, std::move(a))));
}

expr(OUT) ::= expr(A) EQUALS expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  OUT = new std::optional<gc::Root<Expression>>(
      language::OptionalFrom(ExpressionEquals(
          *compilation, OptionalRootToPtr(a), OptionalRootToPtr(b))));
}

expr(OUT) ::= expr(A) NOT_EQUALS expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  OUT = new std::optional<gc::Root<Expression>>(
      language::OptionalFrom(NewNegateExpressionBool(
          *compilation,
          language::OptionalFrom(ExpressionEquals(
              *compilation, OptionalRootToPtr(a), OptionalRootToPtr(b))))));
}

expr(OUT) ::= expr(A) LESS_THAN expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  if (!a.has_value() || !b.has_value()) {
    OUT = nullptr;
  } else if (a.value()->IsNumber() && b.value()->IsNumber()) {
    OUT = new std::optional<gc::Root<Expression>>(
        language::OptionalFrom(compilation->RegisterErrors(BinaryOperator::New(
            std::move(a)->ptr(), std::move(b)->ptr(), types::Bool{},
            [precision = compilation->numbers_precision](
                gc::Pool& pool, const Value& a_value,
                const Value& b_value) -> ValueOrError<gc::Root<Value>> {
              return Value::NewBool(
                  pool, a_value.get_number() < b_value.get_number());
            }))));
  } else {
    compilation->AddError(
        Error{LazyString{L"Unable to compare types: "} +
              TypesToString(a.value()->Types()) + LazyString{L" <= "} +
              TypesToString(b.value()->Types()) + LazyString{L"."}});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) LESS_OR_EQUAL expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  if (!a.has_value() || !b.has_value()) {
    OUT = nullptr;
  } else if (a.value()->IsNumber() && b.value()->IsNumber()) {
    OUT = new std::optional<gc::Root<Expression>>(
        language::OptionalFrom(compilation->RegisterErrors(BinaryOperator::New(
            std::move(a)->ptr(), std::move(b)->ptr(), types::Bool{},
            [precision = compilation->numbers_precision](
                gc::Pool& pool, const Value& a_value,
                const Value& b_value) -> ValueOrError<gc::Root<Value>> {
              return Value::NewBool(
                  pool, a_value.get_number() <= b_value.get_number());
            }))));
  } else {
    compilation->AddError(
        Error{LazyString{L"Unable to compare types: "} +
              TypesToString(a.value()->Types()) + LazyString{L" <= "} +
              TypesToString(b.value()->Types()) + LazyString{L"."}});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) GREATER_THAN expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  if (!a.has_value() || !b.has_value()) {
    OUT = nullptr;
  } else if (a.value()->IsNumber() && b.value()->IsNumber()) {
    OUT = new std::optional<gc::Root<Expression>>(
        language::OptionalFrom(compilation->RegisterErrors(BinaryOperator::New(
            std::move(a)->ptr(), std::move(b)->ptr(), types::Bool{},
            [precision = compilation->numbers_precision](
                gc::Pool& pool, const Value& a_value,
                const Value& b_value) -> ValueOrError<gc::Root<Value>> {
              return Value::NewBool(
                  pool, a_value.get_number() > b_value.get_number());
            }))));
  } else {
    compilation->AddError(
        Error{LazyString{L"Unable to compare types: "} +
              TypesToString(a.value()->Types()) + LazyString{L" <= "} +
              TypesToString(b.value()->Types()) + LazyString{L"."}});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) GREATER_OR_EQUAL expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  if (!a.has_value() || !b.has_value()) {
    OUT = nullptr;
  } else if (a.value()->IsNumber() && b.value()->IsNumber()) {
    OUT = new std::optional<gc::Root<Expression>>(
        language::OptionalFrom(compilation->RegisterErrors(BinaryOperator::New(
            std::move(a)->ptr(), std::move(b)->ptr(), types::Bool{},
            [precision = compilation->numbers_precision](
                gc::Pool& pool, const Value& a_value,
                const Value& b_value) -> ValueOrError<gc::Root<Value>> {
              return Value::NewBool(
                  pool, a_value.get_number() >= b_value.get_number());
            }))));
  } else {
    compilation->AddError(
        Error{LazyString{L"Unable to compare types: "} +
              TypesToString(a.value()->Types()) + LazyString{L" <= "} +
              TypesToString(b.value()->Types()) + LazyString{L"."}});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) OR expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
      NewLogicalExpression(*compilation, false, std::move(a), std::move(b))));
}

expr(OUT) ::= expr(A) AND expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
      NewLogicalExpression(*compilation, true, std::move(a), std::move(b))));
}

expr(OUT) ::= expr(A) PLUS expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  OUT = new std::optional<gc::Root<Expression>>(
      language::OptionalFrom(NewBinaryExpression(
          *compilation, std::move(a), std::move(b),
          [](LazyString a_str, LazyString b_str) {
            return Success(a_str + b_str);
          },
          [](numbers::Number a_number, numbers::Number b_number) {
            return std::move(a_number) + std::move(b_number);
          },
          nullptr)));
}

expr(OUT) ::= expr(A) MINUS expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  OUT = new std::optional<gc::Root<Expression>>(
      language::OptionalFrom(NewBinaryExpression(
          *compilation, std::move(a), std::move(b), nullptr,
          [](numbers::Number a_number, numbers::Number b_number) {
            return std::move(a_number) - std::move(b_number);
          },
          nullptr)));
}

expr(OUT) ::= MINUS expr(A). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);

  if (!a.has_value()) {
    OUT = nullptr;
  } else if (a.value()->IsNumber()) {
    OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
        NewNegateExpressionNumber(*compilation, std::move(a))));
  } else {
    compilation->AddError(Error{LazyString{L"Invalid expression: -: "} +
                                TypesToString(a.value()->Types())});
    OUT = nullptr;
  }
}

expr(OUT) ::= expr(A) TIMES expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  OUT = new std::optional<gc::Root<Expression>>(
      language::OptionalFrom(NewBinaryExpression(
          *compilation, std::move(a), std::move(b), nullptr,
          [](numbers::Number a_number, numbers::Number b_number) {
            return std::move(a_number) * std::move(b_number);
          },
          [](LazyString a_str,
             int b_int) -> language::ValueOrError<LazyString> {
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
          })));
}

expr(OUT) ::= expr(A) DIVIDE expr(B). {
  std::optional<gc::Root<Expression>> a = MoveOutAndDelete(A);
  std::optional<gc::Root<Expression>> b = MoveOutAndDelete(B);

  OUT = new std::optional<gc::Root<Expression>>(
      language::OptionalFrom(NewBinaryExpression(
          *compilation, std::move(a), std::move(b), nullptr,
          [](numbers::Number a_number, numbers::Number b_number) {
            return std::move(a_number) / std::move(b_number);
          },
          nullptr)));
}

////////////////////////////////////////////////////////////////////////////////
// Atomic Expressions
////////////////////////////////////////////////////////////////////////////////

expr(OUT) ::= BOOL(B). {
  gc::Root<Value> b{B->value()};
  CHECK(b->IsBool());
  OUT = new std::optional<gc::Root<Expression>>(
      NewConstantExpression(std::move(b)));
  delete B;
}

expr(OUT) ::= NUMBER(I). {
  gc::Root<Value> i{I->value()};
  CHECK(i->IsNumber());
  OUT = new std::optional<gc::Root<Expression>>(
      NewConstantExpression(std::move(i)));
  delete I;
}

%type string { gc::Root<Value>* }
%destructor string { delete $$; }

expr(OUT) ::= string(S). {
  std::unique_ptr<gc::Root<Value>> s{S};
  CHECK((*s)->IsString());
  OUT = new std::optional<gc::Root<Expression>>(
      NewConstantExpression(std::move(*s)));
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

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
      NewVariableLookup(*compilation, std::move(*N))));
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
  std::optional<gc::Root<Expression>> obj = MoveOutAndDelete(OBJ);

  OUT = new std::optional<gc::Root<Expression>>(language::OptionalFrom(
      NewMethodLookup(*compilation, std::move(obj),
                      FIELD->value().ptr()->get_symbol())));
  delete FIELD;
}

%name Cpp

%extra_argument { Value environment }

%token_type Value

%left LPAREN RPAREN.
%left PLUS MINUS.
%left DIVIDE TIMES.

main ::= program .

program ::= .

program ::= program expr(A) SEMICOLON. {
  //assert(value.type == Value::EDITOR_STATE);
  //value.data.editor_state->SetStatus("Result=" + A.type);
}

expr(A) ::= LPAREN expr(B) RPAREN. {
  A = B;
}

expr(A) ::= expr(B) LPAREN expr(C) RPAREN. {
  assert(B.type == Value::FUNCTION1);
  (*B.data.function1)(C);
  A.type = Value::TYPE_INTEGER;
}

expr(A) ::= expr(B) MINUS  expr(C). {
  A.type = Value::TYPE_INTEGER;
  A.data.integer = B.data.integer - C.data.integer;
}

expr(A) ::= expr(B) PLUS  expr(C). {
  A.type = Value::TYPE_INTEGER;
  A.data.integer = B.data.integer + C.data.integer;
}
expr(A) ::= expr(B) TIMES  expr(C). {
  A.type = Value::TYPE_INTEGER;
  A.data.integer = B.data.integer * C.data.integer;
}
expr(A) ::= expr(B) DIVIDE expr(C). {
  if (C.data.integer != 0) {
    A.type = Value::TYPE_INTEGER;
    A.data.integer = B.data.integer / C.data.integer;
  } else {
    std::cout << "divide by zero" << std::endl;
  }
}  /* end of DIVIDE */

expr(A) ::= INTEGER(B). {
  A = B;
}

expr(A) ::= STRING(B). {
  A = B;
}

expr(A) ::= SYMBOL(B). {
  A = environment.data.environment->Lookup((*B.data.str)->ToString());
}

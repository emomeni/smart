
// $Id$

/** \file compile.h
  
    Functions for use by "compiler" (i.e., parser) are here.
*/

#ifndef COMPILE_H
#define COMPILE_H

#include <stdlib.h>

class shared_object;
class expr;
class type;
class symbol;
struct parser_list;
class option;

/// The actual tokenizer; generated by flex.
int yylex();

// Redirect error messages
void yyerror(const char *msg);


/** For debugging the parser itself.
    Under normal operation, thus function does nothing.
    When debugging, the info string is displayed.
      @param  info  Reduction rule information.
*/
void Reducing(const char* info);

/** Build a type.  On error, returns 0.
      @param  proc  Is this a proc type, or not?
      @param  modif Modifier, if any.  Will be free()d.
      @param  tp    Type name.  Will be free()d.
      @return Constructed type, or 0 for illegal type.
*/
const type* MakeType(bool proc, char* modif, const type* t);


// ******************************************************************
// *                                                                *
// *                                                                *
// *                     List-related functions                     *
// *                                                                *
// *                                                                *
// ******************************************************************


/** Adds a statement to our list (which may be null).
      @param list Circuler list of statements (or NULL)
      @param s    statement to add (ignored if NULL)
      @return If list is NULL, returns a new list containing s.
              Else returns list with s appended.
              If we are at the topmost level, the statement 
              will simply be executed and not added to the list.
*/
parser_list* AppendStatement(parser_list* list, expr* s);


/** Add expression to a list.
    If the list is "collapsable", then if the list contains a single null 
    or error expression, then we collapse the list into a single node
    containing a null or error expression.
      @param  behv  How to deal with null or error expressions?
                      0: add them to the list anyway.
                      1: don't add them to the list.
                      2: if we see one, collapse the entire list.
      @param  list  List of expressions.
      @param  item  Item to add to the list.
      @return The new list.
*/
parser_list* AppendExpression(int behv, parser_list* list, expr* item);

/** Add a formal index (name) to our list.
    This is used for when arrays are defined.
    Since the index names must match the already-defined
    iterators (in the enclosing for-loop), we only need
    to store the names in the list.
      @param  list  List of names (or 0 for empty).
      @param  ident New name to add.  Will eventually be free()d.
      @return A new list.
*/
parser_list* AppendName(parser_list* list, char* ident);

/** Add a symbol to our list.
      @param  list  List of symbols (0 for empty).
      @param  p     Symbol to add.
      @param  kind  Kind of symbol (for error messages).
      @return An updated list.
*/
parser_list* AppendSymbol(parser_list* list, symbol* p, const char* kind);

/** Add a term to a list of sums or products.
      @param  list List of objects (0 for empty).
      @param  op   Operator.
      @param  term The term of the expression.
      @return An updated list.
*/
parser_list* AppendTerm(parser_list* list, int op, expr* term);

// ******************************************************************
// *                                                                *
// *                                                                *
// *                  Statement-related  functions                  *
// *                                                                *
// *                                                                *
// ******************************************************************

/** Remove iterators from our stack, and roll them into a for loop.
      @param  count Number of iterators to remove.
      @param  list  List of statements inside the loop.
      @return For loop statement
*/
expr* BuildForLoop(int count, parser_list* list);

/** Complete the converge statement (started with StartConverge).
      @param list   List of statements
      @return Converge statement
*/
expr* FinishConverge(parser_list* list);

/** Build an option statement.
      @param  o  Option to set.
      @param  v  Value to assign to the option.
      @return A new statement, as appropriate.
*/
expr* BuildOptionStatement(option* o, expr* v);

/** Build an (enumerated) option statement.
      @param  o   Option to set.
      @param  n   Name of radio button to assign to the option.
                  Will be free()d.
      @return A new statement, as appropriate.
*/
expr* BuildOptionStatement(option* o, char* n);

/** Build an (enumerated) option statement, to be followed by nested options.
      @param  o   Option to set.
      @param  n   Name of radio button to assign to the option.
                  Will be free()d.
      @return A new statement, as appropriate.
*/
expr* StartOptionBlock(option* o, char* n);

/** Finish building a nested option statement.
      @param  os    Starting option statement.
      @param  list  List of inner option statements.
      @return A new statement, as appropriate.
*/
expr* FinishOptionBlock(expr* os, parser_list* list);

/** Build a (checkbox) option statement.
      @param  o       Option to set.
      @param  check   If true, we will check lots of boxes.
                      If false, we will uncheck lots of boxes.  
      @param  list    List of names of boxes to be checked / unchecked.
      @return A new statement, as appropriate.
*/
expr* BuildOptionStatement(option* o, bool check, parser_list* list);

/** Builds an expression statement.
      @param  x  An expression to compute.
      @return A void expression (statement) that computes \a x.
*/
expr* BuildExprStatement(expr *x);

/** Start a new converge block.
    Everything from now on is within a converge statement.
    The block is finished by calling FinishConverge().
*/
void StartConverge();

/** Find option with specified name.
      @param  name  Name of the option.  Will be free()d.
      @return An option with the same name, if one exists;
              0 otherwise.
*/
option* BuildOptionHeader(char* name);

/** Add iterator to our stack of iterators (unless it is NULL).

      @param  i  Iterator to add to stack.
      @return 1 if iterator was added,
              0 if it was not (i.e., if i is NULL or ERROR)
*/
int AddIterator(symbol* i);

/** Builds a function "statement".
      @param  f  The function
      @param  r  The return expression
      @return A new statement, as appropriate.
*/
expr* BuildFuncStmt(symbol* f, expr* r);

/** Builds a "variable" statement.
    If we are within a converge, 
        we return an assignment statement.
    If we are within a model,
        we return a measure assignment statement.
    Otherwise, we will perform the assignment as appropriate
    and return 0.

      @param  typ Type of the variable
      @param  id  Name of the variable.
      @param  ret Return expression for the variable.
      @return The appropriate statement.
*/
expr* BuildVarStmt(const type* typ, char* id, expr* ret);

/** Builds a "guess" statement.

      @param  typ Type of the variable
      @param  id  Name of the variable.
      @param  ret Return expression for the variable.
      @return The appropriate statement, or 0 on error
              (for instance, if we are not within a converge).
*/
expr* BuildGuessStmt(const type* typ, char* id, expr* ret);

/** Builds an array assignment statement.
    Similar to BuildVarStmt().
      @param  a   The array
      @param  ret The return expression
      @return An appropriate statement, or 0 on error.
*/
expr* BuildArrayStmt(symbol *a, expr *ret);

/** Builds an array "guess" statement.
    Similar to BuildGuessStmt().
      @param  a   The array
      @param  ret The return expression
      @return An appropriate statement, or 0 on error.
*/
expr* BuildArrayGuess(symbol* a, expr* ret);



// ******************************************************************
// *                                                                *
// *                                                                *
// *                    Symbol-related functions                    *
// *                                                                *
// *                                                                *
// ******************************************************************


/** Build a for-loop iterator.
      @param  typ     Type of the iterator.
      @param  n       Name of the iterator.
      @param  values  Set expression, of the possible values
                      for the iterator.
      @return A new iterator or 0 on error.
*/
symbol* BuildIterator(const type* typ, char* n, expr* values);


/** Used when f is "forward defined".
    This allows us to clear the formal parameters when we're done with them.
*/
void DoneWithFunctionHeader();

/** Build a function "header"
      @param  typ   Return type of the function.
      @param  n     Name of the function.
      @param  list  List of formal parameters.
      @return A new function or 0 on error.
*/
symbol* BuildFunction(const type* typ, char* n, parser_list* list);

/** Build an array "header"
      @param  typ   Return type of the function.
      @param  n     Name of the function.
      @param  list  List of indices (shared_strings).
      @return A new array or 0 on error.
*/
symbol* BuildArray(const type* typ, char* n, parser_list* list);

/** Build a formal parameter
      @param  typ   Type of the parameter.
      @param  name  Name of parameter.  Will eventually be free()d.
      @return A new formal parameter or 0 on error.
*/
symbol* BuildFormal(const type* typ, char* name);

/** Build a formal parameter
      @param  typ   Type of the parameter.
      @param  name  Name of parameter.  Will eventually be free()d.
      @param  deflt Default expression.
      @return A new formal parameter or 0 on error.
*/
symbol* BuildFormal(const type* typ, char* name, expr* deflt);

/** Build a named parameter
      @param  name  Name of parameter.  Will eventually be free()d.
      @param  pass  Expression passed to the parameter.
      @return A new named parameter or 0 on error.
*/
symbol* BuildNamed(char* name, expr* pass);

// ******************************************************************
// *                                                                *
// *                                                                *
// *                    Model-related  functions                    *
// *                                                                *
// *                                                                *
// ******************************************************************


/** Builds a model "statement".
      @param  m     The model.
      @param  block List of statements within the model
*/
void BuildModelStmt(symbol* m, parser_list* block);

/** Build a model "header"
      @param  typ   Type of model.
      @param  n     Name of the function.
      @param  list  List of formal parameters.
      @return A new model or 0 on error.
*/
symbol* BuildModel(const type* typ, char* n, parser_list* list);

/** Build a statement to declare model variables.
    If we're within a for-loop, these will be arrays
    (we already verified the indices in "AddModelArray").
      @param  typ   Type of variables.
      @param  list  List of symbols.
      @return A new statement, or 0 on error.
*/
expr* BuildModelVarStmt(const type* typ, parser_list* list);

/** Add a model variable (name) to our list.
    The name is also added to the model's internal symbol table.
      @param  varlist   Current list of vars.
      @param  ident     New variable name to add.
      @return Updated list of vars.
*/
parser_list* AddModelVar(parser_list* varlist, char* ident);

/** Add a model array (name) to our list.
    The name is also added to the model's internal symbol table.
    The index list is checked against the enclosing for statements.
      @param  varlist   Current list of vars.
      @param  ident     New name of array to add.
      @param  indexlist Index list for the array.
      @return Updated list of vars.
*/
parser_list* AddModelArray(parser_list* varlist, char* ident, parser_list* indexlist);

// ******************************************************************
// *                                                                *
// *                                                                *
// *                  Expression-related functions                  *
// *                                                                *
// *                                                                *
// ******************************************************************


/** Build a set expression for a single-element set.
      @param  elem  Expression to use for the element.
      @return New set expression as appropriate.
*/
expr* BuildElementSet(expr* elem);

/** Build a set expression for an interval.
    The increment is assumed to be 1.
      @param  start Expression to use for the starting element.
      @param  stop  Expression to use for the stopping element.
      @return New set expression as appropriate.
*/
expr* BuildInterval(expr* start, expr* stop);

/** Build a set expression for an interval.
      @param  start Expression to use for the starting element.
      @param  stop  Expression to use for the stopping element.
      @param  inc   Expression to use for the increment.
      @return New set expression as appropriate.
*/
expr* BuildInterval(expr* start, expr* stop, expr* inc);


/** Build a summation expression from an operator-labeled list.
      @param  list  List of (operator, term).
        These should be "compatible" operators,
        e.g., + and -

      @return  New expression as appropriate.
*/
expr* BuildSummation(parser_list* list);


/** Build a product expression from an operator-labeled list.
      @param  list  List of (operator, term).
        These should be "compatible" operators,
        e.g., * and /

      @return  New expression as appropriate.
*/
expr* BuildProduct(parser_list* list);


/** Build an associative expression.
      @param  op    Operator to apply.
      @param  list  List of operands.
      @return A new expression, as appropriate.
*/
expr* BuildAssociative(int op, parser_list* list);

/** Builds the desired binary expression.

      @param  left  Left expression
      @param  op    Operator; use constants in smart.tab.h
      @param  right Right expression

      @return A new expression as appropriate.
*/
expr* BuildBinary(expr* left, int op, expr* right);

/** Type-checks and builds the desired unary expression.

      @param  op    Operator; use constants in smart.tab.h
      @param  opnd  Operand.

      @return A new expression as appropriate.
*/
expr* BuildUnary(int op, expr* opnd);

/** Builds the desired typecast.

      @param  newtype   Desired new type of the expression.
      @param  opnd      Operand expression.

      @return A new expression as appropriate.
*/
expr* BuildTypecast(const type* newtype, expr* opnd);

/** Build a boolean constant.
    
      @param  s  String to build from (i.e., "true" or "false").
        Will be free()d.

      @return  A new expression as appropriate.
*/
expr* MakeBoolConst(char* s);

/** Build an integer constant
    
      @param  s  String to build from (i.e., "153" or "-32").
        Will be free()d.

      @return  A new expression as appropriate.
*/
expr* MakeIntConst(char* s);

/** Build a real constant
    
      @param  s  String to build from (i.e., "3.2" or "1.29e-10").
        Will be free()d.

      @return  A new expression as appropriate.
*/
expr* MakeRealConst(char* s);

/** Build a string constant
    
      @param  s  String to build from.
      @return A new expression as appropriate.
*/
expr* MakeStringConst(char *s);

/** Build a measure call.
    For expressions of the form (funccall).measure
    Makes sure that the measure (m) exists in the model (within struct fc).
      @param  func_call   Structure that handles a "model call",
                          built below by MakeModelCall().
      @param  m           Measure.
      @return A new expression as appropriate.
*/
expr* MakeMCall(shared_object* func_call, char* m);

/** Build a measure call in an array of models.
    For expresions of the form model[indexes].measure
    Makes sure that the measure (m) exists in the model array.
      @param  n     The array (of models) name
      @param  ind   list of array indexes
      @param  m     measure name
      @return A new expression as appropriate.
*/
expr* MakeAMCall(char* n, parser_list* ind, char* m);

/** Build a measure array call.
    For expressions of the form orm (funccall).marray[indices]
    Makes sure that the measure (m) exists in the model (within struct fc),
    and matches array indices.
      @param  func_call   Structure that handles a "model call",
                          built below by MakeModelCall().
      @param  m           Measure, which is an array.
      @param  ind         list of array indexes (for the measure).
      @return A new expression as appropriate.
*/
expr* MakeMACall(shared_object* func_call, char* m, parser_list* ind);

/** Build a measure array call in an array of models.
    For expressions of the form model[indexes].measure[indexes2]
    Makes sure that the measure (m) exists in the model array.
      @param  n     The array (of models) name
      @param  ind   array indexes for the model
      @param  m     measure name
      @param  ind2  array indexes for the measure
      @return A new expression as appropriate.
*/
expr* MakeAMACall(char* n, parser_list* ind, char* m, parser_list* ind2);

/** Handle the first half of a model call.
      @param  n     Name of the model to invoke.
      @param  list  List of positional parameters (or 0 for none).
      @return A data structure with information about the call.
*/
shared_object* MakeModelCallPP(char* n, parser_list* list);

/** Handle the first half of a model call.
      @param  n     Name of the model to invoke.
      @param  list  List of named parameters (or 0 for none).
      @return A data structure with information about the call.
*/
shared_object* MakeModelCallNP(char* n, parser_list* list);

/** Find the best "function with no parameters".
    We check formal parameters, for loop iterators, and user-defined
    functions; if not found, we'll return NULL.
      @param  name  Identifier name
      @return A new expression as appropriate.
*/
expr* FindIdent(char* name);

/** Build an array "call".
      @param  n   The name of the array.  Will be free()d.
      @param  ind list of indexes (expressions)
      @return A new expression as appropriate.
*/
expr* BuildArrayCall(char* n, parser_list* ind);

/** Build a function call with positional parameters.
    Does type checking, promotion, overloading, and everything.
      @param  n   The function name.  Will be free()d.
      @param  ind List of passed (positional) parameters
      @return A new expression as appropriate.
*/
expr* BuildFuncCallPP(char* n, parser_list* posparams);

/** Build a function call with named parameters.
    Does type checking, promotion, overloading, and everything.
      @param  n   The function name.  Will be free()d.
      @param  ind List of passed (named) parameters
      @return A new expression as appropriate.
*/
expr* BuildFuncCallNP(char* n, parser_list* posparams);

/** Return the special "default" expression.
    Used for parameter passing.
*/
expr*  Default();


// ******************************************************************
// *                                                                *
// *                                                                *
// *                      Front-end  functions                      *
// *                                                                *
// *                                                                *
// ******************************************************************


class parse_module;

/** Initialize the compiler data structures.
*/
void InitCompiler(parse_module* pm);

/** Start parsing.
    The lexer must be set to go.
      @param  env  Environment to use.
      @return The error code passed back by the parser.
*/
int Compile(parse_module* pm);

#endif

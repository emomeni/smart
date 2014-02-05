
// $Id$

#include "compile.h"
#include "lexer.h"
#include "../Options/options.h"
#include "../Streams/streams.h"
#include "../ExprLib/exprman.h"
#include "../ExprLib/symbols.h"
#include "../ExprLib/functions.h"
#include "../ExprLib/mod_def.h"
#include "../SymTabs/symtabs.h"
#include "../ExprLib/strings.h"
#include "../ExprLib/formalism.h"
#include "../include/heap.h"
#include "parse_sm.h"
#include <string.h>
#include <stdlib.h>

// Put this one last.
#include "smartyacc.h"

// #define PARSER_DEBUG
// #define COMPILE_DEBUG

/*
    Hopefully, this time around, the compiler support functions
    can be simplified a bit.

    Conventions:

    We use circular linked-lists (with good old-fashioned nodes)
    to allow maximum memory re-use and hopefully speed.
    The list pointer is to the last element in the list
    (or to 0 for an empty list),
    and the last element's next pointer is to the front of the list.
    This allows us to quickly add to the tail of the list :^)
    These are indicated by the type parser_list*
    

    Other conventions here...


    TO DO:

      Currently, user-defined functions are added to the
      same symbol table as builtin functions.  That means
      that whoever invokes the parser can "keep" these
      functions, which may not be the desired behavior.
      Need to think more about the parser interface...
*/

// should be built by bison.
int yyparse();

/* =====================================================================

  Global variables.

   ===================================================================== */

parse_module* pm;
exprman* em;
named_msg parser_debug;
named_msg compiler_debug;

// Expression for the integer constant 1.
expr* ONE = 0;

/// Current stack of for-loop iterators
symbol_table* Iterators = 0;

/// Symbol table of arrays.
symbol_table* Arrays = 0;

/// Symbol table of built-in functions, user-defined functions, and models.
symbol_table* Funcs = 0;

/// Symbol table of "constants", i.e., functions without parameters.
symbol_table* Constants = 0;

/// Depth of converges
int converge_depth;

/// Current function under construction (for finding formal parameters)
function* function_under_construction = 0;

/// Type of model under construction, if any.
const formalism* ModelType = 0;

/// Model under construction
model_def* model_under_construction = 0;

/// Internal symbols for model under construction.
symbol_table* ModelInternal = 0;

/// External (visible) symbols for model under construction.
HeapOfPointers <symbol> ModelExternal;

inline bool WithinFor() { return (Iterators->NumSymbols()); }

inline bool WithinConverge() { return (converge_depth>0); }

inline bool WithinModel() { return ModelType; }

inline bool WithinBlock() { 
  return WithinFor() || WithinConverge() || WithinModel(); 
}

// Compiler stats:

long list_depth;

inline int Compare(const symbol* a, const symbol* b)
{
  DCASSERT(a);
  DCASSERT(b);
  return strcmp(a->Name(), b->Name());
}

/* =====================================================================

  I/O and such.

   ===================================================================== */

void yyerror(const char *msg)
{
  DCASSERT(pm);
  if (pm->startError()) {
    pm->cerr() << msg;
    pm->stopError();
  }
}

void Reducing(const char* msg)
{
  DCASSERT(pm);
  if (parser_debug.startReport()) {
    parser_debug.report() << "reducing rule:\n\t\t";
    parser_debug.report().Put(msg);
    parser_debug.report().Put('\n');
    parser_debug.stopIO();
  }
}

const type* MakeType(bool proc, char* modif, const type* t)
{
  modifier m = 0;
  if (modif) {
    m = em->findModifier(modif);
    if (NO_SUCH_MODIFIER == m) {
      if (pm->startInternal(__FILE__, __LINE__)) {
        pm->internal() << "Bad type modifier: " << modif;
        pm->stopError();
      }
      // shouldn't get here
      return 0;
    }
  }
  const type* answer;
  if (modif) answer = ModifyType(m, t); else answer = t;
  if (proc) answer = ProcifyType(answer);
  free(modif);
  return answer;
}

inline expr* ShowNewStatement(const char* what, expr* f)
{
  // Make noise as appropriate
  if (compiler_debug.startReport()) {
    compiler_debug.report() << "built ";
    if (what) compiler_debug.report() << what;
    else      compiler_debug.report() << "statement: ";
    if (f)  f->Print(compiler_debug.report(), 4);
    else    compiler_debug.report().Put("    null\n");
    compiler_debug.stopIO();
  }
  return f;
}

template <class EXPR>
inline EXPR* ShowWhatWeBuilt(const char* what, EXPR* f)
{
  // Make noise as appropriate
  if (compiler_debug.startReport()) {
    compiler_debug.report() << "built ";
    if (what) compiler_debug.report() << what;
    else      compiler_debug.report() << "expression: ";
    if (f)  f->Print(compiler_debug.report(), 0);
    else    compiler_debug.report().Put("null");
    compiler_debug.report() << "\t type: ";
    if (f)  f->PrintType(compiler_debug.report());
    else    compiler_debug.report().Put("nulltype");
    compiler_debug.report().Put('\n');
    compiler_debug.stopIO();
  }
  return f;
}


/* =====================================================================

  Struct for model calls

   ===================================================================== */

class model_call_data : public shared_object {
public:
  model_def* model1;
  symbol* model2;
  expr** pass;
  int np;
public:
  model_call_data(model_def* m, expr** p, int n);
  model_call_data(symbol* m);
  virtual ~model_call_data();
  
  void Trash();

  virtual bool Print(OutputStream &s, int) const;
  virtual bool Equals(const shared_object* o) const;
};

model_call_data::model_call_data(model_def* m, expr** p, int n)
 : shared_object()
{
  model1 = m;
  model2 = 0;
  pass = p;
  np = n;
}

model_call_data::model_call_data(symbol* m)
 : shared_object()
{
  model1 = 0;
  model2 = m;
  pass = 0;
  np = 0;
}

model_call_data::~model_call_data()
{
}

void model_call_data::Trash()
{
  for (int i=0; i<np; i++)  Delete(pass[i]);
  delete[] pass;
  pass = 0;
  np = 0;
}

bool model_call_data::Print(OutputStream &s, int) const
{
  DCASSERT(0);
  return false;
}

bool model_call_data::Equals(const shared_object* o) const
{
  return (this == o);
}


/* =====================================================================

  Lists for associative arithmetic

   ===================================================================== */

/// Used for lists of sums or products
class expr_term : public shared_object {
public:
  int op;
  expr* term;
public:
  expr_term(int o, expr* t);
  virtual ~expr_term();
  
  virtual bool Print(OutputStream &s, int width) const;
  virtual bool Equals(const shared_object* o) const;
};

expr_term::expr_term(int o, expr* t) : shared_object()
{
  op = o;
  term = t;
}

expr_term::~expr_term()
{
  Delete(term);
}

bool expr_term::Print(OutputStream &s, int) const
{
  s << "(";
  s << TokenName(op);
  s << ", ";
  if (term)   term->Print(s, 0);
  else        s.Put("null");
  s << ")";
  return true;
}

bool expr_term::Equals(const shared_object* o) const
{
  return (this == o);
}

/* =====================================================================

  Centralized list manager.

   ===================================================================== */

struct parser_list {
  shared_object* data;
  parser_list* next;
};

parser_list* FreeList = 0;

parser_list* MakeListNode(shared_object* d)
{
  parser_list* ptr;
  if (FreeList) {
    ptr = FreeList;
    FreeList = ptr->next;
  } else {
    ptr = new parser_list;
    list_depth++;
  }
  ptr->data = d;
  ptr->next = 0;
  return ptr;
}

void RecycleNode(parser_list* ptr)
{
  if (0==ptr) return;
  ptr->next = FreeList;
  FreeList = ptr;
}

void RecycleCircular(parser_list* ptr)
{
  if (0==ptr)  return;
  parser_list* front = ptr->next;
  ptr->next = FreeList;
  FreeList = front;
}

void DeleteCircular(parser_list* ptr)
{
  if (0==ptr)  return;
  parser_list* front = ptr->next;
  ptr->next = 0;
  while (front) {
    parser_list* next = front->next;
    Delete(front->data);
    front->data = 0;
    RecycleNode(front);
    front = next;
  }
}

inline parser_list* AppendCircular(parser_list* list, shared_object* s)
{
  parser_list* ptr = MakeListNode(s);
  if (list) {
    ptr->next = list->next;
    list->next = ptr;
  } else {
    ptr->next = ptr;
  }
  return ptr;
}

inline parser_list* PrependCircular(parser_list* list, shared_object* s)
{
  parser_list* ptr = MakeListNode(s);
  if (list) {
    ptr->next = list->next;
    list->next = ptr;
    return list;
  } else {
    ptr->next = ptr;
    return ptr;
  }
}

parser_list* RemoveCircular(parser_list* thisone)
{
  if (0==thisone)  return 0;
  if (thisone->next == thisone)  {
    RecycleNode(thisone);
    return 0;
  }
  parser_list* next = thisone->next;
  thisone->data = next->data;
  thisone->next = next->next;
  RecycleNode(next);
  return thisone;
}

int CircularLength(parser_list* list)
{
  if (0==list) return 0;
  int length = 1;
  for (parser_list* ptr = list->next; ptr != list; ptr=ptr->next)
  length++;
  return length;
}

template <class DATA>
void CopyCircular(parser_list* ptr, DATA** array, int N)
{
  if (0==ptr) return;
  for (int i=0; i<N; i++) {
    ptr = ptr->next;
    array[i] = smart_cast <DATA*> (ptr->data);
  }
}

expr* MakeStatementBlock(parser_list* stmts)
{
  if (0==stmts)  return 0;
  int length = CircularLength(stmts);
  if (1==length) {
    expr* foo = smart_cast <expr*> (stmts->data);
    RecycleCircular(stmts);
    return foo;
  }
  // build block statement
  expr** opnds = new expr*[length];
  CopyCircular(stmts, opnds, length);
  RecycleCircular(stmts);
  return em->makeAssocOp(
      Filename(), Linenumber(), exprman::aop_semi, opnds, 0, length
  );
}


bool BadIteratorList(char* n, parser_list* list)
{
  // Check for matching dimensions
  int length = CircularLength(list);
  int dimension = Iterators->NumSymbols();
  if (length != dimension) {
     if (pm->startError()) {
      pm->cerr() << "Dimension of array " << n << " does not match iterators";
      pm->stopError();
    }
    DeleteCircular(list);
    free(n);
    return true;
  }
  // Check for matching iterator names
  for (int i=0; i<dimension; i++) {
    list = list->next;
    symbol* nth = Iterators->GetItem(i);
    DCASSERT(nth);
    shared_string* forml = smart_cast <shared_string*> (list->data);
    DCASSERT(forml);
    if (strcmp(nth->Name(), forml->getStr())) {
      if (pm->startError()) {
        pm->cerr() << "Array " << n << " expecting index ";
        pm->cerr() << nth->Name() << ", got " << forml->getStr();
        pm->stopError();
      }
      DeleteCircular(list);
      free(n);
      return true;
    }
  }
  // iterator names match the ones in the list.
  DeleteCircular(list);
  return false;
}

// ******************************************************************
// *                                                                *
// *                     List-related functions                     *
// *                                                                *
// ******************************************************************

// --------------------------------------------------------------
parser_list* AppendStatement(parser_list* list, expr* s)
{
  if (0==s || em->isError(s))  return list;

  // Do we need to save the statement, or can we just execute it?
  if (!WithinBlock()) {
    traverse_data x(traverse_data::Compute);
    result answer(0L);
    x.answer = &answer;
    s->Compute(x);
    Delete(s);
    return list;
  }

  // Save the statement
  return AppendCircular(list, s);
}

// --------------------------------------------------------------
parser_list* AppendExpression(int behv, parser_list* list, expr* item)
{
  switch (behv) {
    case 0:  // add regardless.
        return AppendCircular(list, item);

    case 1:  // add, unless item is null or error
        if ((item!= 0) && (! em->isError(item)))
          return AppendCircular(list, item);
        return list;
  
    case 2:  // collapse on null or error.
        if (0==list)
          return AppendCircular(0, item);  // correct regardless
        if ( (0==item) || em->isError(item) ) {
          // collapse the list
          DeleteCircular(list);
          return AppendCircular(0, item);
        }
        if ( (0==list->data) || (em->isError((expr*)list->data)) ) {
          // list is already collapsed
          Delete(item);
          return list;
        }
        return AppendCircular(list, item);
  } // switch
  return 0;
}

// --------------------------------------------------------------
parser_list* AppendName(parser_list* list, char* ident)
{
  shared_string* i = new shared_string(ident);
  return AppendCircular(list, i);
}

// --------------------------------------------------------------
parser_list* AppendFormal(parser_list* list, symbol* p)
{
  if (0==p || em->isError(p))  return list;
  // Check for duplicate names
  int length = CircularLength(list);
  parser_list* ptr = list;
  const char* pname = p->Name();
  DCASSERT(pname);
  for (int i=0; i<length; i++) {
    ptr = ptr->next;
    symbol* fp = smart_cast <symbol*> (ptr->data);
    DCASSERT(fp);
    const char* fpname = fp->Name();
    DCASSERT(fpname);
    if (0==strcmp(pname, fpname)) {
      if (pm->startError()) {
        pm->cerr() << "Duplicate formal parameter named " << fpname;
        pm->stopError();
      }
      Delete(p);
      return list;
    }
  }

  // no duplicates, go ahead and add
  return AppendCircular(list, p);
}

// --------------------------------------------------------------
parser_list* AppendGeneric(parser_list* list, void* obj)
{
  return AppendCircular(list, (shared_object*) obj);
}

// --------------------------------------------------------------
parser_list* AppendTerm(parser_list* list, int op, expr* t)
{
  // TBD: what about null or error expressions?
  expr_term* foo = new expr_term(op, t);
  return AppendCircular(list, foo);
}


// ******************************************************************
// *                                                                *
// *                  Statement-related  functions                  *
// *                                                                *
// ******************************************************************

// --------------------------------------------------------------
expr* BuildForLoop(int count, parser_list* stmts)
{
  // Construct iterators.
  symbol** iters = 0;
  if (count>0) {
    iters = new symbol*[count];
    for (int d=count-1; d>=0; d--) {
      iters[d] = Iterators->Pop();
    };
    // check for stack underflow
    if (0==iters[0]) {
      if (pm->startInternal(__FILE__, __LINE__)) {
        pm->internal() << "Iterator stack underflow";
        pm->stopError();
      }
      for (int d=0; d<count; d++) Delete(iters[d]);
      delete[] iters;
      return 0;
    }
  }

  // Construct statement block.
  expr* block = MakeStatementBlock(stmts);

  // Construct For Loop.
  return ShowNewStatement(
      "for loop:\n", 
      em->makeForLoop(Filename(), Linenumber(), iters, count, block)
  );
}


// --------------------------------------------------------------
expr* FinishConverge(parser_list* stmts)
{
  converge_depth--;

  // Construct statement block.
  expr* block = MakeStatementBlock(stmts);

  // Construct converge statement.
  return ShowNewStatement(
      "converge:\n",
      em->makeConverge(Filename(), Linenumber(), block, (0==converge_depth))
  );
}

// --------------------------------------------------------------
expr* BuildOptionStatement(option* o, expr* v)
{
  return ShowNewStatement(
      "option statement:\n",
      em->makeOptionStatement(Filename(), Linenumber(), o, v)
  );
}

// --------------------------------------------------------------
expr* BuildOptionStatement(option* o, char* n)
{
  expr* foo;
  option_const* oc = o ? o->FindConstant(n) : 0;
  if (0==oc) {
    if (o && pm->startError()) {
      pm->cerr() << "Illegal value " << n << " for option " << o->Name();
      pm->cerr() << ", ignoring";
      pm->stopError();
    }
    foo = 0;
  } else {
    foo = em->makeOptionStatement(Filename(), Linenumber(), o, oc);
  }
  free(n);

  return ShowNewStatement("option statement:\n", foo);
}

// --------------------------------------------------------------
expr* BuildOptionStatement(option* o, bool check, parser_list* list)
{
  if (0==o) {
    DeleteCircular(list);
    return 0;
  }
  if (0==list)  return 0;

  // Build another circular list of option_consts.
  int length = CircularLength(list);
  parser_list* oclist = 0; 
  for (int i=0; i<length; i++) {
    list = list->next;
    shared_string* s = smart_cast <shared_string*> (list->data);
    const char* name = s ? s->getStr() : 0;
    option_const* oc = name ? o->FindConstant(name) : 0;
    if (oc) {
      oclist = AppendGeneric(oclist, oc);
      continue;
    }
    if (name) if (pm->startError()) {
      pm->cerr() << "Illegal value " << name << " for option " << o->Name();
      pm->cerr() << ", ignoring";
      pm->stopError();
    }
  } 
  DeleteCircular(list);

  length = CircularLength(oclist);
  if (length<1)  return 0;

  option_const** vlist = new option_const*[length];
  for (int i=0; i<length; i++) {
    oclist = oclist->next;
    vlist[i] = (option_const*) oclist->data;
  }
  RecycleCircular(oclist);

  return ShowNewStatement(
    "option statement:\n",
    em->makeOptionStatement(Filename(), Linenumber(), o, check, vlist, length)
  );
}

// --------------------------------------------------------------
expr* BuildExprStatement(expr *x)
{
  if (!em->isOrdinary(x))  return 0;
  return em->makeExprStatement(Filename(), Linenumber(), x);
}

// --------------------------------------------------------------
void StartConverge()
{
  converge_depth++;
}

// --------------------------------------------------------------
option* BuildOptionHeader(char* name)
{
  if (0==name) return 0;
  option* answer = pm ? pm->findOption(name) : 0;

  if (0==answer) if (pm->startError()) {
    pm->cerr() << "Unknown option " << name;
    pm->stopError();
  }
  free(name);
  return answer;
}

// --------------------------------------------------------------
int AddIterator(symbol* i)
{
  if (!em->isOrdinary(i))  return 0;
  symbol* find = Iterators->FindSymbol(i->Name());
  if (find) {
    if (pm->startError()) {
      pm->cerr() << "Duplicate iterator named " << i->Name();
      pm->stopError();
    }
    Delete(i);
    return 0;
  }
  Iterators->AddSymbol(i);
  return 1;
}

// --------------------------------------------------------------
expr* BuildFuncStmt(symbol* f, expr* r)
{
  DCASSERT(function_under_construction==f);
  expr* foo = DefineUserFunction(
    em, Filename(), Linenumber(), f, r, model_under_construction
  );
  function_under_construction = 0;
  return ShowNewStatement("function statement:\n", foo);
}

// --------------------------------------------------------------
bool IllegalModelVarName(char* ident, const char* what_am_i)
{
  DCASSERT(model_under_construction);
  if (model_under_construction->FindFormal(ident)) {
    if (pm->startError()) {
      pm->cerr() << "Model " << what_am_i << " ";
      pm->cerr() << ident << " has same name as parameter";
      pm->stopError();
    }
    free(ident);
    return true;
  }
  if (ModelInternal) if (ModelInternal->FindSymbol(ident)) {
    if (pm->startError()) {
      pm->cerr() << "Duplicate identifier " << ident << " within model";
      pm->stopError();
    }
    free(ident);
    return true;
  }
  return false;
}

// --------------------------------------------------------------
/** Build a measure statement.
    This handles statements of the form (within a model):
        type ident := rhs;
    The measure is added to the model's symbol tables.
      @param  typ     Type of measure.
      @param  ident   Name of measure.
      @param  rhs     Definition of measure.
      @return measure construction statement,
              or 0 on error (will make noise).
*/
expr* BuildMeasure(const type* typ, char* ident, expr* rhs)
{
  if (IllegalModelVarName(ident, "measure")) {
    Delete(rhs);
    return 0;
  }
  symbol* wrap = em->makeModelSymbol(Filename(), Linenumber(), typ, ident);

  // Add measure to symbol tables
  if (0==ModelInternal) {
    ModelInternal = MakeSymbolTable();
  }
  ModelInternal->AddSymbol(wrap);
  
  ModelExternal.Insert(wrap);

  return ShowNewStatement("measure assignment:\n",
    em->makeModelMeasureAssign(
        Filename(), Linenumber(), model_under_construction, wrap, rhs
    )
  );
}

// --------------------------------------------------------------
expr* BuildVarStmt(const type* typ, char* id, expr* ret)
{
  if (em->isError(ret)) {
    free(id);
    return 0;
  }
 
  if (WithinModel())   return BuildMeasure(typ, id, ret);

  DCASSERT(typ);
  if (! typ->canDefineVarOfThis()) {
    if (pm->startError()) {
      pm->cerr() << "Constants of type " << typ->getName();
      pm->cerr() << " are not allowed";
      pm->stopError();
    }
    free(id);
    Delete(ret);
    return 0;
  }

  symbol* find = 0;
  function* match = 0;
  // Check for functions / models with no parameters of this name
  for (find = Funcs->FindSymbol(id); find; find = find->Next()) {
    function* f = smart_cast <function*> (find);
    if (0==f)    continue;
    int score = f->TypecheckParams(0, 0);
    if (score != 0)  continue;
    match = f;
    break;
  }
  if (match) {
    if (pm->startError()) {
      pm->cerr() << "Constant declaration conflicts with existing identifier:";
      pm->newLine();
      match->PrintHeader(pm->cerr(), true);
      pm->cerr() << " declared ";
      pm->cerr().PutFile(match->Filename(), match->Linenumber());
      pm->stopError();
    }
    free(id);
    Delete(ret);
    return 0;
  }
  
  // Check that the name is unique among constants
  find = Constants->FindSymbol(id);
  if (find) {
    free(id);
    
    if (find->isDefined()) {
      if (pm->startError()) {
        pm->cerr() << "Re-definition of constant " << find->Name();
        pm->stopError();
      }
      Delete(ret);
      return 0;
    }
  } else {
    // Make the symbol
    if (WithinConverge()) {
      find = em->makeCvgVar(Filename(), Linenumber(), typ, id);
    } else {
      find = em->makeConstant(Filename(), Linenumber(), typ, id, ret, 0);
    }
    // Add to symbol table
    if (0==find)  return 0;
    Constants->AddSymbol(find);
    ShowWhatWeBuilt("variable: ", find);
  }

  if (WithinConverge()) 
    return ShowNewStatement("assignment:\n",
      em->makeCvgAssign(Filename(), Linenumber(), find, ret)
    );

  return 0;
}

// --------------------------------------------------------------
expr* BuildGuessStmt(const type* typ, char* id, expr* ret)
{
  if (em->isError(ret)) {
    free(id);
    return 0;
  }
  if (!WithinConverge()) {
    if (pm->startError()) {
      pm->cerr() << "Guess for " << id << " outside converge";
      pm->stopError();
    }
    free(id);
    Delete(ret);
    return 0;
  }
  // check that this is not already defined
  symbol* find = Constants->FindSymbol(id);
  if (find) {
    free(id);
    if (find->isGuessed()) {
      if (pm->startError()) {
        pm->cerr() << "Duplicate guess for identifier " << find->Name();
        pm->stopError();
      }
      return 0;
    }
  } else {
    find = em->makeCvgVar(Filename(), Linenumber(), typ, id);
    if (0==find)  return 0;
    Constants->AddSymbol(find);
    ShowWhatWeBuilt("variable: ", find);
  }

  return ShowNewStatement("guess:\n",
    em->makeCvgGuess(Filename(), Linenumber(), find, ret)
  );
}

// --------------------------------------------------------------
expr* BuildArrayStmt(symbol *a, expr *ret)
{
  if (WithinModel()) 
    return ShowNewStatement("measure array assignment:\n", 
      em->makeModelMeasureArray(Filename(), Linenumber(),
        model_under_construction, a, ret)
    );

  if (WithinConverge()) 
    return ShowNewStatement("converge array assignment:\n",
      em->makeArrayCvgAssign(Filename(), Linenumber(), a, ret)
    );

  // ordinary array
  return ShowNewStatement("array assignment:\n",
    em->makeArrayAssign(Filename(), Linenumber(), a, ret)
  );
}

// --------------------------------------------------------------
expr* BuildArrayGuess(symbol* a, expr* ret)
{
  if (0==a) {
    Delete(ret);
    return 0;
  }
  expr* stmt;
  if (WithinConverge()) {
    if (a->isGuessed()) {
      if (pm->startError()) {
        pm->cerr() << "Duplicate guess for identifier " << a->Name();
        pm->stopError();
      }
      Delete(ret);
      return 0;
    }
    stmt = em->makeArrayCvgGuess(Filename(), Linenumber(), a, ret);
  } else {
    if (pm->startError()) {
      pm->cerr() << "Guess for " << a->Name();
      pm->cerr() << " outside converge";
      pm->stopError();
    }
    stmt = 0;
    Delete(ret);
  }
  return ShowNewStatement("array guess:\n", stmt);
}


// ******************************************************************
// *                                                                *
// *                    Symbol-related functions                    *
// *                                                                *
// ******************************************************************

// --------------------------------------------------------------
symbol* BuildIterator(const type* typ, char* n, expr* values)
{
  return ShowWhatWeBuilt("iterator: ",
    em->makeIterator(Filename(), Linenumber(), typ, n, values)
   );
}

// --------------------------------------------------------------
void DoneWithFunctionHeader()
{
  function_under_construction = 0;
}



// Helper for BuildFunction
// --------------------------------------------------------------
function* findFirstMatch(symbol_table* st, char* n, expr** pass, int nfp)
{
  if (0==st) return 0;
  for (symbol* find = st->FindSymbol(n); find; find = find->Next()) {
    function* f = smart_cast <function*> (find);
    if (0==f)    continue;
    int score = f->TypecheckParams(pass, nfp);
    if (score != 0)  continue;
    return f;
  }
  return 0;
}

// Helper for BuildFunction
// --------------------------------------------------------------
void duplicationError(bool warning_only, function* f, const char* how)
{
  if (0==f) return;
  if (warning_only) {
    if (!pm->startWarning()) return;
  } else {
    if (!pm->startError()) return;
  }
  OutputStream &s = warning_only ? pm->warn() : pm->cerr();
  s << "Function declaration " << how << " existing identifier:";
  pm->newLine();
  f->PrintHeader(s, true);
  pm->newLine();
  s << "declared ";
  s.PutFile(f->Filename(), f->Linenumber());
  pm->stopError();
}

// --------------------------------------------------------------
symbol* BuildFunction(const type* typ, char* n, parser_list* list)
{
  DCASSERT(0==function_under_construction);
  if (0==list) {
    free(n);
    return 0;
  }
  if (WithinFor() || WithinConverge()) {
    if (pm->startError()) {
      pm->cerr() << "Function " << n << " defined within a for/converge";
      pm->stopError();
    }
    free(n);
    DeleteCircular(list);
    return 0;
  }
  DCASSERT(typ);
  if (! typ->canDefineFuncOfThis()) {
    if (pm->startError()) {
      pm->cerr() << "Functions of type " << typ->getName();
      pm->cerr() << " are not allowed";
      pm->stopError();
    }
    free(n);
    DeleteCircular(list);
    return 0;
  }

  int nfp = CircularLength(list);
  symbol** fp = new symbol*[nfp];
  CopyCircular(list, fp, nfp);
  RecycleCircular(list);

  // check for forward definitions, duplications, etc.
  function* match = findFirstMatch(
    (model_under_construction) ? ModelInternal : Funcs, n, (expr**) fp, nfp
  );
  if (match) {
    if (!match->HeadersMatch(typ, fp, nfp)) {
      duplicationError(0, match, "conflicts with");
      match = 0;
    }
    free(n);
    ResetUserFunctionParams(em, Filename(), Linenumber(), match, fp, nfp);
    function_under_construction = match;
    return match;
  } 
  if (model_under_construction) {
    // warn if we are hiding a "global" function
    duplicationError(1, findFirstMatch(Funcs, n, (expr**) fp, nfp), "hides");
  } 

  // This is a brand-new function.
  function_under_construction = MakeUserFunction(
      em, Filename(), Linenumber(), typ, n, fp, nfp, model_under_construction
  );

  if (model_under_construction) {
    if (0==ModelInternal) ModelInternal = MakeSymbolTable();
    ModelInternal->AddSymbol(function_under_construction);
  } else {
    Funcs->AddSymbol(function_under_construction);
  }
  return ShowWhatWeBuilt("function: ", function_under_construction);
}

// --------------------------------------------------------------
symbol* BuildMeasureArray(const type* typ, char* n, parser_list* list)
{
  if (IllegalModelVarName(n, "measure")) {
    DeleteCircular(list);
    return 0;
  }

  if (BadIteratorList(n, list))  return 0;

  // Build a new array.  First, construct the iterator list.
  int dim = Iterators->NumSymbols();
  symbol** indexes = new symbol*[dim];
  for (int i=0; i<dim; i++) {
    indexes[i] = Share(Iterators->GetItem(i));
  }
  symbol* wrap = em->makeModelArray(
      Filename(), Linenumber(), typ, n, indexes, dim
  );

  // Add measure to symbol tables
  if (0==ModelInternal) {
    ModelInternal = MakeSymbolTable();
  }
  ModelInternal->AddSymbol(wrap);
  
  ModelExternal.Insert(wrap);

  return ShowWhatWeBuilt("measure array: ", wrap);
}

// --------------------------------------------------------------
symbol* BuildArray(const type* typ, char* n, parser_list* list)
{
  if (0==list) { // can this happen?
    free(n);
    return 0;
  }

  if (WithinModel())   return BuildMeasureArray(typ, n, list);

  // Check if the array already exists
  symbol* find = Arrays->FindSymbol(n);
  if (find) {
    if (find->isDefined()) {
      if (pm->startError()) {
        pm->cerr() << "Array " << n << " already defined";
        pm->stopError();
      }
      DeleteCircular(list);
      free(n);
      return 0;
    }
  }

  if (BadIteratorList(n, list))  return 0;

  // If existing array, return it.
  if (find)  {
    free(n);
    return find;
  }

  // Build a new array.  First, construct the iterator list.
  int dim = Iterators->NumSymbols();
  symbol** indexes = new symbol*[dim];
  for (int i=0; i<dim; i++) {
    indexes[i] = Share(Iterators->GetItem(i));
  }
  symbol* f = em->makeArray(Filename(), Linenumber(), typ, n, indexes, dim);
  if (f) Arrays->AddSymbol(f);
  return ShowWhatWeBuilt("array: ", f);
}

// --------------------------------------------------------------
symbol* BuildFormal(const type* typ, char* name)
{
  return MakeFormalParam(Filename(), Linenumber(), 
                          typ, name, model_under_construction);
}

// --------------------------------------------------------------
symbol* BuildFormal(const type* typ, char* name, expr* deflt)
{
  return MakeFormalParam(em, Filename(), Linenumber(), 
                          typ, name, deflt, model_under_construction);
}

// ******************************************************************
// *                                                                *
// *                    Model-related  functions                    *
// *                                                                *
// ******************************************************************

// --------------------------------------------------------------
void BuildModelStmt(symbol* m, parser_list* block)
{
  DCASSERT(m==(symbol*) model_under_construction);
  expr* stmt_block;
  if (model_under_construction) {
    stmt_block = MakeStatementBlock(block);
    int ns = ModelExternal.Length();
    ModelExternal.Sort();
    symbol** visible = ModelExternal.MakeArray();
    em->finishModelDef(model_under_construction, stmt_block, visible, ns);
    Funcs->AddSymbol(m);
  } else {
    DeleteCircular(block);
    stmt_block = 0;
  }
  ModelType = 0;
  model_under_construction = 0;
  delete ModelInternal;
  ModelInternal = 0;
  ShowWhatWeBuilt("model ", m);
}

// --------------------------------------------------------------
symbol* BuildModel(const type* typ, char* n, parser_list* list)
{
  DCASSERT(pm);
  DCASSERT(typ);
  DCASSERT(0==model_under_construction);
  if (WithinFor() || WithinConverge()) {
    if (pm->startError()) {
      pm->cerr() << "Model " << n << " defined within a for/converge";
      pm->stopError();
    }
    free(n);
    DeleteCircular(list);
    return 0;
  }
  ModelType = dynamic_cast <const formalism*> (typ);
  if (0==ModelType) {
    if (pm->startInternal(__FILE__, __LINE__)) {
      pm->internal() << "Type " << typ->getName() << " is not a formalism!";
      pm->stopError();
    }
    return 0;
  }

  int num_Formals = CircularLength(list);
  symbol** Formals = 0;
  if (num_Formals) {
    Formals = new symbol*[num_Formals];
    CopyCircular(list, Formals, num_Formals);
    RecycleCircular(list);
  }

  // check that the name is unique

  symbol* find = 0;
  if (0==num_Formals) {
    // No parameters - check "constants"
    find = Constants->FindSymbol(n);
    if (find) {
      if (pm->startError()) {
        pm->cerr() << "Model declaration conflicts with existing identifier:";
        pm->newLine();
        find->PrintType(pm->cerr());
        pm->cerr() << " " << find->Name() << " declared ";
        pm->cerr().PutFile(find->Filename(), find->Linenumber());
        pm->stopError();
      }
      free(n);
      return 0;
    }
  } 

  // Check functions and other models
  expr** pass = (expr**) Formals;
  for (find = Funcs->FindSymbol(n); find; find = find->Next()) {
    function* f = smart_cast <function*> (find);
    DCASSERT(f);
    int score = f->TypecheckParams(pass, num_Formals);
    if (score != 0)    continue;
    // perfect match, that's bad!
    if (pm->startError()) {
      pm->cerr() << "Model declaration conflicts with existing identifier:";
      pm->newLine();
      f->PrintHeader(pm->cerr(), true);
      pm->cerr() << " declared ";
      pm->cerr().PutFile(f->Filename(), f->Linenumber());
      pm->stopError();
    }
    free(n);
    for (int i=0; i<num_Formals; i++) {
      Delete(Formals[i]);
    }
    delete[] Formals;
    return 0;
  }

  model_under_construction =
    em->makeModel(Filename(), Linenumber(), typ, n, Formals, num_Formals);

  if (0==model_under_construction) {
    if (pm->startError()) {
      pm->cerr() << "Couldn't make model of type " << typ->getName();
      pm->stopError();
    }
  }

  return (symbol*) model_under_construction;
}

// --------------------------------------------------------------
expr* BuildModelVarStmt(const type* typ, parser_list* list)
{
  if (0==list)  return 0;
  if (0==typ) {
    DeleteCircular(list);
    return 0;
  }
  int numsyms = CircularLength(list);
  symbol** slist = new symbol*[numsyms];
  CopyCircular(list, slist, numsyms);
  RecycleCircular(list);
  
  expr* stmt;
  if (WithinFor()) {
    stmt = em->makeModelArrayDecs(
        Filename(), Linenumber(), model_under_construction, 
        typ, slist, numsyms
    );
  } else {
    stmt = em->makeModelVarDecs(
        Filename(), Linenumber(), model_under_construction, 
        typ, 0, slist, numsyms
    );
  }
  return ShowNewStatement("model decl:\n", stmt);
}

// --------------------------------------------------------------
parser_list* AddModelVar(parser_list* varlist, char* ident)
{
  if (0==ident)  return varlist;
  if (WithinFor()) {
    if (pm->startError()) {
      pm->cerr() << "Expecting array for model variable " << ident;
      pm->stopError();
    }
    free(ident);
    return varlist;
  }
  if (IllegalModelVarName(ident, "variable")) {
    return varlist;
  }

  symbol* ms = em->makeModelSymbol(Filename(), Linenumber(), 0, ident);
  if (0==ModelInternal) {
    ModelInternal = MakeSymbolTable();
  }
  ModelInternal->AddSymbol(ms);

  return AppendCircular(varlist, ms);
}

// --------------------------------------------------------------
parser_list* AddModelArray(parser_list* varlist, char* ident, parser_list* indexlist)
{
  if (0==ident)  {
    DeleteCircular(indexlist);
    return varlist;
  }
  if (!WithinFor()) {
    if (pm->startError()) {
      pm->cerr() << "Model array variable "<< ident <<" outside of for loop";
      pm->stopError();
    }
    free(ident);
    DeleteCircular(indexlist);
    return varlist;
  }
  if (IllegalModelVarName(ident, "array variable")) {
    return varlist;
  }
  if (BadIteratorList(ident, indexlist)) {
    return varlist;
  }

  // copy the iterator list.
  int dim = Iterators->NumSymbols();
  symbol** indexes = new symbol*[dim];
  for (int i=0; i<dim; i++) {
    indexes[i] = Share(Iterators->GetItem(i));
  }

  symbol* ms = em->makeModelArray(
      Filename(), Linenumber(), 0, ident, indexes, dim
  );
  if (0==ModelInternal) {
    ModelInternal = MakeSymbolTable();
  }
  ModelInternal->AddSymbol(ms);

  return AppendCircular(varlist, ms);
}

// ******************************************************************
// *                                                                *
// *                  Expression-related functions                  *
// *                                                                *
// ******************************************************************

// Helper for FindBest
function* scoreFuncs(symbol* find, expr** pass, int np, int &bs, bool &tie)
{
  function* best = 0;
  for (symbol* ptr = find; ptr; ptr = ptr->Next()) {
    function* f = smart_cast <function*> (ptr);
    if (0==f)  continue;
    int score = f->TypecheckParams(pass, np);
    if (score < 0)    continue;
    if (score == bs)  {
      tie = true;
      continue;
    }
    if ((bs < 0) || (score < bs)) {
      tie = false;
      best = f;
      bs = score;
    }
  }
  return best;
}

// Helper for FindBest
void showMatching(symbol* find, expr** pass, int np, int best_score)
{
  for (symbol* ptr = find; ptr; ptr = ptr->Next()) {
    function* f = smart_cast <function*> (ptr);
    if (0==f)      continue;
    int score = f->TypecheckParams(pass, np);
    if (score != best_score)  continue;
    f->PrintHeader(pm->cerr(), true);
    pm->newLine();
  } // for ptr
}

// Function/model call scoring
function* FindBest(symbol* f1, symbol* f2, expr** pass, int length, int first)
{
  if (0==f1 && 0==f2) return 0;
  const char* name = f1 ? f1->Name() : f2->Name();
  int best_score = -1;
  bool tie = false;
  function* best = scoreFuncs(f1, pass, length, best_score, tie);
  if (best_score != 0) {
    int old_best = best_score;
    function* best2 = scoreFuncs(f2, pass, length, best_score, tie);
    if (best) {
      DCASSERT(old_best>=0);
      if (best2 && old_best < best_score) best = best2;
    } else {
      best = best2;
    }
  }

  bool bailout = false;
  if (best_score < 0) {
    if (pm->startError()) {
      pm->cerr() << "No match for " << name << "(";
      for (int i=first; i<length; i++) {
        if (i>first)  pm->cerr() << ", ";
        if (pass[i])  pass[i]->PrintType(pm->cerr());
        else          pm->cerr().Put("null");
      }
      pm->cerr() << ")";
      pm->stopError();
    }
    bailout = true;
  }

  if (tie) {
    if (pm->startError()) {
      pm->cerr() << "Multiple promotions with distance " << best_score;
      pm->cerr() << " for " << name << "(";
      for (int i=first; i<length; i++) {
        if (i>first)  pm->cerr() << ", ";
        pass[i]->PrintType(pm->cerr());
      }
      pm->cerr() << ")";
      pm->newLine();
      pm->cerr() << "Possible choices:";
      pm->newLine();
      showMatching(f1, pass, length, best_score);
      showMatching(f2, pass, length, best_score);
      pm->stopError();
    }
    bailout = true; 
  }

  if (bailout) {
    for (int i=0; i<length; i++)  Delete(pass[i]);
    delete[] pass;
    return 0;
  }

  return best;
}

exprman::unary_opcode Int2Uop(int op) 
{
  switch (op) {
    case NOT:     return exprman::uop_not;
    case MINUS:   return exprman::uop_neg;
  }
  if (pm->startInternal(__FILE__, __LINE__)) {
    pm->internal() << "Operator " << TokenName(op);
    pm->internal() << " not matched to any unary operator";
    pm->stopError();
  }
  return exprman::uop_none;
}

exprman::binary_opcode Int2Bop(int op) 
{
  switch (op) {
    case IMPLIES:   return exprman::bop_implies;
    case MOD:       return exprman::bop_mod;
    case SET_DIFF:  return exprman::bop_diff;
    case EQUALS:    return exprman::bop_equals;
    case NEQUAL:    return exprman::bop_nequal;
    case GT:        return exprman::bop_gt;
    case GE:        return exprman::bop_ge;
    case LT:        return exprman::bop_lt;
    case LE:        return exprman::bop_le;
  }
  if (pm->startInternal(__FILE__, __LINE__)) {
    pm->internal() << "Operator " << TokenName(op);
    pm->internal() << " not matched to any binary operator";
    pm->stopError();
  }
  return exprman::bop_none;
}

exprman::assoc_opcode Int2Aop(int op) 
{
  switch (op) {
    case AND:     return exprman::aop_and;
    case OR:      return exprman::aop_or;
    case PLUS:    return exprman::aop_plus;
    case TIMES:   return exprman::aop_times;
    case COLON:   return exprman::aop_colon;
    case SEMI:    return exprman::aop_semi;
    case COMMA:   return exprman::aop_union;
  }
  if (pm->startInternal(__FILE__, __LINE__)) {
    pm->internal() << "Operator " << TokenName(op);
    pm->internal() << " not matched to any associative operator";
    pm->stopError();
  }
  return exprman::aop_none;
}

// --------------------------------------------------------------
expr* BuildElementSet(expr* elem)
{
  if (0==elem || em->isError(elem))  return elem;
  DCASSERT(elem->Type());
  const type* set_type = elem->Type()->getSetOfThis();
  if (0 == set_type) {
    if (pm->startError()) {
      pm->cerr() << "Sets of type ";
      elem->PrintType(pm->cerr());
      pm->cerr() << " are not allowed";
      pm->stopError();
    }
    Delete(elem);
    return em->makeError();
  }
  return ShowWhatWeBuilt("set element: ",
    em->makeTypecast(Filename(), Linenumber(), set_type, elem)
  );
}

// --------------------------------------------------------------
expr* BuildInterval(expr* start, expr* stop)
{
  return BuildInterval(start, stop, Share(ONE));
}

// --------------------------------------------------------------
expr* BuildInterval(expr* start, expr* stop, expr* inc)
{
  return ShowWhatWeBuilt("set interval: ",
    em->makeTrinaryOp(Filename(), Linenumber(), 
      exprman::top_interval, start, stop, inc
    )
  );
}

// --------------------------------------------------------------
expr* BuildSummation(parser_list* list)
{
  if (0==list)  return 0;

  int length = CircularLength(list);

  // If the list has length 1, return the term (simple and common case)
  if (1==length) {
    // common and easy case
    expr_term* et = smart_cast <expr_term*> (list->data);
    expr* foo = et ? Share(et->term) : 0;
    DeleteCircular(list);
    return foo;
  }
  
  // Fill the list of exprs and flips.
  bool* flip = new bool[length];
  expr** opnds = new expr*[length];
  bool has_null = false;
  bool has_error = false;
  int oper = 0;
  for (int i=0; i<length; i++) {
    list = list->next;
    expr_term* et = smart_cast <expr_term*> (list->data);
    if (0==et || 0==et->term)  has_null = true;
    if (has_null) {
      opnds[i] = 0;
      continue;
    }
    if (MINUS == et->op) {
      oper = PLUS;
      flip[i] = true;
    } else {
      oper = et->op;
      flip[i] = false;
    }
    opnds[i] = Share(et->term);
    if (em->isError(et->term))  has_error = true;
    if (has_error)    continue;
    if (0==i)         continue;
  } // for i

  DeleteCircular(list);
  if (has_null || has_error) {
    for (int i=0; i<length; i++)  Delete(opnds[i]);
    delete[] opnds;
    delete[] flip;
    if (has_null)  return 0;
    return em->makeError();
  }

  exprman::assoc_opcode aop = Int2Aop(oper);
  return ShowWhatWeBuilt(0, 
      em->makeAssocOp(Filename(), Linenumber(), aop, opnds, flip, length)
  );
}

// --------------------------------------------------------------
expr* BuildProduct(parser_list* list)
{
  if (0==list)  return 0;

  int length = CircularLength(list);

  // If the list has length 1, return the term (simple and common case)
  if (1==length) {
    // common and easy case
    expr_term* et = smart_cast <expr_term*> (list->data);
    expr* foo = et ? Share(et->term) : 0;
    DeleteCircular(list);
    return foo;
  }

  // Fill the list of exprs and flips.
  bool* flip = new bool[length];
  expr** opnds = new expr*[length];
  bool has_null = false;
  bool has_error = false;
  int oper = 0;
  for (int i=0; i<length; i++) {
    list = list->next;
    expr_term* et = smart_cast <expr_term*> (list->data);
    if (0==et || 0==et->term)  has_null = true;
    if (has_null) {
      opnds[i] = 0;
      continue;
    }
    if (DIVIDE == et->op) {
      oper = TIMES;
      flip[i] = true;
    } else {
      oper = et->op;
      flip[i] = false;
    }
    opnds[i] = Share(et->term);
    if (em->isError(et->term))  has_error = true;
    if (has_error)    continue;
    if (0==i)         continue;
  } // for i

  DeleteCircular(list);
  if (has_null || has_error) {
    for (int i=0; i<length; i++)  Delete(opnds[i]);
    delete[] opnds;
    delete[] flip;
    if (has_null)  return 0;
    return em->makeError();
  }

  exprman::assoc_opcode aop = Int2Aop(oper);
  return ShowWhatWeBuilt(0, 
      em->makeAssocOp(Filename(), Linenumber(), aop, opnds, flip, length)
  );
}


// --------------------------------------------------------------
expr* BuildAssociative(int op, parser_list* list)
{
  if (0==list)  return 0;
  int length = CircularLength(list);
  if (1==length) {
    expr* foo = smart_cast <expr*> (list->data);
    RecycleCircular(list);
    return foo;
  }
  // build chunk of expressions
  expr** opnds = new expr*[length];
  CopyCircular(list, opnds, length);
  RecycleCircular(list);
  exprman::assoc_opcode aop = Int2Aop(op);

  return ShowWhatWeBuilt(0,
      em->makeAssocOp(Filename(), Linenumber(), aop, opnds, 0, length)
  );
}

// --------------------------------------------------------------
expr* BuildBinary(expr* left, int op, expr* right)
{
  exprman::binary_opcode bop = Int2Bop(op);
  return em->makeBinaryOp(Filename(), Linenumber(), left, bop, right);
}

// --------------------------------------------------------------
expr* BuildUnary(int op, expr* opnd)
{
  exprman::unary_opcode uop = Int2Uop(op);
  return em->makeUnaryOp(Filename(), Linenumber(), uop, opnd);
}

// --------------------------------------------------------------
expr* BuildTypecast(const type* newtype, expr* opnd)
{
  DCASSERT(newtype);
  symbol* find = Funcs->FindSymbol(newtype->getName());
  if (0==find) return ShowWhatWeBuilt("typecast: ", 
    em->makeTypecast(Filename(), Linenumber(), newtype, opnd)
  );
  // There is a function matching the type name, use that instead!
  function* best = FindBest(find, 0, &opnd, 1, 0);
  if (0==best)  return em->makeError();
  expr** pass = new expr*[1];
  pass[0] = opnd;
  return ShowWhatWeBuilt(0,
      em->makeFunctionCall(Filename(), Linenumber(), best, pass, 1)
  );
}

// --------------------------------------------------------------
expr* MakeBoolConst(char* s)
{
  if (0==s) return 0;
  result c;
  DCASSERT(em->BOOL);
  em->BOOL->assignFromString(c, s);

  if (c.isNormal()) {
    free(s);
    return em->makeLiteral(Filename(), Linenumber(), em->BOOL, c);
  }
  if (pm->startInternal(__FILE__, __LINE__)) {
    pm->internal() << "Bad boolean constant: " << s;
    pm->stopError();
  }
  free(s);
  return em->makeError();
}

// --------------------------------------------------------------
expr* MakeIntConst(char* s)
{
  if (0==s)  return 0;
  result c;
  DCASSERT(em->INT);
  em->INT->assignFromString(c, s);
  expr* answer = 0;
  if (c.isNull()) {
    // did we overflow?  try bigints
    if (em->BIGINT) {
      em->BIGINT->assignFromString(c, s);
      answer = em->makeLiteral(Filename(), Linenumber(), em->BIGINT, c);
    }
  } else {
    answer = em->makeLiteral(Filename(), Linenumber(), em->INT, c);
  }
  free(s);
  return answer;
}

// --------------------------------------------------------------
expr* MakeRealConst(char* s)
{
  if (0==s)  return 0;
  result c;
  DCASSERT(em->REAL);
  em->REAL->assignFromString(c, s);
  expr* foo = em->makeLiteral(Filename(), Linenumber(), em->REAL, c);
  free(s);
  return foo;
}

// --------------------------------------------------------------
expr* MakeStringConst(char *s)
{
  result c;
  DCASSERT(em->STRING);
  em->STRING->assignFromString(c, s);
  expr* foo = em->makeLiteral(Filename(), Linenumber(), em->STRING, c);
  free(s);
  return foo;
}

// --------------------------------------------------------------

expr* MakeMCall(shared_object* mcall, char* m)
{
  model_call_data* mcd = dynamic_cast <model_call_data*> (mcall);
  if (0==mcd) {
    free(m);
    return em->makeError();
  }

  expr* foo = 0;
  if (mcd->model1) {
    foo = em->makeMeasureCall(Filename(), Linenumber(), 
      mcd->model1, mcd->pass, mcd->np, m);
  } else {
    foo = em->makeMeasureCall(Filename(), Linenumber(), mcd->model2, m);
  }
  free(m);
  Delete(mcall);
  return ShowWhatWeBuilt(0, foo);
}

// --------------------------------------------------------------
expr* MakeAMCall(char* n, parser_list* ind, char* m)
{
  symbol* find = 0;
  if (n) find = Arrays->FindSymbol(n);
  if (0==find) {
     if (n) if (pm->startError()) {
      pm->cerr() << "Unknown array " << n;
      pm->stopError();
    }
    free(n);
    DeleteCircular(ind);
    free(m);
    return em->makeError();
  }
  free(n);
  
  int ni = CircularLength(ind);
  DCASSERT(ni);
  expr** i = new expr*[ni];
  CopyCircular(ind, i, ni);
  RecycleCircular(ind);
  expr* foo = em->makeMeasureCall(Filename(), Linenumber(), find, i, ni, m);
  free(m);
  return ShowWhatWeBuilt(0, foo);
}

// --------------------------------------------------------------
expr* MakeMACall(shared_object* mcall, char* m, parser_list* ind)
{
  model_call_data* mcd = dynamic_cast <model_call_data*> (mcall);
  if (0==mcd) {
    free(m);
    DeleteCircular(ind);
    return em->makeError();
  }
  int length = CircularLength(ind);
  DCASSERT(length);
  expr** I = new expr*[length];
  CopyCircular(ind, I, length);
  RecycleCircular(ind);
  expr* foo = 0;
  if (mcd->model1) {
    foo = em->makeMeasureCall(Filename(), Linenumber(), 
      mcd->model1, mcd->pass, mcd->np, m, I, length);
  } else {
    foo = em->makeMeasureCall(Filename(), Linenumber(), 
      mcd->model2, m, I, length);
  }
  free(m);
  Delete(mcall);
  return ShowWhatWeBuilt(0, foo);
}

// --------------------------------------------------------------
expr* MakeAMACall(char* n, parser_list* ind, char* m, parser_list* ind2)
{
  symbol* find = 0;
  if (n) find = Arrays->FindSymbol(n);
  if (0==find) {
     if (n) if (pm->startError()) {
      pm->cerr() << "Unknown array " << n;
      pm->stopError();
    }
    free(n);
    DeleteCircular(ind);
    free(m);
    return em->makeError();
  }
  free(n);
  
  int ni = CircularLength(ind);
  DCASSERT(ni);
  expr** i = new expr*[ni];
  CopyCircular(ind, i, ni);
  RecycleCircular(ind);

  int nj = CircularLength(ind2);
  DCASSERT(nj);
  expr** j = new expr*[nj];
  CopyCircular(ind2, j, nj);
  RecycleCircular(ind2);

  expr* foo = em->makeMeasureCall(
      Filename(), Linenumber(), find, i, ni, m, j, nj
  );
  free(m);
  return ShowWhatWeBuilt(0, foo);
}

// --------------------------------------------------------------
shared_object* MakeModelCall(char* n, parser_list* list)
{
  symbol* find;
  if (0==list) {
    // try vars of type "model" first...
    find = Constants->FindSymbol(n);
    if (find) {
      free(n);
      return new model_call_data(find);
    }
  }

  find = Funcs->FindSymbol(n);
  if (0==find) {
    if (pm->startError()) {
      pm->cerr() << "Unknown model " << n;
      pm->stopError();
    }
    free(n);
    DeleteCircular(list);
    return 0;
  }
  free(n);

  // Dump parameters to an array
  int length = CircularLength(list);
  expr** pass;
  if (length) {
    pass = new expr*[length];
    CopyCircular(list, pass, length);
    RecycleCircular(list);
  } else {
    pass = 0;
  }

  function* best = FindBest(find, 0, pass, length, 0);

  if (0==best)  return 0;
  
  // make sure this is a model!
  model_def* parent = em->isAModelDef(best);
  if (0==parent) {
    if (pm->startError()) {
      if (0==length) {
        pm->cerr() << best->Name() << " is not a model";
      } else {
        pm->cerr() << "Expected model for call " << best->Name() << "(";
        for (int i=0; i<length; i++) {
          if (i)        pm->cerr() << ", ";
          if (pass[i])  pass[i]->PrintType(pm->cerr());
          else          pm->cerr().Put("null");
        }
        pm->cerr() << "), but it matches";
        pm->newLine();
        best->PrintHeader(pm->cerr(), true);
        pm->cerr() << " declared ";
        pm->cerr().PutFile(best->Filename(), best->Linenumber());
      } // length
      pm->stopError();
    } // if startError
    return 0;
  }

  return new model_call_data(parent, pass, length);
}

// --------------------------------------------------------------
expr* FindIdent(char* name)
{
  // Check for loop iterators.
  symbol* find = Iterators->FindSymbol(name);

  // Check function formal parameters, if any
  if (!find) if (function_under_construction) {
    find = function_under_construction->FindFormal(name);
  }

  // Check model formal parameters, if any
  if (!find) if (model_under_construction) {
    find = model_under_construction->FindFormal(name);
  }

  // Check model built-ins
  if (!find) if (ModelType) {
    find = ModelType->findIdentifier(name);
  }

  // Check model variables
  if (!find) if (ModelInternal) {
    find = ModelInternal->FindSymbol(name);
  }

  // Any others to check?

  // Check "constants"
  if (!find) find = Constants->FindSymbol(name);

  if (find) {
    free(name);
    return Share(find);
  }

  // Check no-parameter functions
  return BuildFunctionCall(name, 0);
}

// --------------------------------------------------------------
expr* BuildArrayCall(char* n, parser_list* ind)
{
  symbol* find = 0;
  // Check model variables
  if (ModelInternal) {
    find = ModelInternal->FindSymbol(n);
  }
  if (0==find) {
    find = Arrays->FindSymbol(n);
  }

  if (0==find) {
     if (pm->startError()) {
      pm->cerr() << "Unknown array " << n;
      pm->stopError();
    }
    free(n);
    DeleteCircular(ind);
    return em->makeError();
  }
  free(n);
  int length = CircularLength(ind);
  DCASSERT(length);
  expr** pass = new expr*[length];
  CopyCircular(ind, pass, length);
  RecycleCircular(ind);
  return ShowWhatWeBuilt(0,
    em->makeArrayCall(Filename(), Linenumber(), find, pass, length)
  );
}

// --------------------------------------------------------------
expr* BuildFunctionCall(char* n, parser_list* posparams)
{
  symbol* find = em->findFunction(ModelType, n);
  symbol* find2 = 0;
  bool first = 0;
  if (find) {
    // we have a match within a model, add model to params
    expr* passmodel = Share((expr*) model_under_construction);
    posparams = PrependCircular(posparams, passmodel);
    first = 1;
  } else {
    find = ModelInternal ? ModelInternal->FindSymbol(n) : 0;
    find2 = Funcs->FindSymbol(n);
  }

  if (0==find && 0==find2) {
    if (pm->startError()) {
      if (posparams)  pm->cerr() << "Unknown function ";
      else            pm->cerr() << "Unknown identifier: ";
      pm->cerr() << n;
      pm->stopError();
    }
    free(n);
    DeleteCircular(posparams);
    return em->makeError();
  }
  free(n);

  // Dump parameters to an array
  int length = CircularLength(posparams);
  expr** pass;
  if (length) {
    pass = new expr*[length];
    CopyCircular(posparams, pass, length);
  } else {
    pass = 0;
  }
  RecycleCircular(posparams);

  function* best = FindBest(find, find2, pass, length, first);
  if (0==best)  return em->makeError();
  return ShowWhatWeBuilt(0,
    em->makeFunctionCall(Filename(), Linenumber(), best, pass, length)
  );
}

// --------------------------------------------------------------
expr*  Default()
{
  return em->makeDefault();
}

// ******************************************************************
// *                                                                *
// *                      Front-end  functions                      *
// *                                                                *
// ******************************************************************

void InitCompiler(parse_module* parent)
{
  pm = parent;
  em = pm ? pm->em : 0;

  // Init symbol tables
  Iterators = MakeSymbolTable();
  Arrays = MakeSymbolTable();
  Constants = MakeSymbolTable();

  // init globals and such here.
  if (em) {
    result one(1L);
    ONE = em->makeLiteral(0, -1, em->INT, one);

    result dk;
    dk.setUnknown();
    Constants->AddSymbol(
      em->makeConstant(0, -1, em->INT, strdup("DontKnow"),
          em->makeLiteral(0, -1, em->INT, dk), 0
      )
    );

    result inf;
    inf.setInfinity(1);
    Constants->AddSymbol(
      em->makeConstant(0, -1, em->INT, strdup("infinity"),
        em->makeLiteral(0, -1, em->INT, inf), 0
      )
    );
  } else {
    // no em, we can't do much, but stay alive
    ONE = 0;
  }

  option* debug = pm ? pm->findOption("Debug") : 0;
  parser_debug.Initialize(debug,
    "parser",
    "When set, very low-level parser messages are displayed.",
    false
  );
  compiler_debug.Initialize(debug,
    "compiler",
    "When set, low-level compiler messages are displayed.",
    false
  );
#ifdef PARSER_DEBUG
  parser_debug.active = true;
#endif
#ifdef COMPILE_DEBUG
  compiler_debug.active = true;
#endif

  // Compiler stats
  list_depth = 0;
}

int Compile(parse_module* parent)
{
  DCASSERT(parent);
  pm = parent;
  em = pm ? pm->em : 0;
  DCASSERT(em);
  Funcs = pm ? pm->GetBuiltins() : 0;
  bool kill_builtins = false;
  if (0==Funcs) {
    Funcs = MakeSymbolTable();
    kill_builtins = true;
  }

  int ans = yyparse();

  if (compiler_debug.startReport()) {
    compiler_debug.report() << "Done compiling\n";
    compiler_debug.report() << "List depth: " << list_depth << "\n";
    compiler_debug.stopIO();
  }

  if (kill_builtins) {
    delete Funcs;
  }
  return ans;
}



// $Id$

#include "converge.h"

//@Include: converge.h

/** @name converge.cc
    @type File
    @args \ 

   Implementation of all things converge related.

 */

//@{

option* use_current;
option* max_converge_iters;
option* converge_precision;
option* converge_precision_test;

option_const* relative;
option_const* absolute;

//#define DEBUG_CONVERGE

// ******************************************************************
// *                                                                *
// *                         cvgfunc  class                         *
// *                                                                *
// ******************************************************************

cvgfunc::cvgfunc(const char* fn, int line, type t, char* n)
 : variable(fn, line, t, n)
{
  current.Clear();
  current.setNull();  // haven't been computed yet
  hasconverged = false;
  was_updated = false;
}

void cvgfunc::Compute(compute_data &x)
{
  DCASSERT(x.answer);
  DCASSERT(0==x.aggregate);
  *x.answer = current;
}

void cvgfunc::ShowHeader(OutputStream &s) const
{
  s << GetType(Type(0)) << " " << Name();
}

Engine_type cvgfunc::GetEngine(engineinfo *e)
{
  if (e) e->setNone();
  return ENG_None;
}

expr* cvgfunc::SplitEngines(List <measure> *mlist)
{
  return Copy(this);
}

void cvgfunc::UpdateAndCheck() 
{
  if (update.isNull()) {
    // Once we're null, STAY null.
    hasconverged = true;
    was_updated = true;
    current = update;
    return;
  }
  if (current.isNull()) {
    // this can be due to no initial guess
    hasconverged = false;
  } else if (current.isInfinity() && update.isInfinity()) {
    // both infinite (means converged to stop infinite loops)
    hasconverged = (current.ivalue > 0) == (update.ivalue > 0);
  } else if (current.isInfinity() || update.isInfinity()) {
    // one is infinite
    hasconverged = false;
  } else if (current.isUnknown() || update.isUnknown()) {
    // cases that don't make sense:
    Internal.Start(__FILE__, __LINE__, Filename(), Linenumber());
    Internal << "Don't know value within a converge?";
    Internal.Stop();
  } else {
    // ok, so we should have 2 reals, check their difference

    double delta = current.rvalue - update.rvalue;
    if (converge_precision_test->GetEnum() == relative) {
      if (current.rvalue) delta /= current.rvalue;
    }
    hasconverged = (ABS(delta) < converge_precision->GetReal());
  } // monster if

  current = update;
  was_updated = true;
}

// ******************************************************************
// *                                                                *
// *                        guess_stmt class                        *
// *                                                                *
// ******************************************************************

class guess_stmt : public statement {
private:
  cvgfunc* var;
  expr* guess;
public:
  guess_stmt(const char* fn, int line, cvgfunc* var, expr* guess);
  virtual ~guess_stmt();
  virtual void Execute() { }
  virtual void InitialGuess();
  virtual void Affix();
  virtual void GuessesToDefs();
  virtual void show(OutputStream &s) const;
  virtual void showfancy(int dpth, OutputStream &s) const;
};

guess_stmt::guess_stmt(const char* fn, int line, cvgfunc* v, expr* g)
 : statement(fn, line)
{
  var = v;
  guess = g;
  DCASSERT(guess != ERROR);
}

guess_stmt::~guess_stmt()
{
  Delete(guess);
}

void guess_stmt::InitialGuess()
{
  DCASSERT(var->status != CS_Computed);
  compute_data foo;
  foo.answer = &(var->current);
  SafeCompute(guess, foo);
  var->hasconverged = false;
  var->was_updated = true;
#ifdef DEBUG_CONVERGE
  Output << "Initialized " << var << " got ";
  PrintResult(Output, REAL, var->current);
  Output << "\n";
  Output.flush();
#endif
}

void guess_stmt::Affix()
{
  var->status = CS_Computed;
}

void guess_stmt::GuessesToDefs()
{
  if (var->status == CS_HasGuess)
    var->status = CS_Defined;
}

void guess_stmt::show(OutputStream &s) const
{
  var->ShowHeader(s);
  s << " guess " << guess;
}

void guess_stmt::showfancy(int depth, OutputStream &s) const
{
  s.Pad(' ', depth);
  show(s);
  s << ";\n";
}

// ******************************************************************
// *                                                                *
// *                       assign_stmt  class                       *
// *                                                                *
// ******************************************************************

class assign_stmt : public statement {
private:
  cvgfunc* var;
  expr* rhs;
public:
  assign_stmt(const char* fn, int line, cvgfunc* var, expr* rhs);
  virtual ~assign_stmt();
  virtual void Execute();
  virtual bool HasConverged();
  virtual void Affix();
  virtual void show(OutputStream &s) const;
  virtual void showfancy(int dpth, OutputStream &s) const;
};

assign_stmt::assign_stmt(const char* fn, int line, cvgfunc* v, expr* r)
 : statement(fn, line)
{
  var = v;
  rhs = r;
#ifdef DEVELOPMENT_CODE
  DCASSERT(rhs != ERROR);
  if (rhs)
    DCASSERT(rhs->Type(0) == REAL);
#endif
}

assign_stmt::~assign_stmt()
{
  Delete(rhs);
}

void assign_stmt::Execute()
{
  DCASSERT(var->status != CS_Computed);
  compute_data foo;
  foo.answer = &(var->update);
  SafeCompute(rhs, foo);
  var->was_updated = false;
#ifdef DEBUG_CONVERGE
  Output << "Computed " << var << " got ";
  PrintResult(Output, REAL, var->current);
  Output << "\n";
  Output.flush();
#endif
  if (use_current->GetBool()) var->UpdateAndCheck();
}

bool assign_stmt::HasConverged()
{
  DCASSERT(var->status != CS_Computed);
  if (!var->was_updated) var->UpdateAndCheck();
  return var->hasconverged;
}

void assign_stmt::Affix()
{
  var->status = CS_Computed;
}

void assign_stmt::show(OutputStream &s) const
{
  var->ShowHeader(s);
  s << " := " << rhs;
}

void assign_stmt::showfancy(int depth, OutputStream &s) const
{
  s.Pad(' ', depth);
  show(s);
  s << ";\n";
}

// ******************************************************************
// *                                                                *
// *                     array_guess_stmt class                     *
// *                                                                *
// ******************************************************************

class array_guess_stmt : public statement {
private:
  array* var;
  expr* guess;
public:
  array_guess_stmt(const char* fn, int line, array* var, expr* guess);
  virtual ~array_guess_stmt();
  virtual void Execute() { }
  virtual void InitialGuess();
  virtual void Affix();
  virtual void GuessesToDefs();
  virtual void show(OutputStream &s) const;
  virtual void showfancy(int dpth, OutputStream &s) const;
};

array_guess_stmt::array_guess_stmt(const char* fn, int line, array* v, expr* g)
 : statement(fn, line)
{
  var = v;
  guess = g;
  DCASSERT(guess != ERROR);
}

array_guess_stmt::~array_guess_stmt()
{
  Delete(guess);
}

void array_guess_stmt::InitialGuess()
{
  // Get this converge function thingy
  symbol* thisvar = var->GetCurrentReturn();
  DCASSERT(NULL==thisvar);
  char* name = strdup(var->Name());
  thisvar = MakeConvergeVar(REAL, name, Filename(), Linenumber());
  var->SetCurrentReturn(thisvar);
  cvgfunc* v = smart_cast <cvgfunc*> (thisvar);
  DCASSERT(v);
  DCASSERT(v->status != CS_Computed);

  // Compute the guess  value
  compute_data foo;
  foo.answer = &(v->current);
  guess->Compute(foo);
  v->hasconverged = false;
  v->was_updated = true;
}

void array_guess_stmt::Affix()
{
  symbol* thisvar = var->GetCurrentReturn();
  cvgfunc* v = smart_cast <cvgfunc*> (thisvar);
  DCASSERT(v);
  v->status = CS_Computed;
  var->status = CS_Computed;
}

void array_guess_stmt::GuessesToDefs()
{
  if (var->status == CS_HasGuess)
    var->status = CS_Defined;
}

void array_guess_stmt::show(OutputStream &s) const
{
  s << var << " guess " << guess;
}

void array_guess_stmt::showfancy(int depth, OutputStream &s) const
{
  s.Pad(' ', depth);
  show(s);
  s << ";\n";
}

// ******************************************************************
// *                                                                *
// *                    array_assign_stmt  class                    *
// *                                                                *
// ******************************************************************

class array_assign_stmt : public statement {
private:
  array* var;
  expr* rhs;
public:
  array_assign_stmt(const char* fn, int line, array* var, expr* rhs);
  virtual ~array_assign_stmt();
  virtual void Execute();
  virtual bool HasConverged();
  virtual void Affix();
  virtual void show(OutputStream &s) const;
  virtual void showfancy(int dpth, OutputStream &s) const;
};

array_assign_stmt::array_assign_stmt(const char* fn, int ln, array* v, expr* r)
 : statement(fn, ln)
{
  var = v;
  rhs = r;
  DCASSERT(rhs != ERROR);
  DCASSERT(rhs);
  DCASSERT(rhs->Type(0) == REAL);
}

array_assign_stmt::~array_assign_stmt()
{
  Delete(rhs);
}

void array_assign_stmt::Execute()
{
  // Get this converge function thingy
  symbol* thisvar = var->GetCurrentReturn();
  if (NULL==thisvar) {
    // function hasn't been created yet, make one
    char* name = strdup(var->Name());
    thisvar = MakeConvergeVar(REAL, name, Filename(), Linenumber());
    var->SetCurrentReturn(thisvar);
  }
  cvgfunc* v = smart_cast <cvgfunc*> (thisvar);
  DCASSERT(v);
  DCASSERT(v->status != CS_Computed);

  // Compute the new value
  compute_data foo;
  foo.answer = &(v->update);
  rhs->Compute(foo);
  v->was_updated = false;
  if (use_current->GetBool()) v->UpdateAndCheck();
}

bool array_assign_stmt::HasConverged()
{
  symbol* thisvar = var->GetCurrentReturn();
  cvgfunc* v = smart_cast <cvgfunc*> (thisvar);
  DCASSERT(v);
  DCASSERT(v!=NULL);
  if (!v->was_updated) v->UpdateAndCheck();
  return v->hasconverged;
}

void array_assign_stmt::Affix()
{
  symbol* thisvar = var->GetCurrentReturn();
  cvgfunc* v = smart_cast <cvgfunc*> (thisvar);
  DCASSERT(v);
  v->status = CS_Computed;
  var->status = CS_Computed;
}

void array_assign_stmt::show(OutputStream &s) const
{
  s << var << " := " << rhs;
}

void array_assign_stmt::showfancy(int depth, OutputStream &s) const
{
  s.Pad(' ', depth);
  show(s);
  s << ";\n";
}

// ******************************************************************
// *                                                                *
// *                      converge_stmt  class                      *
// *                                                                *
// ******************************************************************

class converge_stmt : public statement {
  statement **block;
  int blocksize;
public:
  converge_stmt(const char* fn, int line, statement** b, int bs);
  virtual ~converge_stmt();

  virtual void Execute();
  // virtual void InitialGuess();
  // virtual bool HasConverged();
  virtual void Affix();
  virtual void show(OutputStream &s) const;
  virtual void showfancy(int depth, OutputStream &s) const;
};

converge_stmt::converge_stmt(const char* fn, int line, statement** b, int bs)
 : statement(fn, line)
{
  block = b;
  blocksize = bs;
}

converge_stmt::~converge_stmt()
{
  int j;
  for (j=0; j<blocksize; j++) delete block[j];
  delete[] block;
}

void converge_stmt::Execute()
{
  int iters, j;
  // do guesses
  for (j=0; j<blocksize; j++) block[j]->InitialGuess();
  
  for (iters=max_converge_iters->GetInt(); iters; iters--) {
    
    // execute block
    for (j=0; j<blocksize; j++) block[j]->Execute();

    // have we converged yet?
    bool hc = true;
    for (j=0; j<blocksize; j++) 
      if (!block[j]->HasConverged()) hc = false;

    if (hc) break;
  }

  if (0==iters) {
    Warning.Start(Filename(), Linenumber());
    Warning << "converge did not attain precision ";
    Warning << converge_precision->GetReal() << " after ";
    Warning << max_converge_iters->GetInt() << " iterations";
    Warning.Stop();
  }
}

void converge_stmt::Affix() 
{
  int j;
  for (j=0; j<blocksize; j++)
    block[j]->Affix();
}

void converge_stmt::show(OutputStream &s) const
{
  s << "converge";
}

void converge_stmt::showfancy(int depth, OutputStream &s) const
{
  int j;
  s.Pad(' ', depth);
  s << "converge {\n";
  for (j=0; j<blocksize; j++) {
    block[j]->showfancy(depth+2, s);
  }
  s.Pad(' ', depth);
  s << "}\n";
}


// ******************************************************************
// *                                                                *
// *                                                                *
// *                          Global stuff                          *
// *                                                                *
// *                                                                *
// ******************************************************************

cvgfunc* MakeConvergeVar(type t, char* id, const char* file, int line)
{
  DCASSERT(t==REAL);
  return new cvgfunc(file, line, t, id);
}

statement* MakeGuessStmt(cvgfunc* v, expr* g, const char* fn, int line)
{
  v->status = CS_HasGuess;
  return new guess_stmt(fn, line, v, g);
}

statement* MakeAssignStmt(cvgfunc* v, expr* r, const char* fn, int line)
{
  v->status = CS_Defined;
  return new assign_stmt(fn, line, v, r);
}

statement* MakeConverge(statement** block, int bsize, const char* fn, int ln)
{
  int i;
  for (i=0; i<bsize; i++) {
    DCASSERT(block[i]);
    block[i]->GuessesToDefs();
  }
  return new converge_stmt(fn, ln, block, bsize);
}

statement* MakeArrayCvgGuess(array* f, expr* guess, const char* fn, int ln)
{
  f->status = CS_HasGuess;
  return new array_guess_stmt(fn, ln, f, guess);
}

statement* MakeArrayCvgAssign(array* f, expr* rhs, const char* fn, int ln)
{
  f->status = CS_Defined;
  return new array_assign_stmt(fn, ln, f, rhs);
}

void InitConvergeOptions()
{
  // UseCurrent
  const char* ucdoc = "Should converge variables be updated immediately";
  use_current = MakeBoolOption("UseCurrent", ucdoc, true);
  AddOption(use_current);

  // MaxConvergeIters
  const char* mci = "Maximum number of iterations of a converge";
  max_converge_iters = MakeIntOption("MaxConvergeIters", mci, 1000, 1, 2000000000);
  AddOption(max_converge_iters);

  // ConvergePrecision
  const char* cp = "Desired precision for converge values";
  converge_precision = MakeRealOption("ConvergePrecision", cp, 1e-5, 1e-100, 1);
  AddOption(converge_precision);

  // ConvergePrecisionTest
  absolute = new option_const("ABSOLUTE", "\aUse absolute precision");
  relative = new option_const("RELATIVE", "\aUse relative precision");
  option_const** list = new option_const*[2];
  list[0] = absolute;
  list[1] = relative;

  const char* cpt = "Comparison to use for convergence test of converge values";

  converge_precision_test 
    = MakeEnumOption("ConvergePrecisionTest", cpt, list, 2, relative);
  AddOption(converge_precision_test);
}

//@}



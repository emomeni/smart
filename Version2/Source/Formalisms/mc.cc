
// $Id$

#include "mc.h"

#include "../Base/api.h"
#include "../Language/api.h"
#include "../Main/tables.h"
#include "../States/reachset.h"

#include "dsm.h"

#define DEBUG_MC

// ******************************************************************
// *                                                                *
// *                        markov_dsm  class                       *
// *                                                                *
// ******************************************************************

/** A discrete-state model (internel representation) for Markov chains.
*/
class markov_dsm : public state_model {
  char** statenames;
  int numstates;
public:
  /** Constructor.
	@param	sn	Array of state names
	@param	ns	Number of states
  */
  markov_dsm(char** sn, int ns);
  virtual ~markov_dsm();

  // required stuff:

  virtual void ShowState(OutputStream &s, const state &x);
  virtual void ShowEventName(OutputStream &s, int e);

  virtual int NumInitialStates() const;
  virtual void GetInitialState(int n, state &s) const;

  virtual expr* EnabledExpr(int e) { return NULL; } // fix later
  virtual expr* NextStateExpr(int e) { return NULL; } // fix later
  virtual expr* EventDistribution(int e) { return NULL; } // fix later

};

// ******************************************************************
// *                       markov_dsm methods                       *
// ******************************************************************

markov_dsm::markov_dsm(char** sn, int ns) : state_model(1)
{
  statenames = sn;
  numstates = ns;

  statespace = new reachset;
  statespace->CreateEnumerated(numstates);
}

markov_dsm::~markov_dsm()
{
}

void markov_dsm::ShowState(OutputStream &s, const state &x)
{
  // check state legality and range here...
  s << statenames[x.Read(0).ivalue];
}

void markov_dsm::ShowEventName(OutputStream &s, int e)
{
  DCASSERT(e < NumEvents());
  DCASSERT(e>=0);

  // Is there something better to do here?
  s << "Markov chain";
}

int markov_dsm::NumInitialStates() const
{
  // fill this in later
  return 0;
}

void markov_dsm::GetInitialState(int n, state &s) const
{
  // fill this in later
  s[0].ivalue = 0;
}

// ******************************************************************
// *                                                                *
// *                       markov_model class                       *
// *                                                                *
// ******************************************************************

/** Smart support for the Markov chain "formalism".
    I.e., front-end stuff for Markov chain formalism.
*/
class markov_model : public model {
  List <char> *statelist;
  char** statenames;
  int numstates;
public:
  markov_model(const char* fn, int line, type t, char*n, 
  		formal_param **pl, int np);

  virtual ~markov_model();

  // Required for models:

  virtual model_var* MakeModelVar(const char *fn, int l, type t, char* n);

  virtual void InitModel();
  virtual void FinalizeModel(result &);
  virtual state_model* BuildStateModel();
};

// ******************************************************************
// *                      markov_model methods                      *
// ******************************************************************

markov_model::markov_model(const char* fn, int line, type t, char*n, 
  formal_param **pl, int np) : model(fn, line, t, n, pl, np)
{
  statelist = NULL; 
  statenames = NULL;
  numstates = 0;
}

markov_model::~markov_model()
{
}

model_var* markov_model::MakeModelVar(const char *fn, int l, type t, char* n)
{
  int ndx = statelist->Length();
  statelist->Append(n);
  model_var* s = new model_var(fn, l, t, n);
  s->SetIndex(ndx);
#ifdef DEBUG_MC
  Output << "\tModel " << Name() << " created state " << n << " index " << ndx << "\n"; 
  Output.flush();
#endif
  return s;
}

void markov_model::InitModel()
{
  statelist = new List <char> (16);
  statenames = NULL;
  numstates = 0;
}

void markov_model::FinalizeModel(result &x)
{
  numstates = statelist->Length();
  statenames = statelist->MakeArray();
  delete statelist;
  statelist = NULL;
#ifdef DEBUG_MC
  Output << "\tMC " << Name() << " has " << numstates << " states\n";
  int i;
  for (i=0; i<numstates; i++) {
    Output << "\t" << statenames[i] << "\n";
  }
  Output.flush();
#endif

  x.Clear();
  x.notFreeable();
  x.other = this;
}

state_model* markov_model::BuildStateModel()
{
  return new markov_dsm(statenames, numstates);
}

// ******************************************************************
// *                                                                *
// *                      MC-specific functions                     *
// *                                                                *
// ******************************************************************


// ********************************************************
// *                         init                         *
// ********************************************************

void compute_mc_init(expr **pp, int np, result &x)
{
  DCASSERT(np>1);
  DCASSERT(pp);
  model *m = dynamic_cast<model*> (pp[0]);
  DCASSERT(m);

#ifdef DEBUG_MC
  Output << "\tInside init for model " << m << "\n";
#endif

  markov_model *mc = dynamic_cast<markov_model*>(m);
  DCASSERT(mc);

  x.Clear();
  int i;
  for (i=1; i<np; i++) {
#ifdef DEBUG_MC
    Output << "\tparameter " << i << " is " << pp[i] << "\n";
    Output << "\t state ";
#endif
    SafeCompute(pp[i], 0, x);
#ifdef DEBUG_MC
    PrintResult(Output, INT, x);
    Output << "\n\t value ";
#endif
    SafeCompute(pp[i], 1, x);
#ifdef DEBUG_MC
    PrintResult(Output, REAL, x);
    Output << "\n";
#endif
  }

#ifdef DEBUG_MC
  Output << "\tExiting init for model " << m << "\n";
  Output.flush();
#endif

  x.setNull();
}

void Add_init(type mctype, PtrTable *fns)
{
  const char* helpdoc = "Sets the initial state(s) with probabilities for a Markov Chain model.";

  formal_param **pl = new formal_param*[2];
  pl[0] = new formal_param(mctype, "m");
  type *tl = new type[2];
  tl[0] = STATE;
  tl[1] = REAL;
  pl[1] = new formal_param(tl, 2, "pair");
  internal_func *p = new internal_func(VOID, "init", 
	compute_mc_init, NULL,
	pl, 2, 1, helpdoc);  // parameter 1 repeats...
  InsertFunction(fns, p);
}


// ******************************************************************
// *                                                                *
// *                        Global front-ends                       *
// *                                                                *
// ******************************************************************

model* MakeMarkovChain(type t, char* id, formal_param **pl, int np,
			const char* fn, int line)
{
  return new markov_model(fn, line, t, id, pl, np);
}

void InitMCModelFuncs(PtrTable *t)
{
  Add_init(DTMC, t);
  Add_init(CTMC, t);
}


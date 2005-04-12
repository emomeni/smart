
// $Id$

#include "api.h"

#include "../Language/measures.h"

#include "ssgen.h"
#include "mcgen.h"
#include "ssmcgen.h"
#include "numerical.h"
#include "simul.h"

//#define DEBUG

option* SolutionType;

option_const numerical_oc("NUMERICAL", "\aGenerate and analyze underlying stochastic process");
option_const simulation_oc("SIMULATION", "\aDiscrete-event simulation");

option_const* NUMERICAL = &numerical_oc;
option_const* SIMULATION = &simulation_oc;


void BuildReachset(model *m)
{
  if ((NULL==m) || (ERROR==m)) return;
  state_model *dsm = dynamic_cast<state_model*>(m->GetModel());
  if (NULL==dsm) return;
  BuildReachset(dsm); // defined in ssgen.h
}

void BuildProcess(model *m)
{
  if ((NULL==m) || (ERROR==m)) return;
  state_model *dsm = dynamic_cast<state_model*>(m->GetModel());
  if (NULL==dsm) return;
  dsm->DetermineProcessType();

  switch (dsm->proctype) {
    case Proc_Unknown:		// this shouldn't happen
	DCASSERT(0);
	return;	

    case Proc_Error:
	return;

    case Proc_FSM:
  	if (NULL==dsm->statespace) {
    	  // build both reachability set and graph
    	  BuildReachSetAndGraph(dsm);
  	} else {
    	  BuildRG(dsm);
  	}
	return;

    case Proc_Ctmc:
  	if (NULL==dsm->statespace) {
    	  // build both reachability set and Markov chain
    	  BuildReachsetAndCTMC(dsm);
  	} else {
    	  BuildCTMC(dsm);
  	}
	return;

    default:
	Internal.Start(__FILE__, __LINE__);
	Internal << "Unhandled process type\n";
        Internal.Stop();
  }
}

/// Sets the return result for all measures to be "error"
void ErrorList(List <measure> *mlist)
{
  if (NULL==mlist) return;
  result err;
  err.Clear();
  err.setError();
  int i;
  for(i=0; i<mlist->Length(); i++) {
    measure *m = mlist->Item(i);
    DCASSERT(m);
    m->SetValue(err);
  }
}

void 	SolveSteadyInst(model *m, List <measure> *mlist)
{
#ifdef DEBUG
  Output << "Solving group of steady-state measures for model " << m << "\n";
  Output.flush();
#endif
  DCASSERT(SolutionType);
  bool aok = false;
  if (SolutionType->GetEnum()==NUMERICAL)
	aok = NumericalSteadyInst(m, mlist);
  else if (SolutionType->GetEnum()==SIMULATION)
	aok = SimulateSteadyInst(m, mlist);
  else {
	Internal.Start(__FILE__, __LINE__);
	Internal << "Bad value for option  " << SolutionType << "\n";
 	Internal.Stop();
	// won't get here
  } 
  if (!aok) ErrorList(mlist);
}

void 	SolveSteadyAcc(model *m, List <measure> *mlist)
{
#ifdef DEBUG
  Output << "Solving group of steady-state accumulated measures for model " << m << "\n";
  Output.flush();
#endif
  ErrorList(mlist);
}

void 	SolveTransientInst(model *m, List <measure> *mlist)
{
#ifdef DEBUG
  Output << "Solving group of transient measures for model " << m << "\n";
  Output.flush();
#endif
  ErrorList(mlist);
}

void 	SolveTransientAcc(model *m, List <measure> *mlist)
{
#ifdef DEBUG
  Output << "Solving group of transient accumulated measures for model " << m << "\n";
  Output.flush();
#endif
  ErrorList(mlist);
}

void 	InitEngines()
{
  // initialize "global" engine options
  option_const **enginelist = new option_const*[2];
  enginelist[0] = NUMERICAL;
  enginelist[1] = SIMULATION;
  SolutionType = MakeEnumOption("SolutionType", "Method to compute measures",
    enginelist, 2, NUMERICAL);
  AddOption(SolutionType);

  // initialize specifics
  InitNumerical();
  InitSimulation();
  InitSSGen();
  InitMCGen();
}

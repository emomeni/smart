
// $Id$

/** @name converge.h
    @type File
    @args \ 

  Everything to do with converge statements.
 
*/


#ifndef CONVERGE_H
#define CONVERGE_H

#include "variables.h"
#include "stmts.h"


/** Real variables within a converge.
    Members are public because they're used by converge statements.
 */
class cvgfunc : public constfunc {
public:
  result current;
public:
  cvgfunc(const char *fn, int line, type t, char *n);
  virtual void Compute(int i, result &x);
  virtual void ShowHeader(OutputStream &s) const;
};

// ******************************************************************
// *                                                                *
// *                                                                *
// *                          Global stuff                          *
// *                                                                *
// *                                                                *
// ******************************************************************

extern option* use_current;

cvgfunc* MakeConvergeVar(type t, char* id, const char* file, int line);

statement* MakeGuessStmt(cvgfunc* v, expr* guess, const char* file, int line);
statement* MakeAssignStmt(cvgfunc* v, expr* rhs, const char* file, int line);

#endif
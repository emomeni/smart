
// $Id$

#include "sets.h"

#include "math.h"

//@Include: sets.h

/** @name sets.cc
    @type File
    @args \ 

   Implementation of simple set stuff.

 */

//@{

#define UNION_DEBUG

option* index_precision;

// ******************************************************************
// *                                                                *
// *                        set_result class                        *
// *                                                                *
// ******************************************************************

set_result::set_result(int s)
{
  incoming = 1;
  size = s;
}

set_result::~set_result()
{
  // nothing to do
}

// ******************************************************************
// *                                                                *
// *                       int_interval class                       *
// *                                                                *
// ******************************************************************

/**  An interval set of integers.
     These have the form {start..stop..inc}.
 */
class int_interval : public set_result {
  int start, stop, inc;
public:
  int_interval(int s, int e, int i);
  virtual ~int_interval() { }
  virtual void GetElement(int n, result &x);
  virtual int IndexOf(const result &x);
  virtual void GetOrder(int n, int &i, result &x);
  virtual void show(OutputStream &s);
};

int_interval::int_interval(int s, int e, int i) 
  : set_result(int((e-s)/i + 1))
{
  start = s;
  stop = e;
  inc = i;
}

void int_interval::GetElement(int n, result &x)
{
  DCASSERT(n>=0);
  DCASSERT(n<Size());
  x.Clear();
  x.ivalue = start + n * inc;
}

int int_interval::IndexOf(const result &x)
{
  if (x.error || x.null || x.infinity) return -1;
  int i = int((x.ivalue-start)/inc);
  if (i>=Size()) return -1;
  if (i<0) return -1;
  return i;
}

void int_interval::GetOrder(int n, int &i, result &x)
{
  DCASSERT(n>=0);
  DCASSERT(n<Size());
  x.Clear();
  if (inc>0) i = n;
  else i = Size()-n-1;
  x.ivalue = start + i * inc;
}

void int_interval::show(OutputStream &s)
{
  s << "{" << start << ".." << stop << ".." << inc << "}";
}

// ******************************************************************
// *                                                                *
// *                      real_interval  class                      *
// *                                                                *
// ******************************************************************

/**  An interval set of reals.
     These have the form {start..stop..inc}.
 */
class real_interval : public set_result {
  double start, stop, inc;
public:
  real_interval(double s, double e, double i);
  virtual ~real_interval() { }
  virtual void GetElement(int n, result &x);
  virtual int IndexOf(const result &x);
  virtual void GetOrder(int n, int &i, result &x);
  virtual void show(OutputStream &s);
};

real_interval::real_interval(double s, double e, double i) 
  : set_result(int((e-s)/i + 1))
{
  start = s;
  stop = e;
  inc = i;
}

void real_interval::GetElement(int n, result &x)
{
  DCASSERT(n>=0);
  DCASSERT(n<Size());
  x.Clear();
  x.rvalue = start + n * inc;
}

int real_interval::IndexOf(const result &x)
{
  if (x.error || x.null || x.infinity) return -1;
  int i;
  if (inc>0) 
    i = int( ceil((x.rvalue-start)/inc - 0.5) );
  else
    i = int( ceil((start-x.rvalue)/inc - 0.5) );
  if (i>=Size()) return -1;
  if (i<0) return -1;
  // i is the closest index.
  // make sure start + i*inc is close enough to x
  double delta = fabs( (start + i*inc) - x.rvalue );
  if (delta > index_precision->GetReal()) 
    return -1;  // too far away
  return i;
}

void real_interval::GetOrder(int n, int &i, result &x)
{
  DCASSERT(n>=0);
  DCASSERT(n<Size());
  x.Clear();
  if (inc>0) i = n;
  else i = Size()-n-1;
  x.rvalue = start + i * inc;
}

void real_interval::show(OutputStream &s)
{
  s << "{" << start << ".." << stop << ".." << inc << "}";
}

// ******************************************************************
// *                                                                *
// *                     generic_int_set  class                     *
// *                                                                *
// ******************************************************************

/**  A generic set of integers.
     This can be used to represent the empty set,
     sets of a single element, and arbitrary sets.
 */
class generic_int_set : public set_result {
  /// Values in the set.  Must be sorted.
  int* values;
  /// The order of the values.
  int* order;
public:
  generic_int_set(int s, int* v, int* o);
  virtual ~generic_int_set(); 
  virtual void GetElement(int n, result &x);
  virtual int IndexOf(const result &x);
  virtual void GetOrder(int n, int &i, result &x);
  virtual void show(OutputStream &s);
};

generic_int_set::generic_int_set(int s, int* v, int* o) : set_result(s)
{
  values = v;
  order = o;
}

generic_int_set::~generic_int_set()
{
  delete[] values;
  delete[] order;
}

void generic_int_set::GetElement(int n, result &x)
{
  DCASSERT(n>=0);
  DCASSERT(n<Size());
  x.Clear();
  x.ivalue = values[n];
}

int generic_int_set::IndexOf(const result &x)
{
  if (x.error || x.null || x.infinity) return -1;
  // binary search through values
  int low = 0, high = Size()-1;
  while (low <= high) {
    int mid = (low+high)/2;
    int i = order[mid];
    DCASSERT(i>=0);
    DCASSERT(i<Size());
    if (values[i] == x.ivalue) return i;
    if (values[i] < x.ivalue) 
      low = mid+1;
    else
      high = mid-1;
  }
  return -1;
}

void generic_int_set::GetOrder(int n, int &i, result &x)
{
  DCASSERT(n>=0);
  DCASSERT(n<Size());
  i = order[n];
  DCASSERT(i>=0);
  DCASSERT(i<Size());
  x.Clear();
  x.ivalue = values[i];
}

void generic_int_set::show(OutputStream &s)
{
  s << "{";
  int i;
  if (Size()) {
    s << values[0];
    for (i=1; i<Size(); i++) s << ", " << values[i];
  }
  s << "}";
  // temporary...
  /*
  s << "\nOrder array:\n";
  if (order) {
    s << "[" << order[0];
    int i;
    for (i=1; i<Size(); i++) s << ", " << order[i];
    s << "]\n";
  } else s << "[]\n";
  */
}


// ******************************************************************
// *                                                                *
// *                     generic_real_set class                     *
// *                                                                *
// ******************************************************************

/**  A generic set of reals.
     This can be used to represent the empty set,
     sets of a single element, and arbitrary sets.
 */
class generic_real_set : public set_result {
  /// Values in the set.  Must be sorted.
  double* values;
  /// The order of the values.
  int* order;
public:
  generic_real_set(int s, double* v, int* o);
  virtual ~generic_real_set(); 
  virtual void GetElement(int n, result &x);
  virtual int IndexOf(const result &x);
  virtual void GetOrder(int n, int &i, result &x);
  virtual void show(OutputStream &s);
};

generic_real_set::generic_real_set(int s, double* v, int* o) : set_result(s)
{
  values = v;
  order = o;
}

generic_real_set::~generic_real_set()
{
  delete[] values;
  delete[] order;
}

void generic_real_set::GetElement(int n, result &x)
{
  DCASSERT(n>=0);
  DCASSERT(n<Size());
  x.Clear();
  x.rvalue = values[n];
}

int generic_real_set::IndexOf(const result &x)
{
  if (x.error || x.null || x.infinity) return -1;
  // binary search through values
  int low = 0, high = Size()-1;
  while (low <= high) {
    int mid = (low+high)/2;
    int i = order[mid];
    DCASSERT(i>=0);
    DCASSERT(i<Size());
    if (values[i] == x.rvalue) return i;
    if (values[i] < x.rvalue) 
      low = mid+1;
    else
      high = mid-1;
  }
  return -1;
}

void generic_real_set::GetOrder(int n, int &i, result &x)
{
  DCASSERT(n>=0);
  DCASSERT(n<Size());
  i = order[n];
  DCASSERT(i>=0);
  DCASSERT(i<Size());
  x.Clear();
  x.rvalue = values[i];
}

void generic_real_set::show(OutputStream &s)
{
  s << "{";
  int i;
  if (Size()) {
    s << values[0];
    for (i=1; i<Size(); i++) s << ", " << values[i];
  }
  s << "}";
}



// ******************************************************************
// *                                                                *
// *                       int_realset  class                       *
// *                                                                *
// ******************************************************************

/**  A set of reals that was built as a set of integers.
     If we take the union of a set of integers and
     a set of reals, we wrap this around the set of integers.
     That way the types match.
 */
class int_realset : public set_result {
  set_result *intset;
public:
  int_realset(set_result *is);
  virtual ~int_realset(); 
  virtual void GetElement(int n, result &x);
  virtual int IndexOf(const result &x);
  virtual void GetOrder(int n, int &i, result &x);
  virtual void show(OutputStream &s);
};

int_realset::int_realset(set_result *is) : set_result(is->Size())
{
  intset = is;
}

int_realset::~int_realset()
{
  Delete(intset);
}

void int_realset::GetElement(int n, result &x)
{
  DCASSERT(intset);
  intset->GetElement(n, x);
  if (x.infinity || x.null || x.error) return;
  // convert to real
  x.rvalue = x.ivalue; 
}

int int_realset::IndexOf(const result &x)
{
  result y;
  y.Clear();
  y.ivalue = int(x.rvalue);
  DCASSERT(intset);
  return intset->IndexOf(y);
}

void int_realset::GetOrder(int n, int &i, result &x)
{
  DCASSERT(intset);
  intset->GetOrder(n, i, x);
  if (x.infinity || x.null || x.error) return;
  // convert to real
  x.rvalue = x.ivalue; 
}

void int_realset::show(OutputStream &s)
{
  s << intset;
}




// ******************************************************************
// ******************************************************************
// **                                                              **
// **                                                              **
// **                                                              **
// **                     Expressions for sets                     **
// **                                                              **
// **                                                              **
// **                                                              **
// ******************************************************************
// ******************************************************************






// ******************************************************************
// *                                                                *
// *                     setexpr_interval class                     *
// *                                                                *
// ******************************************************************

/** Base class for expressions to build set interval.
    Those have the form {start..stop..inc}.
    If any of those are null, then we build a null set.
 */
class setexpr_interval : public expr {
protected:
  expr* start;
  expr* stop;
  expr* inc;
public:
  setexpr_interval(const char* fn, int line, expr* s, expr* e, expr* i);
  virtual ~setexpr_interval();
  virtual expr* Substitute(int i);
  virtual int GetSymbols(int i, List <symbol> *syms=NULL);
  virtual void show(OutputStream &s) const;
protected:
  inline bool ComputeAndCheck(result &s, result &e, result &i, result &x) {
    x.Clear();
    start->Compute(0, s);
    if (s.null || s.error) {
      x = s;
      return false;
    }
    stop->Compute(0, e);
    if (e.null || e.error) {
      x = e;
      return false;
    }
    inc->Compute(0, i);
    if (i.null || i.error) {
      x = i;
      return false;
    }
    // special cases here
    if (s.infinity || e.infinity) {
      // Print error message here
      x.error = CE_Undefined;
      return false;
    } 
    return true;
  }
  virtual setexpr_interval* MakeAnother(const char* fn, int line, 
  					expr* s, expr* e, expr* i) = 0;
};

setexpr_interval::setexpr_interval(const char* f, int l, expr* s, expr* e, expr* i) : expr(f, l)
{
  start = s;
  stop = e;
  inc = i;
  DCASSERT(start);
  DCASSERT(stop);
  DCASSERT(inc);
}

setexpr_interval::~setexpr_interval()
{
  Delete(start);
  Delete(stop);
  Delete(inc);
}

expr* setexpr_interval::Substitute(int i)
{
  //  Is this even used?  Just in case...
  // 
  DCASSERT(0==i);
  DCASSERT(start);
  DCASSERT(stop);
  DCASSERT(inc);
  // keep copying to a minimum...
  expr* newstart = start->Substitute(0);
  expr* newstop = stop->Substitute(0);
  expr* newinc = inc->Substitute(0);
  // Anything change?
  if ((newstart==start) && (newstop==stop) && (newinc==inc)) {
    // Just copy ourself
    Delete(newstart);
    Delete(newstop);
    Delete(newinc);
    return Copy(this);
  }
  // something changed, make a new one
  return MakeAnother(Filename(), Linenumber(), newstart, newstop, newinc);
}

int setexpr_interval::GetSymbols(int i, List <symbol> *syms)
{
  DCASSERT(0==i);
  DCASSERT(start);
  DCASSERT(stop);
  DCASSERT(inc);
  int answer = 0;
  answer = start->GetSymbols(0, syms);
  answer += stop->GetSymbols(0, syms);
  answer += inc->GetSymbols(0, syms);
  return answer;
}

void setexpr_interval::show(OutputStream &s) const
{
  s << start << ".." << stop << ".." << inc;
}



// ******************************************************************
// *                                                                *
// *                  real_setexpr_interval  class                  *
// *                                                                *
// ******************************************************************

/** An expression to build an real set interval.
 */
class real_setexpr_interval : public setexpr_interval {
public:
  real_setexpr_interval(const char* fn, int line, expr* s, expr* e, expr* i);
  virtual type Type(int i) const;
  virtual void Compute(int i, result &x);
protected:
  virtual setexpr_interval* MakeAnother(const char* fn, int line, 
  					expr* s, expr* e, expr* i);
};

real_setexpr_interval::real_setexpr_interval(const char* f, int l, expr* s, expr* e, expr* i)
 : setexpr_interval(f, l, s, e, i)
{
  DCASSERT(s->Type(0)==REAL);
  DCASSERT(e->Type(0)==REAL);
  DCASSERT(i->Type(0)==REAL);
}

type real_setexpr_interval::Type(int i) const
{
  DCASSERT(0==i);
  return SET_REAL;
}

void real_setexpr_interval::Compute(int n, result &x)
{
  DCASSERT(0==n);
  result s,e,i;
  if (!setexpr_interval::ComputeAndCheck(s,e,i,x)) return;
  set_result *xs = NULL;
  double* values;
  int* order;
  if (i.infinity || i.rvalue==0.0) {
    // that means an interval with just the start element.
    values = new double[1];
    order = new int[1];
    values[0] = s.rvalue;
    order[0] = 0;
    xs = new generic_real_set(1, values, order);
    // print a warning here
  } else
  if (((s.rvalue > e.rvalue) && (i.rvalue>0))
     ||
     ((s.rvalue < e.rvalue) && (i.rvalue<0))) {
    // empty interval
    xs = new generic_real_set(0, NULL, NULL);
    // print a warning here...
  } else {
    // we have an ordinary interval
    xs = new real_interval(s.rvalue, e.rvalue, i.rvalue);
  }
  x.other = xs;
}

setexpr_interval* real_setexpr_interval::MakeAnother(const char* fn, int line, 
  					expr* s, expr* e, expr* i)
{
  return new real_setexpr_interval(fn, line, s, e, i);
}


// ******************************************************************
// *                                                                *
// *                   int_setexpr_interval class                   *
// *                                                                *
// ******************************************************************

/** An expression to build an integer set interval.
 */
class int_setexpr_interval : public setexpr_interval {
public:
  int_setexpr_interval(const char* fn, int line, expr* s, expr* e, expr* i);
  virtual type Type(int i) const;
  virtual void Compute(int i, result &x);
protected:
  virtual setexpr_interval* MakeAnother(const char* fn, int line, 
  					expr* s, expr* e, expr* i);
};

int_setexpr_interval::int_setexpr_interval(const char* f, int l, expr* s, expr* e, expr* i)
 : setexpr_interval(f, l, s, e, i)
{
  DCASSERT(s->Type(0)==INT);
  DCASSERT(e->Type(0)==INT);
  DCASSERT(i->Type(0)==INT);
}

type int_setexpr_interval::Type(int i) const
{
  DCASSERT(0==i);
  return SET_INT;
}

void int_setexpr_interval::Compute(int n, result &x)
{
  DCASSERT(0==n);
  result s,e,i;
  if (!setexpr_interval::ComputeAndCheck(s,e,i,x)) return;
  set_result *xs = NULL;
  int* values;
  int* order;
  if (i.infinity || i.ivalue==0) {
    // that means an interval with just the start element.
    values = new int[1];
    order = new int[1];
    values[0] = s.ivalue;
    order[0] = 0;
    xs = new generic_int_set(1, values, order);
    // print a warning here
  } else
  if (((s.ivalue > e.ivalue) && (i.ivalue>0))
     ||
     ((s.ivalue < e.ivalue) && (i.ivalue<0))) {
    // empty interval
    xs = new generic_int_set(0, NULL, NULL);
    // print a warning here...
  } else {
    // we have an ordinary interval
    xs = new int_interval(s.ivalue, e.ivalue, i.ivalue);
  }
  x.other = xs;
}

setexpr_interval* int_setexpr_interval::MakeAnother(const char* fn, int line, 
  					expr* s, expr* e, expr* i)
{
  return new int_setexpr_interval(fn, line, s, e, i);
}

// ******************************************************************
// *                                                                *
// *                      intset_element class                      *
// *                                                                *
// ******************************************************************

/** Used for a single element set.
    (Used when we make those ugly for loops.)
 */
class intset_element : public unary {
public:
  intset_element(const char* fn, int line, expr* x) 
    : unary(fn, line, x) { };
  virtual type Type(int i) const;
  virtual void Compute(int i, result &x);
  virtual void show(OutputStream &s) const;
protected:
  virtual expr* MakeAnother(expr* newopnd) {
    return new intset_element(Filename(), Linenumber(), newopnd);
  }
};

type intset_element::Type(int i) const 
{
  DCASSERT(i==0);
  return SET_INT;
}

void intset_element::Compute(int i, result &x)
{
  DCASSERT(i==0);
  DCASSERT(opnd);
  opnd->Compute(0, x);
  if (x.error || x.null) return;
  if (x.infinity) {
    x.error = CE_Undefined;  // print error message?
    return;
  }
  int* values = new int[1];
  int* order = new int[1];
  values[0] = x.ivalue;
  order[0] = 0;
   
  set_result *answer = new generic_int_set(1, values, order);
  x.other = answer;
}

void intset_element::show(OutputStream &s) const
{
  s << opnd;
}

// ******************************************************************
// *                                                                *
// *                       intset_union class                       *
// *                                                                *
// ******************************************************************

/** A binary operator to handle the union of two integer sets.
    (Used when we make those ugly for loops.)
 */
class intset_union : public binary {
public:
  intset_union(const char* fn, int line, expr* l, expr* r) 
    : binary(fn, line, l, r) { };
  virtual type Type(int i) const;
  virtual void Compute(int i, result &x);
  virtual void show(OutputStream &s) const { s << left << ", " << right; }
protected:
  virtual expr* MakeAnother(expr* newleft, expr* newright) {
    return new intset_union(Filename(), Linenumber(), newleft, newright);
  }
};

type intset_union::Type(int i) const
{
  DCASSERT(0==i);
  return SET_INT;
}

void intset_union::Compute(int i, result &x)
{
  DCASSERT(0==i);
  DCASSERT(left);
  DCASSERT(right);
  x.Clear();
  result l;
  left->Compute(0, l); 
  if (l.error || l.null) {
    x = l;
    return;
  }
  set_result *ls = (set_result*) l.other;
  result r;
  right->Compute(0, r);
  if (r.error || r.null) {
    x = r;
    Delete(ls);
    return;
  }
  set_result *rs = (set_result*) r.other;

  // mark the duplicate elements in rs.
  int *rspos = new int[rs->Size()];     // eventually... use a temp buffer
  int lp = 0, rp = 0;
  result lx, rx;
  int ordl, ordr;
  if (lp<ls->Size()) ls->GetOrder(lp, ordl, lx);
  if (rp<rs->Size()) rs->GetOrder(rp, ordr, rx);
  while (lp<ls->Size() && rp<rs->Size()) {
    if (lx.ivalue == rx.ivalue) {
      rspos[ordr] = 0;  // duplicate
      lp++;
      rp++;
      if (lp<ls->Size()) ls->GetOrder(lp, ordl, lx);
      if (rp<rs->Size()) rs->GetOrder(rp, ordr, rx);
    } else if (lx.ivalue < rx.ivalue) {
      // advance lp only
      lp++;
      if (lp<ls->Size()) ls->GetOrder(lp, ordl, lx);
    } else {
      // advance rp only
      rspos[ordr] = 1;  // not duplicate
      rp++;
      if (rp<rs->Size()) rs->GetOrder(rp, ordr, rx);
    }
  }
  // fill the rest of rspos
  while (rp<rs->Size()) {
    rspos[ordr] = 1;
    rp++;
    if (rp<rs->Size()) rs->GetOrder(rp, ordr, rx);
  }

  // we have an array of bits for non-duplicates.
  // translate that into the new positions (by summing).
  for (rp=1; rp<rs->Size(); rp++) rspos[rp] += rspos[rp-1];
  
  // rspos[rs->Size()-1]   is the number of non-duplicates in rs.
  
  int newsize = ls->Size() + rspos[rs->Size()-1];

  set_result *answer;

  if (0==newsize) {
    // left and right must be empty.
    answer = new generic_int_set(0, NULL, NULL);

  } else {
    // we have a non-trivial union.

    // Do a "mergesort"
    int* values = new int[newsize];
    int* order = new int[newsize];
    int optr = 0;
    lp = 0; rp = 0;
    if (lp<ls->Size()) ls->GetOrder(lp, ordl, lx);
    if (rp<rs->Size()) rs->GetOrder(rp, ordr, rx);
    while (lp<ls->Size() && rp<rs->Size()) {
      if (lx.ivalue == rx.ivalue) {
        // this is a duplicate
        order[optr] = ordl;
        optr++;
        values[ordl] = lx.ivalue;
        lp++;
        rp++;
        if (lp<ls->Size()) ls->GetOrder(lp, ordl, lx);
        if (rp<rs->Size()) rs->GetOrder(rp, ordr, rx);
      } else if (lx.ivalue < rx.ivalue) {
        // copy next element from left
        order[optr] = ordl;
        values[ordl] = lx.ivalue;
        optr++;
        lp++;
        if (lp<ls->Size()) ls->GetOrder(lp, ordl, lx);
      } else {
        // copy next element from right
        order[optr] = rspos[ordr] + ls->Size() - 1; // I think this works...
        values[order[optr]] = rx.ivalue;
        optr++;
        rp++;
        if (rp<rs->Size()) rs->GetOrder(rp, ordr, rx);
      }
    }
    // At most one of these loops will go
    while (lp<ls->Size()) {
      order[optr] = ordl;
      values[ordl] = lx.ivalue;
      optr++;
      lp++;
      if (lp<ls->Size()) ls->GetOrder(lp, ordl, lx);
    }
    while (rp<rs->Size()) {
      order[optr] = rspos[ordr] + ls->Size() - 1;
      values[order[optr]] = rx.ivalue;
      optr++;
      rp++;
      if (rp<rs->Size()) rs->GetOrder(rp, ordr, rx);
    }

    answer = new generic_int_set(newsize, values, order);
    
    // eventually... return buffer
    delete[] rspos;
  }

#ifdef UNION_DEBUG
  Output << "Inside set union\n";
  Output << "The union of sets " << ls << " and " << rs << " is " << answer << "\n";
#endif
  
  x.other = answer;
  Delete(ls);
  Delete(rs);
}


// ******************************************************************
// *                                                                *
// *                       int2realset  class                       *
// *                                                                *
// ******************************************************************

/** A typecast expression from int sets to real sets.
 */
class int2realset : public unary {
public:
  int2realset(const char* fn, int line, expr* x) 
    : unary(fn, line, x) { };
  virtual type Type(int i) const;
  virtual void Compute(int i, result &x);
  virtual void show(OutputStream &s) const { s << opnd; }
protected:
  virtual expr* MakeAnother(expr* newx) {
    return new int2realset(Filename(), Linenumber(), newx);
  }
};

type int2realset::Type(int i) const
{
  DCASSERT(0==i);
  return SET_REAL;
}

void int2realset::Compute(int i, result &x)
{
  DCASSERT(0==i);
  DCASSERT(opnd);
  opnd->Compute(0, x);
  if (x.null || x.error) return;
  set_result* xs = (set_result*) x.other;
  set_result* answer = new int_realset(xs);
  x.other = answer;
}

// ******************************************************************
// *                                                                *
// *                                                                *
// *           Global functions  to build set expressions           *
// *                                                                *
// *                                                                *
// ******************************************************************

expr*  MakeInterval(const char *fn, int ln, expr* start, expr* stop, expr* inc)
{
  switch (start->Type(0)) {
    case INT: 
      return new int_setexpr_interval(fn, ln, start, stop, inc);

    case REAL:
      // not done yet
      return new real_setexpr_interval(fn, ln, start, stop, inc);
  }
  return NULL;
}

expr*  MakeElementSet(const char *fn, int ln, expr* element)
{
  switch (element->Type(0)) {
    case INT:
      return new intset_element(fn, ln, element);

    case REAL:
      // not done yet
      return NULL;
  }
  return NULL;
}

expr*  MakeUnionOp(const char *fn, int ln, expr* left, expr* right)
{
  switch (left->Type(0)) {
    case SET_INT:
      return new intset_union(fn, ln, left, right);

    case SET_REAL:
      // not done yet
      return NULL;
  }
  return NULL;
}

expr*  MakeInt2RealSet(const char* fn, int line, expr* intset)
{
  DCASSERT(intset->Type(0) == SET_INT);
  return new int2realset(fn, line, intset);
}

//@}


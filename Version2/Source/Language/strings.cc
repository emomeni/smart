
// $Id$

#include "strings.h"
#include "operators.h"

void PrintString(const result& x, OutputStream &out)
{
  const char* s = (char*)x.other;
  int stlen = strlen(s);
  int i;
  for (i=0; i<stlen; i++) {
    if (s[i]!='\\') {
      out << s[i];
      continue;
    }
    // special character
    i++;
    if (i>=stlen) break;  // trailing \, ignore
    switch (s[i]) {
      case 'n'	:	out << "\n";	break;
      case 't'	:	out << "\t";	break;
      case '\\'	:	out << "\\";	break;
      case 'q'	:	out << '"';	break;
      case 'b'	:	out << "\b";	break;
      case 'a'	:	out << "\a";	break;
    }
  }
}

// ******************************************************************
// *                                                                *
// *                        string_add class                        *
// *                                                                *
// ******************************************************************

/** Addition of strings.
 */
class string_add : public addop {
  result* xop;
public:
  string_add(const char* fn, int line, expr **x, int n) 
    : addop(fn, line, x, n) { xop = new result[n]; }
  
  string_add(const char* fn, int line, expr *l, expr *r) 
    : addop(fn, line, l, r) { xop = new result[2]; }

  virtual ~string_add() { delete[] xop; }
  
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return STRING;
  }
  virtual void Compute(int i, result &x);
protected:
  virtual expr* MakeAnother(expr **x, int n) {
    return new string_add(Filename(), Linenumber(), x, n);
  }
};


void string_add::Compute(int a, result &x)
{
  DCASSERT(0==a);
  x.Clear();

  int i;
  // Clear strings
  for (i=0; i<opnd_count; i++) xop[i].canfree = false;

  // Compute strings for each operand
  int total_length = 0;
  for (i=0; i<opnd_count; i++) {
    DCASSERT(operands[i]);
    DCASSERT(operands[i]->Type(0) == STRING);
    operands[i]->Compute(0, xop[i]);
    if (xop[i].error || xop[i].null) {
      x = xop[i];
      break;
    }
    if (xop[i].other)
      total_length += strlen((char*)xop[i].other); 
  }

  if (x.error || x.null) {
    // delete strings and bail out
    for (i=0; i<opnd_count; i++) if (xop[i].canfree) free(xop[i].other); 
    return;
  }

  // concatenate all strings
  char* answer = (char*) malloc(total_length+1); // save room for terminating 0
  answer[0] = 0;
  for (i=0; i<opnd_count; i++) if (xop[i].other) {
    strcat(answer, (char*)xop[i].other);
    if (xop[i].canfree) free(xop[i].other);
  }

  x.other = answer;
  x.canfree = true;
}


// ******************************************************************
// *                                                                *
// *                       string_equal class                       *
// *                                                                *
// ******************************************************************

/** Check equality of two string expressions.
 */
class string_equal : public consteqop {
public:
  string_equal(const char* fn, int line, expr *l, expr *r)
    : consteqop(fn, line, l, r) { }
  
  virtual void Compute(int i, result &x);
protected:
  virtual expr* MakeAnother(expr *l, expr *r) {
    return new string_equal(Filename(), Linenumber(), l, r);
  }
};

void string_equal::Compute(int i, result &x)
{
  DCASSERT(0==i);
  DCASSERT(left);
  DCASSERT(right);
  result l;
  result r;
  x.Clear();
  left->Compute(0, l);
  right->Compute(0, r);

  if (l.error) {
    x.error = l.error;
  } else if (r.error) {
    x.error = r.error;
  }
  if (l.null || r.null) {
    x.null = true;
  }
  if ((!x.error) && (!x.null)) {
    x.bvalue = (0==strcmp((char*)l.other, (char*)r.other));
  } 
  if (l.canfree) free(l.other);
  if (r.canfree) free(r.other);
}

// ******************************************************************
// *                                                                *
// *                        string_neq class                        *
// *                                                                *
// ******************************************************************

/** Check inequality of two string expressions.
 */
class string_neq : public constneqop {
public:
  string_neq(const char* fn, int line, expr *l, expr *r)
    : constneqop(fn, line, l, r) { }
  
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x);
protected:
  virtual expr* MakeAnother(expr *l, expr *r) {
    return new string_neq(Filename(), Linenumber(), l, r);
  }
};

void string_neq::Compute(int i, result &x)
{
  DCASSERT(0==i);
  DCASSERT(left);
  DCASSERT(right);
  result l;
  result r;
  x.Clear();
  left->Compute(0, l);
  right->Compute(0, r);

  if (l.error) {
    x.error = l.error;
  } else if (r.error) {
    x.error = r.error;
  }
  if (l.null || r.null) {
    x.null = true;
  }
  if ((!x.error) && (!x.null)) {
    x.bvalue = (0!=strcmp((char*)l.other, (char*)r.other));
  } 
  if (l.canfree) free(l.other);
  if (r.canfree) free(r.other);
}


// ******************************************************************
// *                                                                *
// *                        string_gt  class                        *
// *                                                                *
// ******************************************************************

/** Check if one string expression is greater than another.
 */
class string_gt : public constgtop {
public:
  string_gt(const char* fn, int line, expr *l, expr *r)
    : constgtop(fn, line, l, r) { }
  
  virtual void Compute(int i, result &x);
protected:
  virtual expr* MakeAnother(expr *l, expr *r) {
    return new string_gt(Filename(), Linenumber(), l, r);
  }
};

void string_gt::Compute(int i, result &x)
{
  DCASSERT(0==i);
  DCASSERT(left);
  DCASSERT(right);
  result l;
  result r;
  x.Clear();
  left->Compute(0, l);
  right->Compute(0, r);

  if (l.error) {
    x.error = l.error;
  } else if (r.error) {
    x.error = r.error;
  }
  if (l.null || r.null) {
    x.null = true;
  }
  if ((!x.error) && (!x.null)) {
    x.bvalue = (strcmp((char*)l.other, (char*)r.other) > 0);
  } 
  if (l.canfree) free(l.other);
  if (r.canfree) free(r.other);
}

// ******************************************************************
// *                                                                *
// *                        string_ge  class                        *
// *                                                                *
// ******************************************************************

/** Check if one string expression is greater than or equal another.
 */
class string_ge : public constgeop {
public:
  string_ge(const char* fn, int line, expr *l, expr *r)
    : constgeop(fn, line, l, r) { }
  
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x);
protected:
  virtual expr* MakeAnother(expr *l, expr *r) {
    return new string_ge(Filename(), Linenumber(), l, r);
  }
};

void string_ge::Compute(int i, result &x)
{
  DCASSERT(0==i);
  DCASSERT(left);
  DCASSERT(right);
  result l;
  result r;
  x.Clear();
  left->Compute(0, l);
  right->Compute(0, r);

  if (l.error) {
    x.error = l.error;
  } else if (r.error) {
    x.error = r.error;
  }
  if (l.null || r.null) {
    x.null = true;
  }
  if ((!x.error) && (!x.null)) {
    x.bvalue = (strcmp((char*)l.other, (char*)r.other) >= 0);
  } 
  if (l.canfree) free(l.other);
  if (r.canfree) free(r.other);
}

// ******************************************************************
// *                                                                *
// *                        string_lt  class                        *
// *                                                                *
// ******************************************************************

/** Check if one string expression is less than another.
 */
class string_lt : public constltop {
public:
  string_lt(const char* fn, int line, expr *l, expr *r)
    : constltop(fn, line, l, r) { }
  
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x);
protected:
  virtual expr* MakeAnother(expr *l, expr *r) {
    return new string_lt(Filename(), Linenumber(), l, r);
  }
};

void string_lt::Compute(int i, result &x)
{
  DCASSERT(0==i);
  DCASSERT(left);
  DCASSERT(right);
  result l;
  result r;
  x.Clear();
  left->Compute(0, l);
  right->Compute(0, r);

  if (l.error) {
    x.error = l.error;
  } else if (r.error) {
    x.error = r.error;
  }
  if (l.null || r.null) {
    x.null = true;
  }
  if ((!x.error) && (!x.null)) {
    x.bvalue = (strcmp((char*)l.other, (char*)r.other) < 0);
  } 
  if (l.canfree) free(l.other);
  if (r.canfree) free(r.other);
}

// ******************************************************************
// *                                                                *
// *                        string_le  class                        *
// *                                                                *
// ******************************************************************

/** Check if one string expression is less than or equal another.
 */
class string_le : public constleop {
public:
  string_le(const char* fn, int line, expr *l, expr *r)
    : constleop(fn, line, l, r) { }
  
  virtual type Type(int i) const {
    DCASSERT(0==i);
    return BOOL;
  }
  virtual void Compute(int i, result &x);
protected:
  virtual expr* MakeAnother(expr *l, expr *r) {
    return new string_le(Filename(), Linenumber(), l, r);
  }
};

void string_le::Compute(int i, result &x)
{
  DCASSERT(0==i);
  DCASSERT(left);
  DCASSERT(right);
  result l;
  result r;
  x.Clear();
  left->Compute(0, l);
  right->Compute(0, r);

  if (l.error) {
    x.error = l.error;
  } else if (r.error) {
    x.error = r.error;
  }
  if (l.null || r.null) {
    x.null = true;
  }
  if ((!x.error) && (!x.null)) {
    x.bvalue = (strcmp((char*)l.other, (char*)r.other) <= 0);
  } 
  if (l.canfree) free(l.other);
  if (r.canfree) free(r.other);
}

// ******************************************************************
// *                                                                *
// *             Global functions  to build expressions             *
// *                                                                *
// ******************************************************************

expr* MakeStringAdd(expr** opnds, int n, const char* file, int line)
{
  return new string_add(file, line, opnds, n);
}

expr* MakeStringBinary(expr* left, int op, expr* right, const char* file, int
line)
{
  switch (op) {
    case PLUS:
      return new string_add(file, line, left, right);
    case EQUALS:
      return new string_equal(file, line, left, right);
    case NEQUAL:
      return new string_neq(file, line, left, right);
    case GT:
      return new string_gt(file, line, left, right);
    case GE:
      return new string_ge(file, line, left, right);
    case LT:
      return new string_lt(file, line, left, right);
    case LE:
      return new string_le(file, line, left, right);
  }
  return NULL;
}


// $Id$

#include "mdds.h"
#include "../Base/errors.h"

node_manager::node_manager()
{
  a_size = 256;
  addresses = (int*) malloc(a_size*sizeof(int));
  a_last = 1;
  a_unused = 0;
  // just in case
  addresses[0] = 0;
  addresses[1] = 0;

  d_size = 1024;
  data = (char*) malloc(d_size);
  d_last = 0;
  d_unused = 0;
}

node_manager::~node_manager()
{
  free(addresses);
  free(data);
}

int node_manager::TempNode(int k, int sz)
{
    int p = NextFreeNode();
    addresses[p] = FindHole(1+(4+sz)*sizeof(int));
    char* flags = data+addresses[p];
    *flags = 0;
    int* foo = (int*) (flags+1);
    foo[0] = 0; // #incoming
    foo++;
    foo[0] = 0; // #cache entries
    foo++;
    foo[0] = k; // level
    foo++;
    foo[0] = sz;
    // downward pointers
    for (; sz; sz--) {
      foo++;
      foo[0] = 0;
    }
}

inline int digits(int a) 
{
  int d = 1;
  while (a) { d++; a/=10; }
  return d;
}

void node_manager::Dump(OutputStream &s) const
{
  s << "Nodes: \n";
  int width = digits(a_last);
  int p;
  int x = a_unused;
  for (p=0; p<=a_last; p++) {
    s.Put(p, width);
    s << ": ";
    if (p<2) {
      s << "terminal\n";
      s.flush();	
      continue;
    }
    if (p==x) {
      s << "DELETED\n";
      x=addresses[x];  // next deleted
      s.flush();	
      continue;
    } 
    s << "addr " << addresses[p] << "\n";
    // show node details...
    s.flush();	
  } // for p
}

// ------------------------------------------------------------------
//  Protected methods
// ------------------------------------------------------------------

int node_manager::NextFreeNode()
{
  if (a_unused) {
    // grab a recycled index
    int p = a_unused;
    a_unused = addresses[p];
    if (0==a_unused) a_unused_tail = 0;
    return p;
  }
  // new index
  a_last++;
  if (a_last>=a_size) {
    a_size += 256;
    addresses = (int*) realloc(addresses, a_size * sizeof(int));
    if (NULL==addresses)
      OutOfMemoryError("Too many MDD nodes");
  }
  return a_last;
}

void node_manager::FreeNode(int p)
{
  DCASSERT(p>1);
  if (p==a_last) { 
    // special case
    a_last--;
    return;
  }
  if (p>a_unused_tail) {
    // Definitely the tail
    addresses[p] = 0;
    if (a_unused_tail) {
      // we're at the end of an existing list
      addresses[a_unused_tail] = p;
      a_unused_tail = p;
    } else {
      // empty list
      a_unused = a_unused_tail = p;
    }
    return;
  }
  // find spot in list
  int prev = 0;
  int next = a_unused;
  while (next && next<p) {
    prev = next;
    next = addresses[next];
  }
  addresses[p] = next;
  DCASSERT(next);  // we should have handled this case already
  if (prev) addresses[prev] = p;
  else a_unused = p;
}


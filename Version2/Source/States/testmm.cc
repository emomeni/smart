
// $Id$

#include "mdds_malloc.h"
#include "mddops_malloc.h"
#include <stdlib.h>

int* root;
int* size;
mdd_node_manager bar;
operations cruft(&bar);

//#define SHOW_MXD
//#define SHOW_FINAL

void ReadMDD(const char* filename)
{
  FILE* foo = fopen(filename, "r"); 
  if (NULL==foo) {
    Output << "Couldn't open file " << filename << "\n"; 
    Output.flush();
    return;
  }
  InputStream in(foo);

  // File format is:
  // index
  // level
  // truncated--full size
  // entry1 ... entryn

  int mapsize = 1024;
  int* indexmap = (int*) malloc(mapsize*sizeof(int));
  indexmap[0] = 0;
  indexmap[1] = 1;
  
  int index;
  int level;
  while (in.Get(index)) {
    int sz;
    in.Get(level);
    in.Get(sz);
    if (level>0) size[level] = MAX(size[level], sz);
    if (level<0) size[-level] = MAX(size[-level], sz);
    int n = bar.TempNode(level, sz);
    if (index>=mapsize) {
      mapsize = (1+index/1024)*1024;
      indexmap = (int *) realloc(indexmap, mapsize*sizeof(int));
      DCASSERT(indexmap);
    }
    // read, translate, and store the pointers
    for (int i=0; i<sz; i++) {
      int ptr;
      in.Get(ptr);
      bar.Link(indexmap[ptr]);
      bar.SetArc(n, i, indexmap[ptr]);
    }
    indexmap[index] = bar.Reduce(n);
  }
  DCASSERT(0==root[level]);
  root[level] = indexmap[index];
  free(indexmap);
}

int MakeString(int K) 
{
  int below = 1;
  for (int k=1; k<=K; k++) {
    int a = bar.TempNode(k, size[k]);
    bar.SetArc(a, 0, below);
    below = a;
  }
  return below;
}

void smart_exit()
{
}

int main(int argc, char** argv)
{
  if (argc<3) {
    Output << "Usage: " << argv[0] << " K file1 ... filen\n";
    return 0;
  }
  int K = atoi(argv[1]);
  root = new int[K+1];
  size = new int[K+1];
  int i;
  for (i=0; i<=K; i++) root[i] = size[i] = 0;
  for (i=2; i<argc; i++) {
    Output << "Reading " << argv[i] << "\n";
    Output.flush();
    ReadMDD(argv[i]);
  }

  while (1) {
    if (size[K]) break;
    if (0==K) break;
    K--;
  }
  Output << "Sizes: [";
  Output.PutArray(size+1, K);
  Output << "]\n";
  Output << "Transitions by level:\n";
  for (i=K; i; i--) 
    if (root[i]) 
      Output << "\t" << i << " : " << root[i] << "\n";

  int reachset = MakeString(K);

#ifdef SHOW_MXD
  Output << "Initial: " << reachset << "\n";
  bar.Dump(Output);
#endif

  Output << "Starting " << K << " level saturation\n"; 
  Output.flush();

  cruft.Saturate(reachset, root, size, K);  

#ifdef SHOW_FINAL
  Output << "Reachset: " << reachset << "\n";
  bar.Dump(Output);
#endif
  
  int card = cruft.Count(reachset);

  Output << card << " reachable states\n";

  // stats here

  Output.Pad('-', 60);
  Output << "\nCache performance\n";
  Output << "\tUnion cache\t" << cruft.Uhits() << " hits / " << cruft.Upings() << " pings\n";
  Output << "\tFiring cache\t" << cruft.Fhits() << " hits / " << cruft.Fpings() << " pings\n";
  return 0;
}



// $Id$

#include "flatss.h"

/** @name states.cc
    @type File
    @args \ 

  Working on a new and efficient way to store states.
    
 */

#define  DEBUG_COMPACT

//@{ 

// ==================================================================
// ||                                                              ||
// ||                     state_array  methods                     ||
// ||                                                              ||
// ==================================================================

const unsigned char frommask[8] = 
	{0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF};

const unsigned char tomask[8] = 
	{0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x80};

const unsigned int twoexp[32] = 
	{0x0001, 0x0002, 0x0004, 0x0008, 
	 0x0010, 0x0020, 0x0040, 0x0080,
	 0x0100, 0x0200, 0x0400, 0x0800,
	 0x1000, 0x2000, 0x4000, 0x8000};

const char* EncodingMethod[4] = 
	{ "deleted state", "sparse", "runlength", "full" };

const char LIST_BIT = 0;
const char RUN_BIT = 1;

const unsigned placelimit[] = { 16, 256, 65536, 16777216, 0xffffffff };
const char placebits[] = { 4, 8, 16, 24, 32 };

const unsigned tokenlimit[] = { 2, 4, 16, 256, 65536, 16777216, 0xffffffff };
const char tokenbits[] = { 1, 2, 4, 8, 16, 24, 32 };


void state_array::ReadInt(char bits, int &x)
{
  int toptr = bitptr-bits+1;
  if (toptr>=0) {
    // Fairly easy case: we don't cross a byte boundary
    CHECK_RANGE(0, byteptr, memsize);
    x = mem[byteptr] & frommask[bitptr] & tomask[toptr];
    if (toptr) {
      // we have to shift everything
      x>>=toptr;
      bitptr = toptr-1;
    } else {
      // we don't have to shift
      bitptr = 7;
      byteptr++;
    }
    return;
  }
  
  // Most general case: we cross a byte boundary.
  // First: get the rest of the bits from this byte.
  CHECK_RANGE(0, byteptr, memsize);
  x = mem[byteptr] & frommask[bitptr];
  bits -= (bitptr+1);
  bitptr = 7;
  byteptr++;
  // Get the rest of the bits
  while (bits) {
    CHECK_RANGE(0, byteptr, memsize);
    if (bits<8) {
      // We only have part of a byte left
      x<<=bits;
      toptr = bitptr-bits+1;
      x += ( mem[byteptr] & tomask[toptr] ) >> toptr;
      bitptr = toptr-1;
      return;
    }
    // we need to consume an entire byte
    x *= 256; 
    x += mem[byteptr];
    byteptr++;
    bits-=8;
  }
}

void state_array::WriteInt(char bits, int x)
{
  // clear out the rest of the bits of this byte
  CHECK_RANGE(0, byteptr, memsize);
  if (bitptr<7) {
    mem[byteptr] &= tomask[bitptr+1];
  } else {
    mem[byteptr] = 0;
  }

  int toptr = bitptr-bits+1;
  if (toptr>=0) {
    // Fairly easy case: we don't cross a byte boundary
    if (toptr) {
      // shift x
      x <<= toptr;
      mem[byteptr] |= x;
      bitptr = toptr-1;
    } else {
      mem[byteptr] |= x;
      bitptr = 7;
      byteptr++;
    }
    return;
  }

  // If we're here, we cross a byte boundary.
  
  // First: fill the rest of the bits from this byte.
  unsigned char chunk = x / twoexp[bits - (bitptr+1)];
  x %= twoexp[bits - (bitptr+1)];
  mem[byteptr] |= chunk;
  bits -= (bitptr+1);
  bitptr = 7;
  byteptr++;
  // Do the rest of the bits
  while (bits) {
    CHECK_RANGE(0, byteptr, memsize);
    mem[byteptr] = 0;
    if (bits<8) {
      // We only have part of a byte left
      x<<=(8-bits);
      mem[byteptr] = x;
      bitptr = 7-bits;
      return;
    }
    // we write an entire byte
    bits -= 8;
    if (bits) {
      chunk = x / twoexp[bits];
      x %= twoexp[bits];
      mem[byteptr] = chunk;
    } else {
      mem[byteptr] = x;
    }
    byteptr++;
  }
}

void state_array::PrintBits(OutputStream &s, int start, int stop)
{
  int i, b;
  for (i=start; i<stop; i++) {
    s << "|";
    for (b=7; b>=0; b--) {
      CHECK_RANGE(0, i, memsize);
      if (mem[i] & frommask[b] & tomask[b]) 
	s << "1"; else s << "0";
    }
  }
}

void state_array::EnlargeMem(int newsize)
{
  if (newsize<memsize) return;
  int newmemsize = memsize;
  while (newsize>=newmemsize) {
    if (newmemsize<16777216)  // less than 16Mb, double size
      newmemsize *= 2;
    else  // greater than 16Mb, add 16Mb increments 
      newmemsize += 16777216;
  }
  mem = (unsigned char*) realloc(mem, newmemsize);
  if (NULL==mem) OutOfMemoryError("state array resize");
  memsize = newmemsize;
}

state_array::state_array(bool useindices)
{
  // start with 256 bytes, in case we're a small set
  mem = (unsigned char*) malloc(256);  
  if (NULL==mem) OutOfMemoryError("new state array");
  memsize = 256;

  if (useindices) {
    mapsize = 256;
    map = (int*) malloc(mapsize * sizeof(int));
    if (NULL==map) OutOfMemoryError("new state array");
  } else {
    mapsize = 0;
    map = NULL;
  }
  bitptr = 7;
  byteptr = 0;
  numstates = 0;
  lasthandle = 0;
  firsthandle = -1;
  // stats
  encodecount[0] = encodecount[1] = encodecount[2] = encodecount[3] = 0;
}

state_array::~state_array()
{
  free(map);
  free(mem); mem = NULL;
}


void state_array::RunlengthEncode(char npbits, char tkbits, state &s)
{
  // state is not binary
  int i = 0;
  int np = s.Size();
  while (i<np) {
    if (i+1 >= np) {
     	// one last state var, treat as a list of length 1
	WriteInt(1, LIST_BIT);
	WriteInt(npbits, 1);  
	WriteInt(tkbits, s[i].ivalue);
        i++;
        continue;
    }
    if (s[i].ivalue == s[i+1].ivalue) {
 	// Start of a RUN
	WriteInt(1, RUN_BIT); 
	int j;
	for (j=i+1; j<np && s[i].ivalue == s[j].ivalue; j++) { } 
	// j is one past the end of the run
  	WriteInt(npbits, j-i);    	// run length
	WriteInt(tkbits, s[i].ivalue);	// run value
 	i = j;
     	continue;
    }
    // Must be a LIST
    WriteInt(1, LIST_BIT);	
    int j;
    // find end of list
    for (j=i+1; j<np; j++) {
      if (j+2 >= np) {
	j=np;
	break;
      } 
      if (s[j].ivalue != s[j+1].ivalue) continue;
      if (s[j].ivalue == s[j+2].ivalue) break;  // run of 3
    } // for j
    // j is one past the end of the list
    WriteInt(npbits, j-i);	// list length
    for (i; i<j; i++)
      WriteInt(tkbits, s[i].ivalue);
  } // while i < np
}

void state_array::RunlengthEncodeBinary(char npbits, state &s)
{
  // special case: state is binary
  int i = 0;
  int np = s.Size();
  // write the first bit value
  WriteInt(1, s[0].ivalue);
  while (i<np) {
    if (i+1 >= np) {
     	// one last state var, treat as a flip of length 1
	WriteInt(1, LIST_BIT);
	WriteInt(npbits, 1);  
        i++;
        continue;
    }
    if (s[i].ivalue == s[i+1].ivalue) {
 	// Start of a RUN
	WriteInt(1, RUN_BIT); 
	int j;
	for (j=i+1; j<np && s[i].ivalue == s[j].ivalue; j++) { } 
	// j is one past the end of the run
  	WriteInt(npbits, j-i);    	// run length
 	i = j;
     	continue;
    }
    // Must be a LIST, in this case, a sequence of FLIPS
    WriteInt(1, LIST_BIT);	
    int j;
    // find end of list
    for (j=i+1; j<np; j++) {
      if (j+1 >= np) {
	j=np;
	break;
      } 
      if (s[j].ivalue == s[j+1].ivalue) break;
    } // for j
    // j is one past the end of the list
    WriteInt(npbits, j-i);	// list length
    i = j;
  } // while i<np
}
 
int state_array::AddState(state &s)
{
  int answer;
  if (firsthandle<0) firsthandle = lasthandle;
  if (map) {
    if (numstates>=mapsize) {
      // Enlarge map array
      mapsize *= 2;
      map = (int*) realloc(map, mapsize * sizeof(int));
      if (NULL==map) OutOfMemoryError("too many states");
    }
    map[numstates] = lasthandle;
    answer = numstates;
  } else {
    answer = lasthandle;
  }
  // 
  // Scan state for max #tokens and number of nonzeroes 
  //
  int i;
  int np = s.Size();
  DCASSERT(np);        // can we have 0-length states?
  int nnz, maxval;
  nnz = maxval = 0;
  for (i = 0; i < np; i++) {
    // verify the state vars are legal
    DCASSERT(s[i].isNormal());
    int si = s[i].ivalue;
    maxval = MAX(maxval, si);
    if (si) nnz++;
  }
  //  
  //  Determine number of bits to use for "places"
  //
  char npselect;
  for (npselect=0; npselect<5; npselect++) 
    if (np <= placelimit[npselect]) break;
  DCASSERT(npselect<5);
  char npbits = placebits[npselect];

  //  
  //  Determine number of bits to use for "tokens"
  //
  char tkselect;
  for (tkselect=0; tkselect<7; tkselect++) 
    if (maxval < tokenlimit[tkselect]) break;
  DCASSERT(tkselect<7);
  char tkbits = tokenbits[tkselect];

  //
  //  Scan state to determine runlength stats
  //  (but only if there are more than 2 state variables)
  // 
  int runs = 0;
  int lists = 0;
  int listlengths = 0;
  if (np>2) {
    if (maxval == 1) {
      // special case: state is binary
      i = 0;
      while (i<np) {
	if (i+1 >= np) {
          // one last state var, treat as a list
   	  lists++;
          listlengths++;
          i++;
          continue;
        }
        if (s[i].ivalue == s[i+1].ivalue) {
 	  // Start of a RUN
	  runs++;
	  int runval = s[i].ivalue;
  	  do { i++; } while ((i<np) && (runval==s[i].ivalue));	
     	  continue;
        }
	// Must be a LIST, in this case, a sequence of FLIPS
 	lists++;
  	while (1) {
	  listlengths++;
	  i++;
	  if (i+1 >= np) {
	    i++;
	    listlengths++;
	    break;	
 	  } 
	  if (s[i].ivalue == s[i+1].ivalue) break;
	} // while 1
      } // while i<np
    } else {
      // state is not binary
      i = 0;
      while (i<np) {
	if (i+1 >= np) {
          // one last state var, treat as a list
   	  lists++;
          listlengths++;
          i++;
          continue;
        }
        if (s[i].ivalue == s[i+1].ivalue) {
 	  // Start of a RUN
	  runs++;
	  int runval = s[i].ivalue;
  	  do { i++; } while ((i<np) && (runval==s[i].ivalue));	
     	  continue;
        }
	// Must be a LIST
 	lists++;
  	while (1) {
	  listlengths++;
	  i++;
	  if (i+2 >= np) {
	    i+=2;
	    listlengths+=2;
	    break;	
 	  } 
	  if (s[i].ivalue != s[i+1].ivalue) continue;
	  if (s[i+1].ivalue == s[i+2].ivalue) break;  // run of 3
	} // while 1
      } // while i < np
    } // if maxval
  } else {
    // set to impossible values (on the large side)
    runs = 0;
    lists = np;
    listlengths = 2*np;
  }
  
  //
  // Count bits/bytes required total for full storage
  //
  int full_bytes = Bits2Bytes(npbits + np*tkbits);

  //
  // Count bits/bytes required total for sparse storage
  //
  int sparse_bits = (1+nnz) * npbits;
  if (tkbits>1) sparse_bits += nnz*tkbits;
  int sparse_bytes = Bits2Bytes(sparse_bits);

  // 
  // Count bits/bytes required total for runlength storage
  //
  int runlength_bits = npbits + (runs+lists) * (npbits+1);
  if (tkbits>1) runlength_bits += (runs+listlengths) * tkbits;
  else runlength_bits++;
  int runlength_bytes = Bits2Bytes(runlength_bits);

  //
  // Determine best encoding with tie preference to full / sparse
  //
  int min = MIN(MIN(sparse_bytes, full_bytes), runlength_bytes);
  char encoding;
  if (min==full_bytes) 
  	encoding = 3; 
  else if (min==sparse_bytes)
    	encoding = 1;
  else  
    	encoding = 2;
  encodecount[encoding]++;

  //
  // Make room for this state
  //
  EnlargeMem(lasthandle + 1+min);
 
  //
  // Initialize bitstream
  // 
  byteptr = lasthandle;
  bitptr = 7;

  // 
  // Write the state code
  //
  WriteInt(2, encoding);
  WriteInt(3, npselect);
  WriteInt(3, tkselect);

  // 
  // Write the state using the appropriate encoding
  //
  switch (encoding) {
    case 1: 
      // *****************	Sparse Encoding 
      WriteInt(npbits, nnz);
      for (i=0; i<np; i++) {
        int tk = s[i].ivalue;
        if (tk) {
          WriteInt(npbits, i);
          if (tkbits>1) WriteInt(tkbits, tk);
        }
      }
      break;

    case 2: 
      // *****************	Runlength Encoding 
      WriteInt(npbits, runs+lists);
      if (tkbits>1) RunlengthEncode(npbits, tkbits, s);
      else RunlengthEncodeBinary(npbits, s);
      break;

    case 3:
      // *****************	Full Encoding 
      WriteInt(npbits, (np-1));
      for (i=0; i<np; i++) 
	WriteInt(tkbits, s[i].ivalue);
    break;
  }
  
  // 
  // Get bitstream ready for next time
  // 
  if (bitptr<7) byteptr++;
  lasthandle = byteptr;
  numstates++;

#ifdef  DEBUG_COMPACT
  Output << "Added state #" << numstates-1 << " : " << s << "\n";
  int Handel = (map) ? map[numstates-1] : answer;
  Output << "handle: " << Handel << "\n";
  Output << "nexthandle: " << lasthandle << "\n";
  Output << "#places bits: " << (int) npbits;
  Output << "\t#tokens bits: " << (int) tkbits << "\n";
  Output << "Used " << EncodingMethod[encoding] << " storage\n";
  switch (encoding) {
    case 1: 
      Output << "#nonzeroes: " << nnz << "\n"; 
      break;
    case 2:
      Output << "#Runs: " << runs << "\n";
      Output << "#Lists: " << lists << "\n";
      Output << "List length: " << listlengths << "\n";
      break;
    case 3:
      Output << "#places: " << np << "\n";
      break;
  }
  Output << "Encoding:\n";
  PrintBits(Output, Handel, lasthandle);
  Output << "\n";
  Output.flush();
#endif
  
  return answer;
}

bool state_array::GetState(int Handel, state &s)
{
  //
  //  Find starting point of the state represented by Handel
  //
  int h;
  if (Handel<0) return false;  // Impossible, bail out
  if (map) {
    if (Handel>=numstates) return false;
    h = map[Handel];
  } else {
    if (Handel>=lasthandle) return false;
    h = Handel;
  }
  
  // 
  // Initialize bitstream and read state encoding method
  // 
  byteptr = h;
  bitptr = 7;
  int encselect;
  ReadInt(2, encselect);
  int npselect;
  ReadInt(3, npselect); 	// #places selector
  int tkselect;
  ReadInt(3, tkselect);  	// maxtokens selector

  // Figure out the numbers of bits used
  char npbits = placebits[npselect];
  char tkbits = tokenbits[tkselect];

  //
  // Decode the state based on the encoding method
  //
  int tk, np, nnz;
  int i, j, type;
  // stats for debugging
#ifdef DEBUG_COMPACT
  int lists, runs, listlength;
  lists = runs = listlength = 0;
#endif
  switch(encselect) {
    case 1: 
      // *****************	Sparse Encoding 
      for (i=s.Size()-1; i>=0; i--) s[i].ivalue = 0;
      ReadInt(npbits, nnz);
      for (i=0; i<nnz; i++) {
        ReadInt(npbits, np);
	if (tkbits>1) {
	  ReadInt(tkbits, tk);
	  s[np].ivalue = tk;
	} else 
	  s[np].ivalue = 1;
      }
      break;
      
    case 2: 
      // *****************	Runlength Encoding 
      ReadInt(npbits, nnz); 
      j=0;
      if (1==tkbits) ReadInt(1, tk);
      for (i=0; i<nnz; i++) {
        ReadInt(1, type);
        if (RUN_BIT == type) {
	  // RUN
#ifdef DEBUG_COMPACT
	  runs++;
#endif
          ReadInt(npbits, np);
	  if (tkbits>1) ReadInt(tkbits, tk);
          for(np; np; np--) 
	    s[j++].ivalue = tk;
	  // flip bit
	  tk = !tk;
        } else {
	  // LIST
	  ReadInt(npbits, np);
#ifdef DEBUG_COMPACT
	  lists++;
	  listlength+=np;
#endif
	  for(np; np; np--) {
	    if (tkbits>1) ReadInt(tkbits, tk);
	    s[j++].ivalue = tk;
	    if (tkbits<=1) tk = !tk;
          }
        }
      } // for i
      break;
      
    case 3:
      // *****************	Full Encoding 
      ReadInt(npbits, np);
      np++;
      for (i=0; i<np; i++) {
	ReadInt(tkbits, tk);
	s[i].ivalue = tk;
      }
    break;

    default:
      return false;  // shouldn't get here
  }

#ifdef  DEBUG_COMPACT
  Output << "Decoded state successfully?\n";
  Output << "Handle: " << h << "\n";
  Output << "Encoding:\n";
  if (bitptr<7) byteptr++;
  PrintBits(Output, h, byteptr);
  Output << "\n#places bits: " << (int) npbits;
  Output << "\t#tokens bits: " << (int) tkbits << "\n";
  Output << "Used " << EncodingMethod[encselect] << " storage\n";
  switch (encselect) {
    case 1: 
      Output << "#nonzeroes: " << nnz << "\n"; 
      break;
    case 2:
      Output << "#Runs: " << runs << "\n";
      Output << "#Lists: " << lists << "\n";
      Output << "List length: " << listlength << "\n";
      break;
    case 3:
      Output << "#places: " << np << "\n";
      break;
  }
  Output << "Got state " << s << "\n";
  Output.flush();
#endif

  return true;
}


int state_array::NextHandle(int h)
{
/*
  if (h<0) return -1;
  if (map) {
    // handles are indices
    if (h>=numstates-1) return -1;
    return h+1;
  }
  if (h>=lasthandle) return -1;
  int np, nnz, Mfv;
  int encselect;
  int npselect;
  char npbits;
  int tkselect;
  char tkbits;
#ifndef SHORT_STATES
  int dummy;
#endif
  int numbits;
  
  // here we go...
  byteptr = h;
  bitptr = 7;
  // read the state code
  ReadInt(2, encselect);
  ReadInt(3, npselect); 	// #places selector
#ifdef SHORT_STATES
  ReadInt(3, tkselect);  	// maxtokens selector
#else
  ReadInt(2, tkselect);  	// maxtokens selector
  ReadInt(1, dummy);		// extra unused bit
#endif

  // Figure out the numbers of bits
  switch(npselect) {
    case 0: npbits = 4; break;
    case 1: npbits = 8; break;
    case 2: npbits = 16; break;
    case 3: npbits = 24; break;
    case 4: npbits = 32; break;
    default:
      return -1;  // shouldn't get here
  }

  tkbits = twoexp[tkselect];

  switch(encselect) {
    case 1: 
      // *****************	Sparse Encoding 
      ReadInt(npbits, nnz);
      if (tkbits==1) tkbits=0;  // #tk not used in this case
      numbits = 8+npbits+ nnz*(npbits + tkbits);
      break;

    case 2: 
      // *****************	Inverse-Sparse Encoding 
      if (tkbits==1) tkbits=0;  // #tk not used in this case
      if (tkbits) ReadInt(tkbits, Mfv);
      ReadInt(npbits, nnz);
      numbits = 8+npbits+tkbits+ nnz*(npbits + tkbits);
      break;

    case 3:
      // *****************	Full Encoding 
      ReadInt(npbits, np);
      np++;
      numbits = 8+npbits+ np*tkbits;
      break;

    default:
      return -1;  // shouldn't get here
  }
  int answer;
  bool extrabits = (numbits % 8);
  answer = h+numbits / 8;
  if (extrabits) answer++;

#ifdef  DEBUG_COMPACT
  Output << "Determined next handle ok?\n";
  Output << "Handle: " << h << "\n";
  Output << "Encoding:\n";
  PrintBits(h, answer);
  Output << "\n#places bits: " << (int)npbits;
  Output << "\t#tokens bits: " << (int)tkbits << "\n";
  Output << "Next handle: " << answer << "\n";
#endif

  return answer;
*/
  Internal.Start(__FILE__, __LINE__);
  Internal << "Next handle not implemented yet\n";
  Internal.Stop();
  return 0;
}

void state_array::Report(OutputStream &R)
{
  R << "State array report\n";
  R << "  " << numstates << " states inserted\n";
  int i;
  for (i=0; i<4; i++) if (encodecount[i])
    R <<"  "<< encodecount[i] <<" "<< EncodingMethod[i] << " encodings\n";
  R << "State array allocated " << memsize << " bytes\n";
  R << "State array using     " << lasthandle << " bytes\n";
  if (map) {
    R << "Index map allocated " << int(mapsize*sizeof(int)) << " bytes\n";
    R << "Index map using     " << int(numstates*sizeof(int)) << " bytes\n";
  }
}

int state_array::MemUsed()
{
  int answer = memsize;
  if (map) answer += numstates * sizeof(int);
  return answer;
}


void state_array::Clear()
{
  bitptr = 7;
  byteptr = 0;
  numstates = 0;
  lasthandle = 0;
  firsthandle = -1;
  // stats
  encodecount[0] = encodecount[1] = encodecount[2] = encodecount[3] = 0;
}

//@}

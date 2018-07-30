/* "NETGEN", a netlist-specification tool for VLSI
   Copyright (C) 1989, 1990   Massimo A. Sivilotti
   Author's address: mass@csvax.cs.caltech.edu;
                     Caltech 256-80, Pasadena CA 91125.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation (any version).

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file copying.  If not, write to
the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* netcmp.c  -- graph isomorphism testing */

#include "config.h"

#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <time.h>    /* for time() as a seed for random number generator */
#include <limits.h>
#include <math.h>    /* for fabs() */

#ifdef IBMPC
#include <alloc.h>
#include <process.h>  /* for exit() */
/* #include <stdlib.h>   for rand(), srand() */
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "netcmp.h"
#include "hash.h"
#include "objlist.h"
#include "query.h"
#include "netfile.h"
#include "print.h"
#include "dbug.h"

#ifdef TCL_NETGEN
int InterruptPending = 0;
void (*oldinthandler)() = SIG_DFL;
extern Tcl_Interp *netgeninterp;
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 1
#endif

/* define the following to debug core allocation */
#undef DEBUG_ALLOC

/* define the following to try to expedite initialization of 
   data structures by allocating some extra lookup tables */
#define LOOKUP_INITIALIZATION

/*  The data-structures for NETCOMP are rather complicated:

    1) A global variable ElementClasses points to a
    linked-list of ElementClass.  Similarly, a global variable NodeClasses
    points to a linked-list of NodeClass.

    2) Each ElementClass record points to a linked list of Element.
    Each NodeClass points to a linked list of Node.
    This list represents an equivalence class of Elements/Nodes, and
    incorporates components from both graphs.  A legal class has an
    equal number of components from each graph.
*/


struct NodeList {
	struct NodeList *next;
	struct Node *node;
	struct Element *element; 
        /* pins in the same nodelist with equal 'pin_magic' are permutable */
	unsigned long pin_magic;  
};

struct Element {
	unsigned long hashval;
	short graph;  /* which graph did this element come from ? */
	struct objlist *object;  /* points to the START of the object */
	struct Element *next;
	struct ElementClass *elemclass;
	struct NodeList *nodelist;
};

struct ElementClass {
	unsigned long magic;
	struct Element *elements;
	struct ElementClass *next;
	int count;
	int legalpartition;
};

struct NodeClass {
	unsigned long magic;
	struct Node *nodes;
	struct NodeClass *next;
	int count;
	int legalpartition;
};

struct Node {
	unsigned long hashval;
	short graph;  /* which graph did this node come from ? */
	struct objlist *object;
	struct ElementList *elementlist;
	struct NodeClass *nodeclass;
	struct Node *next;
};

struct ElementList {
	struct NodeList *subelement;
	struct Node *node;
	struct ElementList *next;
};

struct Correspond {
	char *class1;
	int file1;
	char *class2;
	int file2;
	struct Correspond *next;
};

struct Permutation {
	char *model;
	char *pin1;
	char *pin2;
	struct Permutation *next;
};

struct ElementClass *ElementClasses = NULL;
struct NodeClass *NodeClasses = NULL;
struct Correspond *ClassCorrespondence = NULL;
struct Correspond *CompareQueue = NULL;
struct IgnoreList *ClassIgnore = NULL;
struct Permutation *PinPermutations = NULL;

/* global variables for return of lists from CreateLists */
static struct Element *Elements;
static struct Node *Nodes;

/* global variables to keep track of circuits */
static struct nlist *Circuit1;
static struct nlist *Circuit2;

/* keep free lists of Elements and Nodes */
static struct Element *ElementFreeList = NULL;
static struct Node *NodeFreeList = NULL;
static struct ElementClass *ElementClassFreeList = NULL;
static struct NodeClass *NodeClassFreeList = NULL;
static struct ElementList *ElementListFreeList = NULL;
static struct NodeList *NodeListFreeList = NULL;

/* if TRUE, always partition ALL classes */
int ExhaustiveSubdivision = 0;

#ifdef TEST
static void PrintElement_List(struct Element *E)
{
  struct NodeList *nl;
	
  for (; E != NULL; E = E->next) {
    struct objlist *ob;
    Fprintf(stdout, "   Element %s, circuit: %hd hashval = %lX\n", 
	   E->object->instance, E->graph, E->hashval);
    ob = E->object;
    for (nl = E->nodelist; nl != NULL; nl = nl->next) {
#if 1
      Fprintf(stdout, "      %s:  node: %s name: %d pin magic: %lX nodeclassmagic: %lX\n",
	     ob->name + strlen(ob->instance) + 1,
	     nl->node->object->name,  nl->node->object->node, 
	     nl->pin_magic, nl->node->nodeclass->magic);
#else
      Fprintf(stdout, "      %s: %lX  node: %lX num: %d magic: %lX nodeclassmag: %lX\n",
	     ob->name + strlen(ob->instance) + 1,
	     (long)nl, (long)(nl->node), nl->node->object->node, 
	     nl->magic, nl->node->nodeclass->magic);
#endif
      ob = ob->next;
    }
  }
}

static char *ElementList_Name(struct ElementList *e)
/* returns a pointer to the name of the pin that a particular 
   ElementList element contacts.  */
{
  struct objlist *ob;
  struct NodeList *nl;

  ob = e->subelement->element->object;
  nl = e->subelement->element->nodelist;
  while (nl != e->subelement) {
    nl = nl->next;
    ob = ob->next;
  }
  return(ob->name);
}



static void PrintNode_List(struct Node *N)
{
  struct ElementList *el;
	
  for (; N != NULL; N = N->next) {
    Fprintf(stdout, "   Node %s circuit: %hd (num = %d) addr = %lX, hashval = %lX\n", 
	   N->object->name, N->graph, N->object->node, (long)N, N->hashval);
    for (el = N->elementlist; el != NULL; el = el->next) {
#if 1
      Fprintf(stdout, "      name: %10s pin magic: %lX element class magic: %lX\n", 
	     ElementList_Name(el), el->subelement->pin_magic,
	     el->subelement->element->elemclass->magic);
#else
      Fprintf(stdout, "      %lX  element: %lX name: %s pin magic: "
		"%lX class magic: %lX\n", 
	     (long)el, (long)(el->subelement), 
/*	     el->subelement->element->object->instance, */
	     ElementList_Name(el),
	     el->subelement->magic,
	     el->subelement->element->elemclass->magic);
#endif
    }
  }
}

/* For PrintElementClasses() and PrintNodeClasses(), type is:	*/
/*  0 -> Print legal partitions					*/
/*  1 -> Print illegal partitions				*/
/* -1 -> Print all partitions					*/

/* Ignore "dolist" in test version */

void PrintElementClasses(struct ElementClass *EC, int type, int dolist)
{
  while (EC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    if (EC->legalpartition) {
       if (type != 1) {
          Fprintf(stdout, "Element class: count = %d; magic = %lX",
			EC->count, EC->magic);
	  Fprintf(stdout, " -- legal partition\n");
          PrintElement_List(EC->elements);
       }
    }
    else {
       if (type != 0) {
          Fprintf(stdout, "Element class: count = %d; magic = %lX",
			EC->count, EC->magic);
	  Fprintf(stdout, " -- ILLEGAL partition\n");
          PrintElement_List(EC->elements);
       }
    }
    Printf("\n");
    EC = EC->next;
  }
}

void PrintNodeClasses(struct NodeClass *NC, int type, int dolist)
{
  while (NC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    if (NC->legalpartition) {
       if (type != 1) {
          Fprintf(stdout, "Node class: count = %d; magic = %lX",
			NC->count, NC->magic);
          Fprintf(stdout, " -- legal partition\n");
          PrintNode_List(NC->nodes);
       }
    }
    else {
       if (type != 0) {
          Fprintf(stdout, "Node class: count = %d; magic = %lX",
			NC->count, NC->magic);
          Fprintf(stdout, " -- ILLEGAL partition\n");
          PrintNode_List(NC->nodes);
       }
    }
    Fprintf(stdout, "\n");
    NC = NC->next;
  }
}

#else	/* NOT TEST */

/* For PrintElementClasses() and PrintNodeClasses(), type is:	*/
/*  0 -> Print legal partitions					*/
/*  1 -> Print illegal partitions				*/
/* -1 -> Print all partitions					*/

void PrintElementClasses(struct ElementClass *EC, int type, int dolist)
{
  struct Element *E;
#ifdef TCL_NETGEN
  Tcl_Obj *plist;

  plist = Tcl_NewListObj(0, NULL);
#endif

  while (EC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    if (EC->legalpartition) {
       if (type != 1) {
	  if (dolist == 0) {
	     Printf("Element class: count = %d; magic = %lX", EC->count, EC->magic);
	     Printf(" -- legal partition\n");

	     for (E = EC->elements; E != NULL; E = E->next) 
                Printf("   %-20s (circuit %hd) hash = %lX\n", 
			E->object->instance, E->graph, E->hashval);
	  }
#ifdef TCL_NETGEN
	  else {
	     Tcl_Obj *elist, *epart1, *epart2;
	     elist = Tcl_NewListObj(0, NULL);
	     epart1 = Tcl_NewListObj(0, NULL);
	     epart2 = Tcl_NewListObj(0, NULL);
	     for (E = EC->elements; E != NULL; E = E->next)
	        Tcl_ListObjAppendElement(netgeninterp,
			(E->graph == Circuit1->file) ? epart1 : epart2,
			Tcl_NewStringObj(E->object->instance, -1));
		
	     Tcl_ListObjAppendElement(netgeninterp, elist, epart1);
	     Tcl_ListObjAppendElement(netgeninterp, elist, epart2);
	     Tcl_ListObjAppendElement(netgeninterp, plist, elist);
	  }
#endif
       }
    }
    else {
       if (type != 0) {
	  if (dolist == 0) {
	     Printf("Element class: count = %d; magic = %lX", EC->count, EC->magic);
             Printf(" -- ILLEGAL partition\n");

	     for (E = EC->elements; E != NULL; E = E->next) 
                Printf("   %-20s (circuit %hd) hash = %lX\n", 
			E->object->instance, E->graph, E->hashval);
	  }
#ifdef TCL_NETGEN
	  else {
	     Tcl_Obj *elist, *epart1, *epart2;
	     elist = Tcl_NewListObj(0, NULL);
	     epart1 = Tcl_NewListObj(0, NULL);
	     epart2 = Tcl_NewListObj(0, NULL);
	     for (E = EC->elements; E != NULL; E = E->next)
	        Tcl_ListObjAppendElement(netgeninterp,
				(E->graph == Circuit1->file) ? epart1 : epart2,
				Tcl_NewStringObj(E->object->instance, -1));

	     Tcl_ListObjAppendElement(netgeninterp, elist, epart1);
	     Tcl_ListObjAppendElement(netgeninterp, elist, epart2);
	     Tcl_ListObjAppendElement(netgeninterp, plist, elist);
	  }
#endif
       }
    }
    EC = EC->next;
  }
#ifdef TCL_NETGEN
  Tcl_SetObjResult(netgeninterp, plist);
#endif
}

void PrintNodeClasses(struct NodeClass *NC, int type, int dolist)
{
  struct Node *N;
#ifdef TCL_NETGEN
  Tcl_Obj *plist;

  plist = Tcl_NewListObj(0, NULL);
#endif

  while (NC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    if (NC->legalpartition) {
       if (type != 1) {
	  if (dolist == 0) {
	     Printf("Node class: count = %d; magic = %lX", NC->count, NC->magic);
	     Printf(" -- legal partition\n");
	     for (N = NC->nodes; N != NULL; N = N->next) 
	        Printf("   %-20s (circuit %hd) hash = %lX\n", 
		     N->object->name, N->graph, N->hashval);
	  }
#ifdef TCL_NETGEN
	  else {
	     Tcl_Obj *nlist, *npart1, *npart2;
	     nlist = Tcl_NewListObj(0, NULL);
	     npart1 = Tcl_NewListObj(0, NULL);
	     npart2 = Tcl_NewListObj(0, NULL);
	     for (N = NC->nodes; N != NULL; N = N->next)
	        Tcl_ListObjAppendElement(netgeninterp,
				(N->graph == Circuit1->file) ? npart1 : npart2,
				Tcl_NewStringObj(N->object->name, -1));

	     Tcl_ListObjAppendElement(netgeninterp, nlist, npart1);
	     Tcl_ListObjAppendElement(netgeninterp, nlist, npart2);
	     Tcl_ListObjAppendElement(netgeninterp, plist, nlist);
	  }
#endif
       }
    }
    else {
       if (type != 0) {
	  if (dolist == 0) {
	     Printf("Node class: count = %d; magic = %lX", NC->count, NC->magic);
	     Printf(" -- ILLEGAL partition\n");
	     for (N = NC->nodes; N != NULL; N = N->next) 
	        Printf("   %-20s (circuit %hd) hash = %lX\n", 
		     N->object->name, N->graph, N->hashval);
	  }
#ifdef TCL_NETGEN
	  else {
	     Tcl_Obj *nlist, *npart1, *npart2;
	     nlist = Tcl_NewListObj(0, NULL);
	     npart1 = Tcl_NewListObj(0, NULL);
	     npart2 = Tcl_NewListObj(0, NULL);
	     for (N = NC->nodes; N != NULL; N = N->next)
	        Tcl_ListObjAppendElement(netgeninterp,
				(N->graph == Circuit1->file) ? npart1 : npart2,
				Tcl_NewStringObj(N->object->name, -1));

	     Tcl_ListObjAppendElement(netgeninterp, nlist, npart1);
	     Tcl_ListObjAppendElement(netgeninterp, nlist, npart2);
	     Tcl_ListObjAppendElement(netgeninterp, plist, nlist);
	  }
#endif
       }
    }
    NC = NC->next;
  }
#ifdef TCL_NETGEN
  Tcl_SetObjResult(netgeninterp, plist);
#endif
}
#endif  /* TEST */

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void SummarizeNodeClasses(struct NodeClass *NC)
{
  while (NC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    Printf("Node class: count = %d; magic = %lX; hash = %ld", 
	   NC->count, NC->magic, NC->nodes->hashval);
    if (NC->legalpartition) Printf(" -- legal partition\n");
    else Printf(" -- ILLEGAL partition\n");
    NC = NC->next;
  }
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void SummarizeElementClasses(struct ElementClass *EC)
{
  while (EC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    Printf("Element class: count = %d; magic = %lX; hash = %ld", 
	   EC->count, EC->magic, EC->elements->hashval);
    if (EC->legalpartition) Printf(" -- legal partition\n");
    else Printf(" -- ILLEGAL partition\n");
    EC = EC->next;
  }
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void SummarizeDataStructures(void)
{
  struct ElementClass *EC;
  struct NodeClass *NC;
  struct Element *E;
  struct Node *N;
  int cell1, cell2, orphan1, orphan2;

  cell1 = cell2 = 0;
  for (EC = ElementClasses; EC != NULL; EC = EC->next)
    for (E = EC->elements; E != NULL; E = E->next) {
      if (E->graph == Circuit1->file) cell1++;
      else cell2++;
    }
  Printf("Circuit 1 contains %d elements, Circuit 2 contains %d elements.",
	 cell1, cell2);
  if (cell1 != cell2) Printf(" *** MISMATCH ***");
  Printf("\n");

  cell1 = cell2 = 0;
  orphan1 = orphan2 = 0;
  for (NC = NodeClasses; NC != NULL; NC = NC->next)
    for (N = NC->nodes; N != NULL; N = N->next) {
      if (N->graph == Circuit1->file) {
	cell1++;
	if (N->elementlist == NULL) orphan1++;
      }
      else {
	cell2++;
	if (N->elementlist == NULL) orphan2++;
      }
    }
  Printf("Circuit 1 contains %d nodes,    Circuit 2 contains %d nodes.",
	 cell1, cell2);
  if (cell1 != cell2) Printf(" *** MISMATCH ***");
  Printf("\n");
  if (orphan1 || orphan2) {
    Printf("Circuit 1 contains %d orphan nodes, Circuit 2 contains %d orphans.");
    if (orphan1 != orphan2) Printf(" *** MISMATCH ***");
    Printf("\n");
  }
  Printf("\n");
}
  
/*
 *-----------------------------------------------------------------------
 * Structures used by FormatBadElementFragment and FormatBadNodeFragment
 *-----------------------------------------------------------------------
 */

struct FanoutList {
   char *model;
   char *name;
   char permute;
   int count;
};

struct FormattedList {
   char *name;
   int fanout;
   struct FanoutList *flist;
};

/* Forward declaration */
void FreeFormattedLists(struct FormattedList **nlists, int numlists);

/* 
 *---------------------------------------------------------------------
 * FormatBadElementFragment
 *
 * formats the numbers of nets that a particular bad element pin
 * fragment contacts
 *---------------------------------------------------------------------
 */

struct FormattedList *FormatBadElementFragment(struct Element *E)
{     
  struct NodeList **nodes;
  int fanout;
  struct NodeList *nl;
  struct objlist *ob;
  int count, i, j, k, m;
  struct FormattedList *elemlist;

  elemlist = (struct FormattedList *)MALLOC(sizeof(struct FormattedList));
  if (elemlist == NULL) {
     Fprintf(stdout, "Unable to allocated memory to print element fanout.\n");
     return;
  }

  fanout = 0;
  for (nl = E->nodelist; nl != NULL; nl = nl->next) fanout++;
  nodes = (struct NodeList **)CALLOC(fanout, sizeof(struct NodeList *));
  if (nodes == NULL) {
    Printf("Unable to allocate memory to print element fanout.\n");
    FREE(elemlist);
    return;
  }

  elemlist->flist = (struct FanoutList *)CALLOC(fanout, sizeof(struct FanoutList));
  elemlist->fanout = fanout;
  elemlist->name = E->object->instance;
  
  fanout = 0;
  for (nl = E->nodelist; nl != NULL; nl = nl->next) 
    nodes[fanout++] = nl;

  ob = E->object;
  k = 0;
  for (i = 0; i < fanout; i++) {
    if (nodes[i] == NULL) {
       ob = ob->next;
       continue;
    }

    count = 1;
    for (j = i+1; j < fanout; j++) {
      /* count the number of pins with the same magic number (permutable pins) */
      if (nodes[j] != NULL && nodes[i]->pin_magic == nodes[j]->pin_magic) {
	count++;
      }
    }
    if (count == 1) {			/* pin is unique */
      struct ElementList *elems;

      count = 0;
      if (nodes[i]->node != NULL) {
         for (elems = nodes[i]->node->elementlist; elems != NULL;
			elems = elems->next)
	    count++;

         elemlist->flist[k].count = count;
         elemlist->flist[k].name = ob->name + strlen(ob->instance) + 1;
         elemlist->flist[k].permute = (char)1;
         k++;
      }
    }
    else {				/* handle multiple permutable pins */
      struct objlist *ob2;
      int maxindex, maxcount;
      unsigned long oldmagic;

      ob2 = ob;
      m = k;
      for (j = i; j < fanout; j++) {
	if (nodes[j] != NULL && nodes[i]->pin_magic == nodes[j]->pin_magic) {
          elemlist->flist[k].name = ob2->name + strlen(ob2->instance) + 1;
          elemlist->flist[k].permute = (char)0;
          elemlist->flist[k].count = -1;	// Put total count at end
	  k++;
	}
	ob2 = ob2->next;
      }
      k = m;

      /* sort fanouts in DECREASING order */
      maxindex = i;
      maxcount = -1;
      oldmagic = nodes[i]->pin_magic; /* allows us to nuke pins[i] */

      while (maxindex >= 0) {
	maxcount = -1;
	maxindex = -1;
	  
	for (j = i; j < fanout; j++) {
	  if (nodes[j] != NULL && oldmagic == nodes[j]->pin_magic) {
	    struct ElementList *elems;

	    count = 0;
	    if (nodes[j]->node == NULL) continue;	// ?
	    for (elems = nodes[j]->node->elementlist; elems != NULL;
		 elems = elems->next) 
	      count++;
	    if (count >= maxcount) {
	      maxcount = count;
	      maxindex = j;
	    }
	  }
	}
	if (maxindex >= 0) {
          elemlist->flist[k].count = maxcount;
	  nodes[maxindex] = NULL;	/* So we don't double-count */
	  k++;
	}
      }
      elemlist->flist[k - 1].permute = (char)1;	 /* Denotes end of list */

    }
    nodes[i] = NULL;
    ob = ob->next;		/* point to next name in list */
  }
  elemlist->fanout = k;

  FREE(nodes);
  return elemlist;
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void PrintBadElementFragment(struct Element *E)
{     
  struct NodeList **nodes;
  int fanout;
  struct NodeList *nl;
  struct objlist *ob;
  int count, i, j;

  Fprintf(stdout, "  (%d): %s",E->graph, E->object->instance);
  Ftab(stdout, 20);

  fanout = 0;
  for (nl = E->nodelist; nl != NULL; nl = nl->next) fanout++;
  nodes = (struct NodeList **)CALLOC(fanout, sizeof(struct NodeList *));
  if (nodes == NULL) {
    Printf("Unable to allocate memory to print element fanout.\n");
    return;
  }
  
  Ftab(stdout, 20);
  Fprintf(stdout, " ==>  ");

  Fwrap(stdout, 80);		/* set wrap-around */
  fanout = 0;
  for (nl = E->nodelist; nl != NULL; nl = nl->next) 
    nodes[fanout++] = nl;


  ob = E->object;
  for (i = 0; i < fanout; i++) {
    if (nodes[i] == NULL) {
       ob = ob->next;
       continue;
    }

    count = 1;
    for (j = i+1; j < fanout; j++) {
      /* count the number of pins with the same magic number */
      if (nodes[j] != NULL && nodes[i]->pin_magic == nodes[j]->pin_magic)
	count++;
    }
    if (count == 1) {
      struct ElementList *elems;

      count = 0;
      if (nodes[i]->node != NULL)
         for (elems = nodes[i]->node->elementlist; elems != NULL;
			elems = elems->next)
	    count++;
      if (i != 0) Fprintf(stdout, "; ");
      Fprintf(stdout, "%s = %d", ob->name + strlen(ob->instance) + 1, count);
    }
    else {
      struct objlist *ob2;
      int maxindex, maxcount, someprinted;
      unsigned long oldmagic;

      if (i != 0) Fprintf(stdout, "; ");
      Fprintf(stdout, "(");
      ob2 = ob;
      for (j = i; j < fanout; j++) {
	if (nodes[j] != NULL && nodes[i]->pin_magic == nodes[j]->pin_magic) {
	  if (i != j) Fprintf(stdout, ", ");
	  Fprintf(stdout, "%s", ob2->name + strlen(ob2->instance) + 1);
	}
	ob2 = ob2->next;
      }
      Fprintf(stdout, ") = (");

      /* sort fanouts in DECREASING order */
      someprinted = 0;
      maxindex = i;
      maxcount = -1;
      oldmagic = nodes[i]->pin_magic; /* allows us to nuke pins[i] */

      while (maxindex >= 0) {
	maxcount = -1;
	maxindex = -1;
	  
	for (j = i; j < fanout; j++) {
	  if (nodes[j] != NULL && oldmagic == nodes[j]->pin_magic) {
	    struct ElementList *elems;

	    count = 0;
	    for (elems = nodes[j]->node->elementlist; elems != NULL;
		 elems = elems->next) 
	      count++;
	    if (count >= maxcount) {
	      maxcount = count;
	      maxindex = j;
	    }
	  }
	}
	if (maxindex >= 0) {
	  if (someprinted) Fprintf(stdout, ", ");
	  Fprintf(stdout, "%d", maxcount);
	  someprinted = 1;
	  nodes[maxindex] = NULL;
	}
      }
      Fprintf(stdout, ")");
    }
    nodes[i] = NULL;
    ob = ob->next;		/* point to next name in list */
  }

  Fprintf(stdout, "\n");
  FREE(nodes);
}

/* 
 *---------------------------------------------------------------------
 * FormatBadNodeFragment --
 *
 * formats the numbers and types of pins that a particular bad node
 * fragment contacts;  e.g.   NODE2  5 p:gate  10 n:source
 *---------------------------------------------------------------------
 */

struct FormattedList *FormatBadNodeFragment(struct Node *N)
{
  struct ElementList **pins;
  int fanout;
  struct ElementList *e;
  int i, j, k;
  struct FormattedList *nodelist;

  fanout = 0;
  for (e = N->elementlist; e != NULL; e = e->next) fanout++;
  /* there are at most 'fanout' different types of pins contacted */

  pins = (struct ElementList **)CALLOC(fanout, sizeof(struct ElementList *));
  if (pins == NULL) {
    Fprintf(stdout, "Unable to allocate memory to print node fanout.\n");
    return;
  }

  nodelist = (struct FormattedList *)MALLOC(sizeof(struct FormattedList));
  if (nodelist == NULL) {
    Fprintf(stdout, "Unable to allocate memory to print node fanout.\n");
    FREE(pins);
    return;
  }
  nodelist->flist = (struct FanoutList *)CALLOC(fanout, sizeof(struct FanoutList));
  nodelist->fanout = fanout;
  nodelist->name = (N->object == NULL) ? NULL : N->object->name;

  fanout = 0;
  for (e = N->elementlist; e != NULL; e = e->next) 
    pins[fanout++] = e;

  /* process pins in sequence, NULLing out pins as they are processed */
  k = 0;
  for (i = 0; i < fanout; i++)
    if (pins[i] != NULL) {
      int count;
      char *model, *pinname, permute;
      struct NodeList *n;
      struct objlist *ob;

      count = 1; /* remember: pins[i] contacts it */
      permute = (char)0;
      model = pins[i]->subelement->element->object->model;
      /* find the first pin on that element with the same magic number */
      pinname = "can't happen";
      ob = pins[i]->subelement->element->object;
      for (n = pins[i]->subelement->element->nodelist; n != NULL; n = n->next){
	if (n->pin_magic == pins[i]->subelement->pin_magic) {
	  if (permute == 0) {
	     pinname = ob->name + strlen(ob->instance) + 1;
	  }
	  else {
	     char *pinsave = pinname;
	     pinname = (char *)MALLOC(strlen(pinsave) + strlen(ob->name +
			strlen(ob->instance) + 1) + 2);
	     sprintf(pinname, "%s|%s", pinsave, ob->name + strlen(ob->instance) + 1);
	     if (permute > 1) FREE(pinsave);
	  }
	  permute++;
	}
	ob = ob->next;
      }

      /* now see if any other pins from elements of the same class,
         WITH THE SAME HASH NUMBER, are on this node */
      for (j = i+1; j < fanout; j++) {
	if (pins[j] != NULL && 
	    (*matchfunc) (model,
		  pins[j]->subelement->element->object->model) &&
	    pins[i]->subelement->pin_magic == pins[j]->subelement->pin_magic) {
	      count++;
	      nodelist->fanout--;
	      pins[j] = NULL;
	    }
      }

      nodelist->flist[k].model = model;
      nodelist->flist[k].name = pinname;
      nodelist->flist[k].count = count;
      nodelist->flist[k].permute = permute;
      k++;

      pins[i] = NULL; /* not really necessary */
    }
  FREE(pins);
  return nodelist;
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void PrintBadNodeFragment(struct Node *N)
/* prints out the numbers and types of pins that a particular bad node
   fragment contacts;  e.g.   NODE2  5 p:gate  10 n:source
*/
{
  struct ElementList **pins;
  int fanout;
  struct ElementList *e;
  int i, j;

  Fprintf(stdout, "  (%d): %s",N->graph,  (N->object == NULL) ?
		"(unknown)" : N->object->name);
  
  fanout = 0;
  for (e = N->elementlist; e != NULL; e = e->next) fanout++;
  /* there are at most 'fanout' different types of pins contacted */

  pins = (struct ElementList **)CALLOC(fanout, sizeof(struct ElementList *));
  if (pins == NULL) {
    Fprintf(stdout, "Unable to allocate memory to print node fanout.\n");
    return;
  }
  Ftab(stdout, 25);
  Fprintf(stdout, " ==>  ");

  Fwrap(stdout, 80);  /* set wrap-around */
  fanout = 0;
  for (e = N->elementlist; e != NULL; e = e->next) 
    pins[fanout++] = e;


  /* process pins in sequence, NULLing out pins as they are processed */
  for (i = 0; i < fanout; i++)
    if (pins[i] != NULL) {
      int count;
      char *model, *pinname;
      struct NodeList *n;
      struct objlist *ob;

      count = 1; /* remember: pins[i] contacts it */
      model = pins[i]->subelement->element->object->model;
      /* find the first pin on that element with the same magic number */
      pinname = "can't happen";
      ob = pins[i]->subelement->element->object;
      for (n = pins[i]->subelement->element->nodelist; n != NULL; n = n->next){
	if (n->pin_magic == pins[i]->subelement->pin_magic) {
	  pinname = ob->name + strlen(ob->instance) + 1;
	  break;    /* MAS 3/13/91 */
	}
	ob = ob->next;
      }

      /* now see if any other pins from elements of the same class,
         WITH THE SAME HASH NUMBER, are on this node */
      for (j = i+1; j < fanout; j++) {
	if (pins[j] != NULL && 
	    (*matchfunc)(model,
		  pins[j]->subelement->element->object->model) &&
	    pins[i]->subelement->pin_magic == pins[j]->subelement->pin_magic) {
	      count++;
	      pins[j] = NULL;
	    }
      }

      if (i != 0) Fprintf(stdout, ";");
      Fprintf(stdout, " %s:%s = %d", model, pinname, count);
      pins[i] = NULL; /* not really necessary */
    }
  Fprintf(stdout, "\n");
  Fwrap(stdout, 0);  /* unset wrap-around */
  FREE(pins);
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void FormatIllegalElementClasses(void)
{
  struct FormattedList **elist1, **elist2;
  struct ElementClass *escan;
  int found, numlists1, numlists2, n1, n2, n, f1, f2, i, maxf;
  char ostr[89];

  found = 0;
  for (escan = ElementClasses; escan != NULL; escan = escan->next)
    if (!(escan->legalpartition)) {
      struct Element *E;

      if (!found) {
	Fprintf(stdout, "ILLEGAL element partition: ");
	Fprintf(stdout, "class fragments follow (with node fanout counts):\n");

	/* Print in side-by-side format */

	*(ostr + 43) = '|';
	*(ostr + 87) = '\n';
	*(ostr + 88) = '\0';
	for (i = 0; i < 43; i++) *(ostr + i) = ' ';
	for (i = 44; i < 87; i++) *(ostr + i) = ' ';
	snprintf(ostr, 43, "Circuit 1: %s", Circuit1->name);
	snprintf(ostr + 44, 43, "Circuit 2: %s", Circuit2->name);
	for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
	Fprintf(stdout, ostr);
      }
      found = 1;

      numlists1 = numlists2 = 0;
      for (E = escan->elements; E != NULL; E = E->next)
      {
	 if (E->graph == Circuit1->file)
	    numlists1++;
	 else
	    numlists2++;
      }
      elist1 = (struct FormattedList **)CALLOC(numlists1,
		sizeof(struct FormattedList *));
      elist2 = (struct FormattedList **)CALLOC(numlists2,
		sizeof(struct FormattedList *));

      n1 = n2 = 0;

      for (E = escan->elements; E != NULL; E = E->next) {
#ifdef TCL_NETGEN
        if (check_interrupt()) {
	   FreeFormattedLists(elist1, n1);
	   FreeFormattedLists(elist2, n2);
	   return;
	} 
#endif
	if (E->graph == Circuit1->file) {
	   elist1[n1] = FormatBadElementFragment(E);
	   n1++;
	}
	else {
	   elist2[n2] = FormatBadElementFragment(E);
	   n2++;
	}
      }
      Fprintf(stdout, "\n");

      for (n = 0; n < ((n1 > n2) ? n1 : n2); n++) {
	 if (n != 0) {
	    for (i = 0; i < 43; i++) *(ostr + i) = ' ';
	    for (i = 44; i < 87; i++) *(ostr + i) = ' ';
	    Fprintf(stdout, ostr);
	 } else {
	    for (i = 0; i < 87; i++) *(ostr + i) = '-';
	    Fprintf(stdout, ostr);
	    *(ostr + 43) = '|';
	 }
	 for (i = 0; i < 43; i++) *(ostr + i) = ' ';
	 for (i = 44; i < 87; i++) *(ostr + i) = ' ';
	 if (n < n1)
	    snprintf(ostr, 43, "Instance: %s", elist1[n]->name);
	 else
	    snprintf(ostr, 43, "(no matching instance)");
	 if (n < n2)
	    snprintf(ostr + 44, 43, "Instance: %s", elist2[n]->name);
	 else
	    snprintf(ostr + 44, 43, "(no matching instance)");
	 for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
	 Fprintf(stdout, ostr);

	 if (n >= n1)
	    maxf = elist2[n]->fanout;
	 else if (n >= n2)
	    maxf = elist1[n]->fanout;
	 else
	    maxf = (elist1[n]->fanout > elist2[n]->fanout) ?
			elist1[n]->fanout : elist2[n]->fanout;

	 f1 = f2 = 0;
	 while ((f1 < maxf) || (f2 < maxf)) {
	    for (i = 0; i < 43; i++) *(ostr + i) = ' ';
	    for (i = 44; i < 87; i++) *(ostr + i) = ' ';
	    if (n < n1) {
	       if (f1 < elist1[n]->fanout) {
		  if (elist1[n]->flist[f1].permute == (char)1) {
		     snprintf(ostr, 43, "  %s = %d", elist1[n]->flist[f1].name,
				elist1[n]->flist[f1].count);
		  }
		  else {
		     char permname[80];
		     char permcount[80];
		     char value[10];
		     sprintf(permname, "(");
		     sprintf(permcount, "(");
		     while (elist1[n]->flist[f1].permute == (char)0) {
			strcat(permname, elist1[n]->flist[f1].name);
			strcat(permname, ",");
			sprintf(value, "%d", elist1[n]->flist[f1].count);
			strcat(permcount, value);
			strcat(permcount, ",");
			f1++;
		     }
		     strcat(permname, elist1[n]->flist[f1].name);
		     strcat(permname, ")");
		     sprintf(value, "%d", elist1[n]->flist[f1].count);
		     strcat(permcount, value);
		     strcat(permcount, ")");
		     snprintf(ostr, 43, "  %s = %s", permname, permcount);
		  }
	       }
	    }
	    f1++;
	    if (n < n2) {
	       if (f2 < elist2[n]->fanout) {
		  if (elist2[n]->flist[f2].permute == (char)1) {
		     snprintf(ostr + 44, 43, "  %s = %d", elist2[n]->flist[f2].name,
				elist2[n]->flist[f2].count);
		  }
		  else {
		     char permname[80];
		     char permcount[80];
		     char value[10];
		     sprintf(permname, "(");
		     sprintf(permcount, "(");
		     while (elist2[n]->flist[f2].permute == (char)0) {
			strcat(permname, elist2[n]->flist[f2].name);
			strcat(permname, ",");
			sprintf(value, "%d", elist2[n]->flist[f2].count);
			strcat(permcount, value);
			strcat(permcount, ",");
			f2++;
		     }
		     strcat(permname, elist2[n]->flist[f2].name);
		     strcat(permname, ")");
		     sprintf(value, "%d", elist2[n]->flist[f2].count);
		     strcat(permcount, value);
		     strcat(permcount, ")");
		     snprintf(ostr + 44, 43, "  %s = %s", permname, permcount);
		  }
	       }
	    }
	    f2++;
	    for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
	    Fprintf(stdout, ostr);
	 }
      }

      FreeFormattedLists(elist1, numlists1);
      FreeFormattedLists(elist2, numlists2);
      for (i = 0; i < 87; i++) *(ostr + i) = '-';
      Fprintf(stdout, ostr);
      *(ostr + 43) = '|';
    }
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void PrintIllegalElementClasses(void)
{
  struct ElementClass *escan;
  int found;

  found = 0;
  for (escan = ElementClasses; escan != NULL; escan = escan->next)
    if (!(escan->legalpartition)) {
      struct Element *E;

      if (!found) {
	Fprintf(stdout, "ILLEGAL element partition: ");
	Fprintf(stdout, "class fragments follow (with node fanout counts):\n");
      }
      found = 1;
      for (E = escan->elements; E != NULL; E = E->next)
      {
#ifdef TCL_NETGEN
        if (check_interrupt()) return;
#endif
	PrintBadElementFragment(E);
      }
      Fprintf(stdout, "---------------------------\n");
    }
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void FreeFormattedLists(struct FormattedList **nlists, int numlists)
{
   int n;
   for (n = 0; n < numlists; n++) {
      FREE(nlists[n]->flist);
      FREE(nlists[n]);
   }
   FREE(nlists);
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void FormatIllegalNodeClasses(void)
{
  struct FormattedList **nlists1, **nlists2;
  struct NodeClass *nscan;
  int found, numlists1, numlists2, n1, n2, n, f, i, maxf;
  char ostr[89];

  found = 0;

  for (nscan = NodeClasses; nscan != NULL; nscan = nscan->next)
    if (!(nscan->legalpartition)) {
      struct Node *N;

      if (!found) {
        Fprintf(stdout, "ILLEGAL node partition: ");
	Fprintf(stdout, "class fragments follow (with fanout counts):\n");

	/* Print in side-by-side format */
	*(ostr + 43) =  '|';
	*(ostr + 87) = '\n';
	*(ostr + 88) = '\0';
	for (i = 0; i < 43; i++) *(ostr + i) = ' ';
	for (i = 44; i < 87; i++) *(ostr + i) = ' ';
	snprintf(ostr, 43, "Circuit 1: %s", Circuit1->name);
	snprintf(ostr + 44, 43, "Circuit 2: %s", Circuit2->name);
	for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
	Fprintf(stdout, ostr);
      }
      found = 1;

      numlists1 = numlists2 = 0;
      for (N = nscan->nodes; N != NULL; N = N->next) {
	 if (N->graph == Circuit1->file)
	    numlists1++;
	 else
	    numlists2++;
      }
      nlists1 = (struct FormattedList **)CALLOC(numlists1,
		sizeof(struct FormattedNodeList *));
      nlists2 = (struct FormattedList **)CALLOC(numlists2,
		sizeof(struct FormattedList *));

      n1 = n2 = 0; 
      for (N = nscan->nodes; N != NULL; N = N->next) {
#ifdef TCL_NETGEN
        if (check_interrupt()) {
	   FreeFormattedLists(nlists1, n1);
	   FreeFormattedLists(nlists2, n2);
	   return;
	}
#endif
	if (N->graph == Circuit1->file) {
	   nlists1[n1] = FormatBadNodeFragment(N);
	   n1++;
	}
	else {
	   nlists2[n2] = FormatBadNodeFragment(N);
	   n2++;
	}
      }
      Fprintf(stdout, "\n");

      for (n = 0; n < ((n1 > n2) ? n1 : n2); n++) {
	 if (n != 0) {
            for (i = 0; i < 43; i++) *(ostr + i) = ' ';
            for (i = 44; i < 87; i++) *(ostr + i) = ' ';
	    Fprintf(stdout, ostr);
	 } else {
            for (i = 0; i < 87; i++) *(ostr + i) = '-';
	    Fprintf(stdout, ostr);
	    *(ostr + 43) = '|';
	 }
         for (i = 0; i < 43; i++) *(ostr + i) = ' ';
         for (i = 44; i < 87; i++) *(ostr + i) = ' ';
	 if (n < n1)
	    snprintf(ostr, 43, "Net: %s", nlists1[n]->name);
	 else
	    snprintf(ostr, 43, "(no matching net)");
	 if (n < n2)
	    snprintf(ostr + 44, 43, "Net: %s", nlists2[n]->name);
	 else
	    snprintf(ostr + 44, 43, "(no matching net)");
         for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
         Fprintf(stdout, ostr);

	 if (n >= n1)
	    maxf = nlists2[n]->fanout;
	 else if (n >= n2)
	    maxf = nlists1[n]->fanout;
	 else
	    maxf = (nlists1[n]->fanout > nlists2[n]->fanout) ?
			nlists1[n]->fanout : nlists2[n]->fanout;

	 for (f = 0; f < maxf; f++) {
            for (i = 0; i < 43; i++) *(ostr + i) = ' ';
            for (i = 44; i < 87; i++) *(ostr + i) = ' ';
	    if (n < n1)
	       if (f < nlists1[n]->fanout) {
		  if (nlists1[n]->flist[f].permute <= 1)
	             snprintf(ostr, 43, "  %s/%s = %d",
				nlists1[n]->flist[f].model,
				nlists1[n]->flist[f].name,
				nlists1[n]->flist[f].count);
		  else {
	             snprintf(ostr, 43, "  %s/(%s) = %d",
				nlists1[n]->flist[f].model,
				nlists1[n]->flist[f].name,
				nlists1[n]->flist[f].count);
		     FREE(nlists1[n]->flist[f].name);
		  }
	       }
	    if (n < n2)
	       if (f < nlists2[n]->fanout) {
		  if (nlists2[n]->flist[f].permute <= 1)
	             snprintf(ostr + 44, 43, "  %s/%s = %d",
				nlists2[n]->flist[f].model,
				nlists2[n]->flist[f].name,
				nlists2[n]->flist[f].count);
		  else {
	             snprintf(ostr + 44, 43, "  %s/(%s) = %d",
				nlists2[n]->flist[f].model,
				nlists2[n]->flist[f].name,
				nlists2[n]->flist[f].count);
		     FREE(nlists2[n]->flist[f].name);
		  }
	       }
            for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
            Fprintf(stdout, ostr);
	 }
      }

      FreeFormattedLists(nlists1, numlists1);
      FreeFormattedLists(nlists2, numlists2);
      for (i = 0; i < 87; i++) *(ostr + i) = '-';
      Fprintf(stdout, ostr);
      *(ostr + 43) =  '|';
    }
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void PrintIllegalNodeClasses(void)
{
  struct NodeClass *nscan;
  int found;

  found = 0;
  for (nscan = NodeClasses; nscan != NULL; nscan = nscan->next)
    if (!(nscan->legalpartition)) {
      struct Node *N;

      if (!found) {
	Fprintf(stdout, "\n");
        Fprintf(stdout, "ILLEGAL node partition: ");
	Fprintf(stdout, "class fragments follow (with fanouts):\n");
      }
      found = 1;
      for (N = nscan->nodes; N != NULL; N = N->next) {
#ifdef TCL_NETGEN
        if (check_interrupt()) return;
#endif
	PrintBadNodeFragment(N);
      }
      Fprintf(stdout, "---------------------------\n");
    }
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void PrintIllegalClasses(void)
{
  PrintIllegalElementClasses();
  PrintIllegalNodeClasses();
}

/**************************** Free lists ***************************/

#ifdef DEBUG_ALLOC
int ElementAllocated;
int NodeAllocated;
int NodeClassAllocated;
int ElementClassAllocated;
int ElementListAllocated;
int NodeListAllocated;
#endif

INLINE
struct Element *GetElement(void)
{
	struct Element *new_element;	
	if (ElementFreeList != NULL) {
		new_element = ElementFreeList;
		ElementFreeList = ElementFreeList->next;
		memzero(new_element, sizeof(struct Element));
	}
	else {
	  new_element = (struct Element *)CALLOC(1,sizeof(struct Element));
#ifdef DEBUG_ALLOC
	  ElementAllocated++;
#endif
	}
	return(new_element);
}

INLINE
void FreeElement(struct Element *old)
{
	old->next = ElementFreeList;
	ElementFreeList = old;
}

INLINE
struct Node *GetNode(void)
{
	struct Node *new_node;	
	if (NodeFreeList != NULL) {
		new_node = NodeFreeList;
		NodeFreeList = NodeFreeList->next;
		memzero(new_node, sizeof(struct Node));
	}
	else {
	  new_node = (struct Node *)CALLOC(1,sizeof(struct Node));
#ifdef DEBUG_ALLOC
	  NodeAllocated++;
#endif
	}
	return(new_node);
}

INLINE
void FreeNode(struct Node *old)
{
	old->next = NodeFreeList;
	NodeFreeList = old;
}

INLINE
struct ElementClass *GetElementClass(void)
{
	struct ElementClass *new_elementclass;	
	if (ElementClassFreeList != NULL) {
		new_elementclass = ElementClassFreeList;
		ElementClassFreeList = ElementClassFreeList->next;
		memzero(new_elementclass, sizeof(struct ElementClass));
	}
	else {
	  new_elementclass =
	    (struct ElementClass *)CALLOC(1,sizeof(struct ElementClass));
#ifdef DEBUG_ALLOC
	  ElementClassAllocated++;
#endif
	}
	new_elementclass->legalpartition = 1;
	return(new_elementclass);
}

INLINE
void FreeElementClass(struct ElementClass *old)
{
	old->next = ElementClassFreeList;
	ElementClassFreeList = old;
}

INLINE
struct NodeClass *GetNodeClass(void)
{
	struct NodeClass *new_nodeclass;	
	if (NodeClassFreeList != NULL) {
		new_nodeclass = NodeClassFreeList;
		NodeClassFreeList = NodeClassFreeList->next;
		memzero(new_nodeclass, sizeof(struct NodeClass));
	}
	else {
	  new_nodeclass =
	    (struct NodeClass *)CALLOC(1,sizeof(struct NodeClass));
#ifdef DEBUG_ALLOC
	  NodeClassAllocated++;
#endif
	}
	new_nodeclass->legalpartition = 1;
	return(new_nodeclass);
}

INLINE
void FreeNodeClass(struct NodeClass *old)
{
	old->next = NodeClassFreeList;
	NodeClassFreeList = old;
}

INLINE
struct ElementList *GetElementList(void)
{
	struct ElementList *new_elementlist;	
	if (ElementListFreeList != NULL) {
		new_elementlist = ElementListFreeList;
		ElementListFreeList = ElementListFreeList->next;
		memzero(new_elementlist, sizeof(struct ElementList));
	}
	else {
	  new_elementlist =
	    (struct ElementList *)CALLOC(1,sizeof(struct ElementList));
#ifdef DEBUG_ALLOC
	  ElementListAllocated++;
#endif
	}
	return(new_elementlist);
}

INLINE
void FreeElementList(struct ElementList *old)
{
	old->next = ElementListFreeList;
	ElementListFreeList = old;
}

INLINE
struct NodeList *GetNodeList(void)
{
	struct NodeList *new_nodelist;	
	if (NodeListFreeList != NULL) {
		new_nodelist = NodeListFreeList;
		NodeListFreeList = NodeListFreeList->next;
		memzero(new_nodelist, sizeof(struct NodeList));
	}
	else {
	  new_nodelist = (struct NodeList *)CALLOC(1,sizeof(struct NodeList));
#ifdef DEBUG_ALLOC
	  NodeListAllocated++;
#endif
	}
	return(new_nodelist);
}

INLINE
void FreeNodeList(struct NodeList *old)
{
	old->next = NodeListFreeList;
	NodeListFreeList = old;
}

#ifdef DEBUG_ALLOC
void PrintCoreStats(void)
{
  Fprintf(stdout, "ElementClass records allocated = %d, size = %d\n",
	  ElementClassAllocated, sizeof(struct ElementClass));
  Fprintf(stdout, "Element records allocated = %d, size = %d\n",
	  ElementAllocated, sizeof(struct Element));
  Fprintf(stdout, "NodeList records allocated = %d, size = %d\n",
	  NodeListAllocated, sizeof(struct NodeList));
  Fprintf(stdout, "NodeClass records allocated = %d, size = %d\n",
	  NodeClassAllocated, sizeof(struct NodeClass));
  Fprintf(stdout, "Node records allocated = %d, size = %d\n",
	  NodeAllocated, sizeof(struct Node));
  Fprintf(stdout, "ElementList records allocated = %d, size = %d\n",
	  ElementListAllocated, sizeof(struct ElementList));
  Fprintf(stdout, "Total accounted-for memory: %d\n",
	  ElementClassAllocated * sizeof(struct ElementClass) +
	  ElementAllocated * sizeof(struct Element) +
	  NodeListAllocated * sizeof(struct NodeList) +
	  NodeClassAllocated * sizeof(struct NodeClass) +
	  NodeAllocated * sizeof(struct Node) +
	  ElementListAllocated * sizeof(struct ElementList));
}
#endif


static int OldNumberOfEclasses;
static int OldNumberOfNclasses;
static int NewNumberOfEclasses;
static int NewNumberOfNclasses;

static int Iterations;

void FreeEntireElementClass(struct ElementClass *ElementClasses)
{
  struct ElementClass *next;
  struct Element *E, *Enext;
  struct NodeList *n, *nnext;

  while (ElementClasses != NULL) {
    next = ElementClasses->next;
    E = ElementClasses->elements;
    while (E != NULL) {
      Enext = E->next;
      n = E->nodelist;
      while (n != NULL) {
	nnext = n->next;
	FreeNodeList(n);
	n = nnext;
      }
      FreeElement(E);
      E = Enext;
    }
    FreeElementClass(ElementClasses);
    ElementClasses = next;
  }
}


void FreeEntireNodeClass(struct NodeClass *NodeClasses)
{
  struct NodeClass *next;
  struct Node *N, *Nnext;
  struct ElementList *e, *enext;

  while (NodeClasses != NULL) {
    next = NodeClasses->next;
    N = NodeClasses->nodes;
    while (N != NULL) {
      Nnext = N->next;
      e = N->elementlist;
      while (e != NULL) {
	enext = e->next;
	FreeElementList(e);
	e = enext;
      }
      FreeNode(N);
      N = Nnext;
    }
    FreeNodeClass(NodeClasses);
    NodeClasses = next;
  }
}

int BadMatchDetected;
int PropertyErrorDetected;
int NewFracturesMade;


void ResetState(void)
{
  if (NodeClasses != NULL)
    FreeEntireNodeClass(NodeClasses);
  if (ElementClasses != NULL)
    FreeEntireElementClass(ElementClasses);
  NodeClasses = NULL;
  ElementClasses = NULL;
  Circuit1 = NULL;
  Circuit2 = NULL;
  Elements = NULL;
  Nodes = NULL;
  NewNumberOfEclasses = OldNumberOfEclasses = 0;
  NewNumberOfNclasses = OldNumberOfNclasses = 0;
  Iterations = 0;
  BadMatchDetected = 0;
  PropertyErrorDetected = 0;
  NewFracturesMade = 0;
  ExhaustiveSubdivision = 0;	/* why not ?? */
  /* maybe should free up free lists ??? */
}



struct Element *CreateElementList(char *name, short graph)
/* create a list of the correct 'shape' for Elements, but with empty records*/
{
  struct objlist *ob;
  struct nlist *tp;
  struct Element *head, *tail;
	
  /* get a pointer to the cell */	
  tp = LookupCellFile(name, graph);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return(NULL);
  }

  head = tail = NULL;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      struct Element *new_element;

      new_element = GetElement();
      if (new_element == NULL) {
	Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	PrintCoreStats();
#endif
	ResetState();
	return NULL;
      }
      new_element->object = ob;
      new_element->graph = graph;
      /* append to list */
      if (head == NULL) head = new_element;
      else tail->next = new_element;
      tail = new_element;
    }
    if (ob->type >= FIRSTPIN) {
      struct NodeList *tmp;
      tmp = GetNodeList();
      tmp->element = tail;
      tmp->next = tail->nodelist;
      tail->nodelist = tmp;
    }
  }
  return(head);
}

#ifdef LOOKUP_INITIALIZATION

struct ElementList **LookupElementList;

struct Node *CreateNodeList(char *name, short graph)
/* create a list of the correct 'shape' of the Node list */
/* for now, all the records are blank */
{
  struct objlist *ob, *newobj;
  struct nlist *tp;
  struct Node *head, *tail, *new_node;
  int maxnode, i;
  struct ElementList *tmp;

  /* get a pointer to the cell */	
  tp = LookupCellFile(name, graph);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return(NULL);
  }

  /* find the max. node number */
  maxnode = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) 
    if (ob->type >= FIRSTPIN && ob->node > maxnode) maxnode = ob->node;
	
  /* now allocate the lookup table */
  LookupElementList = 
    (struct ElementList **)CALLOC(maxnode + 1, sizeof(struct ElementList *));
  if (LookupElementList == NULL) {
    Printf("Unable to allocate space for lookup table\n");
    return(NULL);
  }

  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    // Requirement that ob->node be greater than zero eliminates
    // unconnected nodes (value -1) and dummy nodes (value 0)
    if (ob->type >= FIRSTPIN && (ob->node > 0)) {
      tmp = GetElementList();
      if (tmp == NULL) {
	Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	PrintCoreStats();
#endif
	ResetState();
	return NULL;
      }
      tmp->next = LookupElementList[ob->node];
      LookupElementList[ob->node] = tmp;
    }
  }

  /* now generate a list of Nodes */
  head = tail = NULL;
  for (i = 0; i <= maxnode; i++) {
    if (LookupElementList[i] != NULL) {
      newobj = LookupObject(NodeName(tp, i), tp);
      if (newobj != NULL) {	/* NULL objects may be element property records */
        new_node = GetNode();
        if (new_node == NULL) {
	  Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	  PrintCoreStats();
#endif
	  ResetState();
	  return NULL;
        }
        new_node->object = newobj;
        new_node->graph = graph;
        new_node->elementlist = LookupElementList[i];
        for (tmp = new_node->elementlist; tmp != NULL; tmp = tmp->next)
	  tmp->node = new_node;
        if (head == NULL) head = new_node;
        else tail->next = new_node;
        tail = new_node;
      }
    }
  }
  return (head);
}

void CreateLists(char *name, short graph)
/* creates two lists of the correct 'shape', then traverses nodes
   in sequence to link up 'subelement' field of ElementList,
	then 'node' field of NodeList structures.
*/		
{
  struct Element *ElementScan;
  struct ElementList *EListScan;
  struct NodeList *NListScan;
  struct objlist *ob;
  struct nlist *tp;
	
  /* get a pointer to the cell */	
  tp = LookupCellFile(name, graph);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  if (Circuit1 == NULL) Circuit1 = tp;
  else if (Circuit2 == NULL) Circuit2 = tp;
  else {
    Printf ("Error: CreateLists() called more than twice without a reset.\n");
    return;
  }

  Elements = CreateElementList(name, graph);
  Nodes = CreateNodeList(name, graph);
  if (LookupElementList == NULL) return;

  ElementScan = NULL;
  NListScan = NULL; /* just to stop the compiler from bitching */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      if (ElementScan == NULL) ElementScan = Elements;
      else ElementScan = ElementScan->next;
      NListScan = ElementScan->nodelist;
    }
    /* now hook up node */
    if (ob->type >= FIRSTPIN && (ob->node > 0)) {
      EListScan = LookupElementList[ob->node];
      
      /* write it into the EListScan slot */
      EListScan->subelement = NListScan;
      NListScan->node = EListScan->node;

      /* point to next available ElementList unit */
      LookupElementList[ob->node] = EListScan->next;
      NListScan = NListScan->next;
    }
  }

  FREE(LookupElementList);
  LookupElementList = NULL;
}

#else

struct Node *CreateNodeList(char *name, short graph)
/* create a list of the correct 'shape' of the Node list */
/* for now, all the records are blank */
{
  struct objlist *ob;
  struct nlist *tp;
  struct Node *head, *tail, *new_node;
  int maxnode, i;

  /* get a pointer to the cell */	
  tp = LookupCellFile(name, graph);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return(NULL);
  }

  /* find the max. node number */
  maxnode = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) 
    if (ob->type >= FIRSTPIN && ob->node > maxnode) maxnode = ob->node;
	
  /* now sequence through these node numbers, looking for existance only */
  head = tail = NULL;
  for (i = 1; i <= maxnode; i++) {
    new_node = NULL;
    for (ob = tp->cell; ob != NULL; ob = ob->next) {
      if (ob->type >= FIRSTPIN && ob->node == i) {
	struct ElementList *tmp;
	if (new_node == NULL) {
	  /* it is the first */
	  new_node = GetNode();
	  if (new_node == NULL) {
	    Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	    PrintCoreStats();
#endif
	    ResetState();
	    return NULL;
	  }
	  /* probably want something like LookupObject(NodeNumber(i)) */
	  new_node->object = ob;
	  new_node->graph = graph;
	  if (head == NULL) head = new_node;
	  else tail->next = new_node;
	  tail = new_node;
	}
	/* prepend this element to the front of the sublist */
	tmp = GetElementList();
	if (new_node == NULL) {
	  Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	  PrintCoreStats();
#endif
	  ResetState();
	  return NULL;
	}
	tmp->node = new_node;
	tmp->next = new_node->elementlist;
	new_node->elementlist = tmp;
      }
    }
  }
  return (head);
}

void CreateLists(char *name, short graph)
/* creates two lists of the correct 'shape', then traverses nodes
   in sequence to link up 'subelement' field of ElementList,
	then 'node' field of NodeList structures.
*/		
{
  struct Element *E, *ElementScan;
  struct Node *N, *NodeScan;
  struct ElementList *EListScan;
  struct NodeList *NListScan;
  struct objlist *ob, *obscan;
  struct nlist *tp;
  int node;
	
  /* get a pointer to the cell */	
  tp = LookupCellFile(name, graph);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  if (Circuit1 == NULL) Circuit1 = tp;
  else if (Circuit2 == NULL) Circuit2 = tp;
  else {
    Printf ("Error: CreateLists() called more than twice without a reset.\n");
    return;
  }

  ConnectAllNodes(name, graph);

  E = CreateElementList(name, graph);
  N = CreateNodeList(name, graph);
  NodeScan = N;
  node = 1;
  while (NodeScan != NULL) {
    int foundone;
		
    foundone = 0;
    /* look for all instances of 'node' */
    EListScan = NodeScan->elementlist;
    obscan = NULL;
    for (ob = tp->cell; ob != NULL; ob = ob->next) {
      if (ob->type == FIRSTPIN) obscan = ob;

      if (ob->node == node && ob->type >= FIRSTPIN) {
	struct objlist *tmp;
	foundone = 1;

	/* find object in Element list */
	for (ElementScan = E; ElementScan->object != obscan;
	     ElementScan = ElementScan->next) ;
	NListScan = ElementScan->nodelist;
	for (tmp = obscan; tmp != ob; tmp = tmp->next)
	  NListScan = NListScan->next;

	/* write it into the EListScan slot */
	EListScan->subelement = NListScan;
	NListScan->node = EListScan->node;
				
	EListScan = EListScan->next;
      }
    }
    if (foundone) NodeScan = NodeScan->next; /* for non-contiguous nodes */
    node++;
  }
  Elements = E;
  Nodes = N;
}

#endif /* LOOKUP_INITIALIZATION */

int
CheckLegalElementPartition(struct ElementClass *head)
{
  int found;
  struct ElementClass *scan;
  struct Element *E;
  int C1, C2;

  /* now check for bad element classes */
  found = 0;
  for (scan = head; scan != NULL; scan = scan->next) {
    
    if (scan->count == 2) continue;
    C1 = C2 = 0;
    for (E = scan->elements; E != NULL; E = E->next) {
      if (E->graph == Circuit1->file) C1++;
      else C2++;
    }
    scan->count = C1 + C2;
    if (C1 != C2) {
      found = 1;
      BadMatchDetected = 1;
      scan->legalpartition = 0;
    }
  }
  return found;
}
    

struct ElementClass *MakeElist(struct Element *E)
/* traverses a list of elements.  Puts all elements having the
   same hashval into the same class.  Returns a pointer to a list
   of element classes, each of which contains a list of elements.
*/
{
  struct ElementClass *head, *new_elementclass, *scan,
                      *bad_elementclass, *tail;
  struct Element *enext;
  int found;

  head = NULL;
  while (E != NULL) {
    found = 0;
    enext = E->next;
    for (scan = head; scan != NULL && !found; scan = scan->next)
      if (scan->magic == E->hashval) {
	      found = 1;
	      break; /* get out of for loop without changing scan */
      }
    if (!found) {
      /* need to create a new one, and prepend to list */
      new_elementclass = GetElementClass();
      if (new_elementclass == NULL) {
	Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	PrintCoreStats();
#endif
	ResetState();
	return NULL;
      }
      new_elementclass->magic = E->hashval;
      new_elementclass->next = head;
      head = new_elementclass;
      scan = head;
    }
    /* prepend to list already present */
    E->next = scan->elements;
    E->elemclass = scan;
    scan->elements = E;
    scan->count++;  /* just added by MAS */
      
    E = enext;
  }

  if (!CheckLegalElementPartition(head))
     return(head);  /* we are done */

  /* now regroup all the illegal partitions into a single class */
  bad_elementclass = GetElementClass();
  bad_elementclass->legalpartition = 0;
  for (scan = head; scan != NULL; scan = scan->next) {
    struct Element *E, *enext;

    if (!(scan->legalpartition)) {
      for (E = scan->elements; E != NULL; ) {
	enext = E->next;
	/* prepend to list already present */
	E->next = bad_elementclass->elements;
	E->elemclass = bad_elementclass;
	bad_elementclass->elements = E;
	bad_elementclass->count++;  /* just added by MAS */

	E = enext;
      }
    }    
  }
  /* eat all empty element classes */
  tail = bad_elementclass;
  for (scan = head; scan != NULL; ) {
    struct ElementClass *badclass;

    if (!(scan->legalpartition)) {
      badclass = scan;
      scan = scan->next;
      FreeElementClass(badclass);
    }
    else {
      tail->next = scan;
      scan = scan->next;
      tail->next->next = NULL;
      tail = tail->next;
    }
  }
  head = bad_elementclass;

  if (head->next != NULL)
    NewFracturesMade = 1;  /* list did fracture into more than one class */

  
  return (head);
}

int
CheckLegalNodePartition(struct NodeClass *head)
{
  struct NodeClass *scan;
  int found;
  struct Node *N;
  int C1, C2;

  /* now check for bad node classes */
  found = 0;
  for (scan = head; scan != NULL; scan = scan->next) {
    
    if (scan->count == 2) continue;
    C1 = C2 = 0;
    for (N = scan->nodes; N != NULL; N = N->next) {
      if (N->graph == Circuit1->file) C1++;
      else C2++;
    }
    scan->count = C1 + C2;
    if (C1 != C2) {
      /* we have an illegal partition */
      found = 1;
      BadMatchDetected = 1;
      scan->legalpartition = 0;
    }
  }
  return found;
}

struct NodeClass *MakeNlist(struct Node *N)
{
  struct NodeClass *head, *new_nodeclass, *scan, *bad_nodeclass, *tail;
  struct Node *nnext;
  int found;

  head = NULL;
  while (N != NULL) {
    found = 0;
    nnext = N->next;
    for (scan = head; scan != NULL && !found; scan = scan->next)
      if (scan->magic == N->hashval) {
	      found = 1;
	      break; /* get out of for loop without changing scan */
      }
    if (!found) {
      /* need to create a new one, and prepend to list */
      new_nodeclass = GetNodeClass();
      if (new_nodeclass == NULL) {
	Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	PrintCoreStats();
#endif
	ResetState();
	return NULL;
      }
      new_nodeclass->magic = N->hashval;
      new_nodeclass->next = head;
      head = new_nodeclass;
      scan = head;
    }
    /* prepend to list already present */
    N->next = scan->nodes;
    N->nodeclass = scan;
    scan->nodes = N;
    scan->count++;
      
    N = nnext;
  }

  if (!CheckLegalNodePartition(head))
     return(head);  /* we are done */
    
  /* now regroup all the illegal partitions into a single class */
  bad_nodeclass = GetNodeClass();
  bad_nodeclass->legalpartition = 0;
  for (scan = head; scan != NULL; scan = scan->next) {
    struct Node *N, *nnext;

    if (!(scan->legalpartition)) {
      for (N = scan->nodes; N != NULL; ) {
	nnext = N->next;
	/* prepend to list already present */
	N->next = bad_nodeclass->nodes;
	N->nodeclass = bad_nodeclass;
	bad_nodeclass->nodes = N;
	bad_nodeclass->count++;  /* just added by MAS */

	N = nnext;
      }
    }    
  }
  /* eat all empty element classes */
  tail = bad_nodeclass;
  for (scan = head; scan != NULL; ) {
    struct NodeClass *badclass;

    if (!(scan->legalpartition)) {
      badclass = scan;
      scan = scan->next;
      FreeNodeClass(badclass);
    }
    else {
      tail->next = scan;
      scan = scan->next;
      tail->next->next = NULL;
      tail = tail->next;
    }
  }
  head = bad_nodeclass;

  if (head->next != NULL)
	NewFracturesMade = 1;  /* list did fracture into more than one class */
  return (head);
}


/* need to choose MAX_RANDOM to fit inside an 'int' field */
#define MAX_RANDOM (INT_MAX)
/* #define MAX_RANDOM ((1L << 19) - 1) */

#define Magic(a) (a = Random(MAX_RANDOM))
#define MagicSeed(a) RandomSeed(a)

int FractureElementClass(struct ElementClass **Elist)
/* returns the number of new classes that were created */
{
  struct ElementClass *Eclass, *Ehead, *Etail, *Enew, *Enext;

  Ehead = Etail = NULL;
  /* traverse the list, fracturing as required, and freeing EC to recycle */
  Eclass = *Elist;
  while (Eclass != NULL) {
    Enext = Eclass->next;
    if (Eclass->count != 2 || ExhaustiveSubdivision) {
       Enew = MakeElist(Eclass->elements);
       FreeElementClass(Eclass);
       if (Ehead == NULL) {
	  Ehead = Etail = Enew;
	  Magic(Etail->magic);
       }
       else Etail->next = Enew;
       while (Etail->next != NULL) {
	  Etail = Etail->next;
	  /* don't forget to assign new magic numbers to the new elements */
          Magic(Etail->magic);
       }
    }
    else {
       Enew = Eclass;
       Enew->next = NULL;
       if (Ehead == NULL) Ehead = Etail = Enew;
       else Etail->next = Enew;
       Etail = Enew;
    }
    Eclass = Enext;
  }
  *Elist = Ehead;
  NewNumberOfEclasses = 0;
  for (Eclass = *Elist; Eclass != NULL; Eclass = Eclass->next) 
	  NewNumberOfEclasses++;

  if (Debug == TRUE) {
     if (Iterations == 0) Fprintf(stdout, "\n");
     Fprintf(stdout, "Iteration: %3d: Element classes = %4d (+%d);", Iterations,
	  	NewNumberOfEclasses, NewNumberOfEclasses - OldNumberOfEclasses);
     Ftab(stdout, 50);
  }

  /* make the New* things deltas */
  NewNumberOfEclasses -= OldNumberOfEclasses;
  OldNumberOfEclasses += NewNumberOfEclasses;
  return(NewNumberOfEclasses); 
}

int FractureNodeClass(struct NodeClass **Nlist)
/* returns the number of new classes that were created */
{  struct NodeClass *Nclass, *Nhead, *Ntail, *Nnew, *Nnext;
  Nhead = Ntail = NULL;
  /* traverse the list, fracturing as required, and freeing NC to recycle */
  Nclass = *Nlist;
  while (Nclass != NULL) {
    Nnext = Nclass->next;	  
    if (Nclass->count != 2 || ExhaustiveSubdivision) {
       Nnew = MakeNlist(Nclass->nodes);
       FreeNodeClass(Nclass);
       if (Nhead == NULL) {
	  Nhead = Ntail = Nnew;
	  Magic(Ntail->magic);
       }
       else Ntail->next = Nnew;
       while (Ntail->next != NULL) {
          Ntail = Ntail->next;
	  /* don't forget to assign new magic numbers to the new elements */
          Magic(Ntail->magic);
	}
    }
    else {
       Nnew = Nclass;
       Nnew->next = NULL;
       if (Nhead == NULL) Nhead = Ntail = Nnew;
       else Ntail->next = Nnew;
       Ntail = Nnew;
    }
    Nclass = Nnext;
  }
  *Nlist = Nhead;
  NewNumberOfNclasses = 0;
  for (Nclass = *Nlist; Nclass != NULL; Nclass = Nclass->next)
	  NewNumberOfNclasses++;

  if (Debug == TRUE) {
    Fprintf(stdout, "Node classes = %4d (+%d)\n",
	  NewNumberOfNclasses, NewNumberOfNclasses - OldNumberOfNclasses);
  }

  /* make the New* things deltas */
  NewNumberOfNclasses -= OldNumberOfNclasses;
  OldNumberOfNclasses += NewNumberOfNclasses;
  return(NewNumberOfNclasses);
}

/* Structure used by lookupclass */

typedef struct _chd {
   int file;
   unsigned long classhash;
} chdata;

/*----------------------------------------------------------------------*/
/* Callback function used by LookupClassEquivalent			*/
/*----------------------------------------------------------------------*/

struct nlist *lookupclass(struct hashlist *p, chdata *chd)
{
    struct nlist *ptr;

    ptr = (struct nlist *)(p->ptr);

    if (ptr->file == chd->file)
	if (ptr->classhash == chd->classhash)
	    return ptr;

    return NULL;
}

/*----------------------------------------------------------------------*/
/* Given the class (cellname) "model" in file "file1", find the		*/
/* equivalent class in "file2".  This is done by exhaustively searching	*/
/* the cell database for a matching classhash number.  It is therefore	*/
/* not intended to be run often;  it should be only run when choosing	*/
/* two cells to compare.						*/
/*----------------------------------------------------------------------*/

struct nlist *LookupClassEquivalent(char *model, int file1, int file2)
{
   struct nlist *tp, *tp2;
   chdata chd;

   tp = LookupCellFile(model, file1);

   chd.file = file2;
   chd.classhash = tp->classhash;

   tp2 = RecurseCellHashTable2(lookupclass, (void *)(&chd));
   return tp2;
}

/*----------------------------------------------------------------------*/
/* Look up the cell in file2 which has been specified as a match for	*/
/* cell tc1 using the "equate classes" command.  If there is no match,	*/
/* then return NULL.  If there is a match, then set the	classhash	*/
/* values to be equal to each other.					*/
/*----------------------------------------------------------------------*/

struct nlist *LookupPrematchedClass(struct nlist *tc1, int file2)
{
   struct Correspond *crec;
   struct nlist *tc2;

   for (crec = ClassCorrespondence; crec != NULL; crec = crec->next) {
      if (crec->file1 == tc1->file) {
	 if ((*matchfunc)(tc1->name, crec->class1))
	    if (crec->file2 == file2 || crec->file2 == -1)
	       tc2 = LookupCellFile(crec->class2, file2);
      }
      else if (crec->file1 == file2) {
	 if ((*matchfunc)(tc1->name, crec->class2))
	    if (crec->file2 == tc1->file || crec->file2 == -1)
	       tc2 = LookupCellFile(crec->class1, file2);
      }
      else if (crec->file2 == tc1->file) {
	 if ((*matchfunc)(tc1->name, crec->class2))
	    if (crec->file1 == -1)
	       tc2 = LookupCellFile(crec->class1, file2);
      }
      else if (crec->file2 == file2) {
	 if ((*matchfunc)(tc1->name, crec->class1))
	    if (crec->file1 == -1)
	       tc2 = LookupCellFile(crec->class2, file2);
      }
      else if (crec->file1 == -1 && crec->file2 == -1) {
	 if ((*matchfunc)(tc1->name, crec->class1))
	    tc2 = LookupCell(crec->class2);
	 else if ((*matchfunc)(tc1->name, crec->class2))
	    tc2 = LookupCell(crec->class1);
      }
   }
   if (tc2 != NULL) tc2->classhash = tc1->classhash;
   return tc2;
}

/*----------------------------------------------------------------------*/
/* Attempt to define FirstElementPass that will generate element classes */
/* by names of pins, which will allow elements with different cell names */
/* in different circuits to be grouped.  Ultimately we want a solution	*/
/* that allows classes to be forced to be equated.			*/
/*----------------------------------------------------------------------*/

void FirstElementPass(struct Element *E)
{
  struct Element *Esrch, *Ecorr;
  struct NodeList *n;
  struct nlist *tp1, *tp2, *tp;
  int C1, C2, i;
  char ostr[89];

  if (Debug == 0) {
     Fprintf(stdout, "\nSubcircuit summary:\n");
     *(ostr + 43) =  '|';
     *(ostr + 87) = '\n';
     *(ostr + 88) = '\0';
     for (i = 0; i < 43; i++) *(ostr + i) = ' ';
     for (i = 44; i < 87; i++) *(ostr + i) = ' ';

     snprintf(ostr, 43, "Circuit 1: %s", Circuit1->name);
     snprintf(ostr + 44, 43, "Circuit 2: %s", Circuit2->name);
     for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
     Fprintf(stdout, ostr);
     for (i = 0; i < 43; i++) *(ostr + i) = '-';
     for (i = 44; i < 87; i++) *(ostr + i) = '-';
     Fprintf(stdout, ostr);

     // Print side-by-side comparison of elements based on class correspondence
     // Use the hashval record to mark what entries have been processed already.

     for (Esrch = E; Esrch != NULL; Esrch = Esrch->next) {
        if (Esrch->graph == Circuit1->file && Esrch->hashval == 0) {
	   Esrch->hashval = 1;
	   C1 = 1;
	   C2 = 0;
	   tp1 = LookupCellFile(Esrch->object->model, Circuit1->file);
	   tp2 = LookupClassEquivalent(Esrch->object->model, Circuit1->file,
			Circuit2->file);
	   for (Ecorr = E; Ecorr != NULL; Ecorr = Ecorr->next) {
	      if (Ecorr->hashval == 0) {
	         if (Ecorr->graph == Circuit2->file) {
		    tp = LookupCellFile(Ecorr->object->model, Circuit2->file);
		    if (tp == tp2) {
		       Ecorr->hashval = 1;
		       C2++;
	            }
	         }
	         else if (Ecorr->graph == Circuit1->file) {
		    tp = LookupCellFile(Ecorr->object->model, Circuit1->file);
		    if (tp == tp1) {
		       Ecorr->hashval = 1;
		       C1++;
		    }
	         }
	      }
	   }
	   for (i = 0; i < 43; i++) *(ostr + i) = ' ';
	   for (i = 44; i < 87; i++) *(ostr + i) = ' ';
           snprintf(ostr, 43, "%s (%d)", Esrch->object->model, C1);
	   if (C2 > 0)
              snprintf(ostr + 44, 43, "%s (%d)%s", tp2->name, C2,
			(C2 == C1) ? "" : " **Mismatch**");
	   else
              snprintf(ostr + 44, 43, "(no matching element)");
           for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
           Fprintf(stdout, ostr);
        }
     }
     for (Esrch = E; Esrch != NULL; Esrch = Esrch->next) {
        if (Esrch->graph == Circuit2->file && Esrch->hashval == 0) {
	   Esrch->hashval = 1;
	   C2 = 1;
	   tp2 = LookupCellFile(Esrch->object->model, Circuit2->file);
	   tp1 = LookupClassEquivalent(Esrch->object->model, Circuit2->file,
			Circuit1->file);
	   for (Ecorr = E; Ecorr != NULL; Ecorr = Ecorr->next) {
	      if (Ecorr->hashval == 0) {
	         if (Ecorr->graph == Circuit2->file) {
		    tp = LookupCellFile(Ecorr->object->model, Circuit2->file);
		    if (tp == tp2) {
		       Ecorr->hashval = 1;
		       C2++;
	            }
	         }
	      }
	   }
	   for (i = 0; i < 43; i++) *(ostr + i) = ' ';
	   for (i = 44; i < 87; i++) *(ostr + i) = ' ';
           snprintf(ostr, 43, "(no matching element)");
           snprintf(ostr + 44, 43, "%s (%d)", Esrch->object->model, C2);
           for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
           Fprintf(stdout, ostr);
	}
     }
  }
  
  C1 = C2 = 0;
  while (E != NULL) {

    /* initialize random no. gen. to model-specific number */
    tp = LookupCellFile(E->object->model, E->graph);
    MagicSeed(tp->classhash);

    for (n = E->nodelist; n != NULL; n = n->next)
       Magic(n->pin_magic);

    E->hashval = tp->classhash;
    if (E->graph == Circuit1->file) C1++;
    else C2++;
    E = E->next;
  }
  if (Debug == TRUE) {
     if (C1 != C2)
        Printf("Element Mismatch: Circuit 1 has %d, Circuit 2 has %d.\n",C1,C2);
  }
  else {
     for (i = 0; i < 43; i++) *(ostr + i) = ' ';
     for (i = 44; i < 87; i++) *(ostr + i) = ' ';
     snprintf(ostr, 43, "Number of elements: %d%s", C1, (C1 == C2) ? "" :
		" **Mismatch**");
     snprintf(ostr + 44, 43, "Number of elements: %d%s", C2, (C1 == C2) ? "" :
		" **Mismatch**");
     for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
     Fprintf(stdout, ostr);
  }
}

void FirstNodePass(struct Node *N)
{
  struct ElementList *E;
  int fanout;
  int C1, C2;
  
  C1 = C2 = 0;
  while (N != NULL) {
    fanout = 0;
    for (E = N->elementlist; E != NULL; E = E->next)  fanout++;
    N->hashval = fanout;
    if (N->graph == Circuit1->file) C1++;
    else C2++;
    N = N->next;
  }
  if (Debug == TRUE) {
     if (C1 != C2)
        Fprintf(stderr, "Node Mismatch: Circuit 1 has %d, Circuit 2 has %d.\n",C1,C2);
  }
  else {
     char ostr[89];
     int i;

     *(ostr + 43) =  '|';
     *(ostr + 87) = '\n';
     *(ostr + 88) = '\0';

     for (i = 0; i < 43; i++) *(ostr + i) = ' ';
     for (i = 44; i < 87; i++) *(ostr + i) = ' ';
     snprintf(ostr, 43, "Number of nodes: %d%s", C1, (C1 == C2) ? "" :
		" **Mismatch**");
     snprintf(ostr + 44, 43, "Number of nodes: %d%s", C2, (C1 == C2) ? "" :
		" **Mismatch**");
     for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
     Fprintf(stdout, ostr);

     for (i = 0; i < 87; i++) *(ostr + i) = '-';
     Fprintf(stdout, ostr);
  }
}

/*--------------------------------------------------------------*/
/* Declare cells to be non-matching by clearing the		*/
/* CELL_MATCHED	flag in both cells.  This will force cell	*/
/* flattening during hierarchical LVS.				*/
/*--------------------------------------------------------------*/

void MatchFail(char *name1, char *name2)
{
   struct nlist *tc1, *tc2;

   tc1 = LookupCell(name1);
   tc2 = LookupCell(name2);

   tc1->flags &= ~CELL_MATCHED;
   tc2->flags &= ~CELL_MATCHED;
}

/*--------------------------------------------------------------*/

int FlattenUnmatched(struct nlist *tc, char *parent, int stoplevel, int loclevel)
{
   struct nlist *tcsub;
   struct objlist *ob;
   int changed = 1;

   if (loclevel == stoplevel && !(tc->flags & CELL_MATCHED)) {
      ClearDumpedList();
      if (Debug == TRUE) Fprintf(stdout, "Level %d ", loclevel);
      Fprintf(stdout, "Flattening unmatched subcell %s in circuit %s\n",
				tc->name, parent);
      flattenInstancesOf(parent, tc->file, tc->name);
      return 1;
   }

   if (tc->cell == NULL) return 0;

   while (changed) {
      for (ob = tc->cell; ob != NULL; ob = ob->next) {
	 changed = 0;
         tcsub = NULL;
         if (ob->type == FIRSTPIN) {
	    /* First check if there is a class equivalent */
	    tcsub = LookupCellFile(ob->model, tc->file);
	    if (!tcsub || (tcsub->class != CLASS_SUBCKT)) continue;
	    else if (tcsub == tc) continue;
	    if (FlattenUnmatched(tcsub, tc->name, stoplevel, loclevel + 1)) {
	       changed = 1;
	       break;
	    }
	 }
      }
   }

   return 0;
}

/*--------------------------------------------------------------*/

void DescendCountQueue(struct nlist *tc, int *level, int loclevel)
{
   struct nlist *tcsub;
   struct objlist *ob;

   if (loclevel > *level) *level = loclevel;

   for (ob = tc->cell; ob != NULL; ob = ob->next) {
      tcsub = NULL;
      if (ob->type == FIRSTPIN) {
	 /* First check if there is a class equivalent */
	 tcsub = LookupCellFile(ob->model, tc->file);
	 if (!tcsub || (tcsub->class != CLASS_SUBCKT)) continue;
	 else if (tcsub == tc) continue;
	 DescendCountQueue(tcsub, level, loclevel + 1);
      }
   }
}

/*--------------------------------------------------------------*/

void DescendCompareQueue(struct nlist *tc, struct nlist *tctop, int stoplevel,
		int loclevel, int flip)
{
   struct nlist *tcsub, *tc2;
   struct objlist *ob;
   struct Correspond *scomp, *newcomp;

   if (loclevel == stoplevel && !(tc->flags & CELL_MATCHED)) {

      // Find exact-name equivalents or cells that have been specified
      // as equivalent using the "equate class" command.

      tc2 = LookupClassEquivalent(tc->name, tc->file, tctop->file);
      if (tc2 == NULL) {
	 // Check if cell names were forced to be matched using the
	 // "equate classes" command.
	 tc2 = LookupPrematchedClass(tc, tctop->file);
      }
      if (tc2 != NULL) {
	 newcomp = (struct Correspond *)CALLOC(1, sizeof(struct Correspond));
	 newcomp->next = NULL;
	 if (flip) {
	    newcomp->class1 = tc2->name;
	    newcomp->file1 = tc2->file;
	    newcomp->class2 = tc->name;
	    newcomp->file2 = tc->file;
	 }
	 else {
	    newcomp->class1 = tc->name;
	    newcomp->file1 = tc->file;
	    newcomp->class2 = tc2->name;
	    newcomp->file2 = tc2->file;
	 }
	
         if (Debug == TRUE)
	    Fprintf(stdout, "Level %d Appending %s %s to compare queue\n",
			loclevel, tc->name, tc2->name);

	 /* Add this pair to the end of the list of cells to compare */
	 if (CompareQueue == NULL)
	    CompareQueue = newcomp;
	 else {
	    for (scomp = CompareQueue; scomp->next; scomp = scomp->next);
	    scomp->next = newcomp;
	 }
	 tc->flags |= CELL_MATCHED;
	 tc2->flags |= CELL_MATCHED;
      }
      else if (Debug == TRUE)
	 Fprintf(stdout, "Level %d Class %s is unmatched and will be flattened\n",
			loclevel, tc->name);
      return;
   }

   /* Now work through the subcircuits of tc */

   for (ob = tc->cell; ob != NULL; ob = ob->next) {
      tcsub = NULL;
      if (ob->type == FIRSTPIN) {
	 tcsub = LookupCellFile(ob->model, tc->file);
	 if (!tcsub || (tcsub->class != CLASS_SUBCKT)) continue;
	 else if (tcsub == tc) continue;
	 DescendCompareQueue(tcsub, tctop, stoplevel, loclevel + 1, flip);
      }
   }
}

/*--------------------------------------------------------------*/
/* Parse the top-level cells for hierarchical structure, and	*/
/* generate a stack of cells to compare.  This is done		*/
/* carefully from bottom up so as to raise the likelihood that	*/
/* we are comparing two circuits that really are supposed to	*/
/* be equivalent.  Comparisons are then done from bottom up so	*/
/* that anything that isn't matched can be flattened as we go;	*/
/* in the worst case, if no subcells can be definitively	*/
/* matched, then we will end up running a comparison on two	*/
/* completely flattened netlists.				*/
/*								*/
/* Return 0 on success, 1 on failure to look up cell name1,	*/
/* and 2 on failure to look up cell name2.			*/
/*--------------------------------------------------------------*/


int CreateCompareQueue(char *name1, int file1, char *name2, int file2)
{
   struct nlist *tc1, *tc2, *tcsub1, *tcsub2;
   struct Correspond *newcomp, *scomp;
   int level;

   tc1 = LookupCellFile(name1, file1);
   tc2 = LookupCellFile(name2, file2);

   if (tc1 == NULL) return 1;
   if (tc2 == NULL) return 2;

   level = 0;

   /* Recurse the hierarchies of tc1 and tc2 and find the deepest level */
   DescendCountQueue(tc1, &level, 0);
   DescendCountQueue(tc2, &level, 0);

   /* Starting at the deepest level, compare each circuit to the other	*/
   /* When each level has put as many cells as it can find on the	*/
   /* compare queue, then flatten all the unmatched cells in that level	*/

   while (level > 0) {
      if (Debug == TRUE)
         Fprintf(stdout, "Descend level %d circuit 1\n", level);
      DescendCompareQueue(tc1, tc2, level, 0, 0);
      if (Debug == TRUE)
         Fprintf(stdout, "Descend level %d circuit 2\n", level);
      DescendCompareQueue(tc2, tc1, level, 0, 1);

      if (Debug == TRUE)
         Fprintf(stdout, "Flatten level %d circuit 1\n", level);
      FlattenUnmatched(tc1, name1, level, 0);
      if (Debug == TRUE)
         Fprintf(stdout, "Flatten level %d circuit 2\n", level);
      FlattenUnmatched(tc2, name2, level, 0);
      level--;
   }

   /* Add the topmost cells to the end of the compare queue */

   newcomp = (struct Correspond *)CALLOC(1, sizeof(struct Correspond));
   newcomp->next = NULL;
   newcomp->class1 = tc1->name;
   newcomp->file1 = tc1->file;
   newcomp->class2 = tc2->name;
   newcomp->file2 = tc2->file;

   if (CompareQueue == NULL)
      CompareQueue = newcomp;
   else {
      for (scomp = CompareQueue; scomp->next; scomp = scomp->next);
      scomp->next = newcomp;
   }

   tc1->flags |= CELL_MATCHED;
   tc2->flags |= CELL_MATCHED;

   return 0;
}

/*----------------------------------------------*/
/* Read the top of the compare queue, but do	*/
/* not alter the stack.				*/
/*----------------------------------------------*/

int PeekCompareQueueTop(char **name1, int *file1, char **name2, int *file2)
{
   if (CompareQueue == NULL)
      return -1;

   *name1 = CompareQueue->class1;
   *file1 = CompareQueue->file1;
   *name2 = CompareQueue->class2;
   *file2 = CompareQueue->file2;

   return 0;
}

/*----------------------------------------------*/
/* Return the top of the compare queue stack,	*/
/* and pop the topmost entry.  Return 0 on	*/
/* success, -1 if the compare queue is empty.	*/
/*----------------------------------------------*/

int GetCompareQueueTop(char **name1, int *file1, char **name2, int *file2)
{
   struct Correspond *nextcomp;

   if (PeekCompareQueueTop(name1, file1, name2, file2) < 0)
      return -1;

   nextcomp = CompareQueue->next; 
   FREE(CompareQueue);
   CompareQueue = nextcomp;
   return 0;
}

/*--------------------------------------*/
/* Delete the comparison queue		*/
/*--------------------------------------*/

void RemoveCompareQueue()
{
   struct Correspond *comp, *nextcomp;

   for (comp = CompareQueue; comp != NULL;) {
      nextcomp = comp->next;
      FREE(comp);
      comp = nextcomp;
   }
   CompareQueue = NULL;
}

/*----------------------------------*/
/* create an initial data structure */
/*----------------------------------*/

void CreateTwoLists(char *name1, int file1, char *name2, int file2)
{
  struct Element *El1;
  struct Node *N1;
  struct nlist *tc1, *tc2;

  ResetState();

  /* print preliminary statistics */
  Printf("Contents of circuit 1:  ");
  DescribeInstance(name1, file1);
  Printf("Contents of circuit 2:  ");
  DescribeInstance(name2, file2);
  Printf("\n");

  if (file1 == -1)
     tc1 = LookupCell(name1);
  else
     tc1 = LookupCellFile(name1, file1);

  if (file2 == -1)
     tc2 = LookupCell(name2);
  else
     tc2 = LookupCellFile(name2, file2);

  /* determine if matching will be case sensitive or case insensitive */
  matchfunc = match;
  matchintfunc = matchfile;
  hashfunc = hash;
  if (tc1 != NULL && tc2 != NULL) {
     if ((tc1->flags & CELL_NOCASE) && (tc2->flags & CELL_NOCASE)) {
	matchfunc = matchnocase;
	matchintfunc = matchfilenocase;
	hashfunc = hashnocase;
     }
  }

  CreateLists(name1, file1);
  if (Elements == NULL) {
    Printf("Cell %s contains no elements.\n", name1);
    return;
  }
  if (Nodes == NULL) {
    Printf("Cell %s contains no nodes.\n", name1);
    return;
  }

  ElementClasses = GetElementClass();
  if (ElementClasses == NULL) {
    Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
    PrintCoreStats();
#endif
    ResetState();
    return;
  }
  ElementClasses->elements = Elements;
  Magic(ElementClasses->magic);

  for (El1 = Elements; El1->next != NULL; El1 = El1->next) {
    El1->elemclass = ElementClasses;
  }
  /* El1 now points to last element of list */

  NodeClasses = GetNodeClass();
  if (NodeClasses == NULL) {
    Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
    PrintCoreStats();
#endif
    ResetState();
    return;
  }
  NodeClasses->nodes = Nodes;
  Magic(NodeClasses->magic);

  for (N1 = Nodes; N1->next != NULL; N1 = N1->next) {
    N1->nodeclass = NodeClasses;
  }
  /* N1 now points to last element of list */


  CreateLists(name2, file2);
  if (Elements == NULL) {
    Printf("Cell %s contains no elements.\n", name2);
    ResetState();
    return;
  }

  if (Nodes == NULL) {
    Printf("Cell %s contains no nodes.\n", name2);
    ResetState();
    return;
  }

  /* splice new lists into existing lists */
  El1->next = Elements;
  for (El1 = Elements; El1->next != NULL; El1 = El1->next) {
    El1->elemclass = ElementClasses;
  }

  N1->next = Nodes;
  for (N1 = Nodes; N1->next != NULL; N1 = N1->next) {
    N1->nodeclass = NodeClasses;
  }

  /* print preliminary statistics */
  SummarizeDataStructures();
  
  /* perform first set of fractures */
  FirstElementPass(ElementClasses->elements);
  FirstNodePass(NodeClasses->nodes);
  FractureElementClass(&ElementClasses);
  FractureNodeClass(&NodeClasses);
}

void RegroupDataStructures(void)
{
  struct ElementClass *EC;
  struct Element *El1, *Etail;
  struct NodeClass *NC;
  struct Node *N1, *Ntail;
  

  if (ElementClasses == NULL || NodeClasses == NULL) {
    Printf( "Need to initialize data structures first!\n");
    return;
  }

  Elements = Etail = NULL;
  for (EC = ElementClasses; EC != NULL; ) {
    struct ElementClass *ECnext;
    ECnext = EC->next;
    if (Elements == NULL) Elements = EC->elements;
    else Etail->next = EC->elements;
    for (El1= EC->elements; El1 != NULL && El1->next != NULL; El1 = El1->next);
    Etail = El1;
    FreeElementClass(EC);
    EC = ECnext;
  }

  ElementClasses = GetElementClass();
  if (ElementClasses == NULL) {
    Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
    PrintCoreStats();
#endif
    ResetState();
    return;
  }
  ElementClasses->elements = Elements;

  for (El1 = Elements; El1->next != NULL; El1 = El1->next) {
    El1->elemclass = ElementClasses;
  }
  /* El1 now points to last element of list */


  Nodes = Ntail = NULL;
  for (NC = NodeClasses; NC != NULL; ) {
    struct NodeClass *NCnext;
    NCnext = NC->next;
    if (Nodes == NULL) Nodes = NC->nodes;
    else Ntail->next = NC->nodes;
    for (N1 = NC->nodes; N1 != NULL && N1->next != NULL; N1 = N1->next);
    Ntail = N1;
    FreeNodeClass(NC);
    NC = NCnext;
  }


  NodeClasses = GetNodeClass();
  if (NodeClasses == NULL) {
    Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
    PrintCoreStats();
#endif
    ResetState();
    return;
  }
  NodeClasses->nodes = Nodes;

  for (N1 = Nodes; N1->next != NULL; N1 = N1->next) {
    N1->nodeclass = NodeClasses;
  }

  /* reset magic numbers */
  NewNumberOfEclasses = OldNumberOfEclasses = 0;
  NewNumberOfNclasses = OldNumberOfNclasses = 0;
  Iterations = 0;

  /* perform first set of fractures */
  FirstElementPass(ElementClasses->elements);
  FirstNodePass(NodeClasses->nodes);
  FractureElementClass(&ElementClasses);
  FractureNodeClass(&NodeClasses);
}

unsigned long ElementHash(struct Element *E)
{
  struct NodeList *N;
  unsigned long hashval = 0;
  
  for (N = E->nodelist; N != NULL; N = N->next)
     if (N->node != NULL)
        hashval += (N->pin_magic ^ N->node->nodeclass->magic);

  // Added by Tim 10/10/2012.  Cannot ignore own hashval, or else
  // two instances of different cells can be swapped and although
  // the netlist will report as not matching, the illegal partition
  // will be recast as legal, and no information will be generated
  // about the mismatched elements.
  hashval ^= E->hashval;

  return(hashval);
}

unsigned long NodeHash(struct Node *N)
{
  struct ElementList *E;
  unsigned long hashval = 0;
  
  for (E = N->elementlist; E != NULL; E = E->next)
     hashval += (E->subelement->pin_magic ^
		E->subelement->element->hashval ^	// Added by Tim
		E->subelement->element->elemclass->magic);

  return(hashval);
}

int Iterate(void)
/* does one iteration, and returns TRUE if we are done */
{
  int notdone;
  struct ElementClass *EC;
  struct NodeClass *NC;

  if (ElementClasses == NULL || NodeClasses == NULL) {
    Printf( "Need to initialize data structures first!\n");
    return(1);
  }

  for (EC = ElementClasses; EC != NULL; EC = EC->next) 
    Magic(EC->magic);
  for (NC = NodeClasses; NC != NULL; NC = NC->next) 
    Magic(NC->magic);

  Iterations++;
  NewFracturesMade = 0;
  
  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
    struct Element *E;
    for (E = EC->elements; E != NULL; E = E->next)
      E->hashval = ElementHash(E);

    // Check for partitions of two elements, not balanced
    if (EC->count == 2 && EC->elements->graph ==
		EC->elements->next->graph)
       EC->legalpartition = 0;
  }

  notdone = FractureElementClass(&ElementClasses);

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    struct Node *N;
    for (N = NC->nodes; N != NULL; N = N->next)
      N->hashval = NodeHash(N);

    // Check for partitions of two nodes, not balanced
    if (NC->count == 2 && NC->nodes->graph ==
		NC->nodes->next->graph)
       NC->legalpartition = 0;
  }
  notdone = notdone | FractureNodeClass(&NodeClasses);


#if 0
  if (NewFracturesMade) Printf("New fractures made;   ");
  else Printf("No new fractures made; ");
  if (BadMatchDetected) Printf("Matching error has been detected.");
  if (PropertyErrorDetected) Printf("Property error has been detected.");
  Printf("\n");
#endif

  return(!notdone);
}

/*--------------------------------------------------------------*/
/* Print results of property checks				*/
/*--------------------------------------------------------------*/

void PrintPropertyResults(void)
{
  struct ElementClass *EC;

  for (EC = ElementClasses; EC != NULL; EC = EC->next)
    PropertyCheck(EC, 1);
}

/*--------------------------------------------------------------*/
/* Check device properties of one element class against the	*/
/* other.  Use graph1 for the reference property names.		*/
/* EC is assumed to have only one element from each class.	*/
/* Automorphisms (element classes with multiple entries) are	*/
/* handled separately by re-partitioning the element class	*/
/* based on the property values.				*/
/*								*/
/* Return -1 on complete failure (bad element class), or else	*/
/* return the number of mismatched properties (0 = perfect	*/
/* match of all properties).					*/
/*--------------------------------------------------------------*/

int PropertyCheck(struct ElementClass *EC, int do_print)
{
   struct Element *E1, *E2;
   struct nlist *tc1, *tc2;
   struct objlist *tp1, *tp2;
   char *key1, *key2;
   int mismatches = 0;
   struct keylist *kl;
   struct valuelist *vl1, *vl2;

   /* This element class should contain exactly two entries,	*/
   /* one belonging to each graph.				*/

   if ((E1 = EC->elements) == NULL) return -1;
   if ((E2 = EC->elements->next) == NULL) return -1;
   if (E2->next != NULL) return -1;
   if (E1->graph == E2->graph) return -1;

   if (E1->graph != Circuit1->file) {	/* Ensure that E1 is Circuit1 */
      struct Element *Etmp = E1;
      E1 = E2;
      E2 = Etmp;
   }

   tc1 = LookupCellFile(E1->object->model, Circuit1->file);
   tc2 = LookupCellFile(E2->object->model, Circuit2->file);

   if (tc1->classhash != tc2->classhash) return -1;

   for (tp1 = E1->object; (tp1 != NULL) && tp1->type >= FIRSTPIN; tp1 = tp1->next);
   for (tp2 = E2->object; (tp2 != NULL) && tp2->type >= FIRSTPIN; tp2 = tp2->next);

   /* Check if there are any properties to match */

   if ((tp1 == NULL) && (tp2 == NULL)) return 0;
   if (tp1 == NULL) {
      if (tp2->type != PROPERTY) return 0;  
      else return -1;
   }
   else if (tp2 == NULL) {
      if (tp1->type != PROPERTY) return 0;
      else return -1;
   }
   if ((tp1->type != PROPERTY) && (tp2->type != PROPERTY)) return 0;  
   if ((tp1->type != PROPERTY) || (tp2->type != PROPERTY)) return -1;

   vl1 = (struct valuelist *)tp1->instance;
   vl2 = (struct valuelist *)tp2->instance;
   for (kl = tc1->proplist; kl != NULL; kl = kl->next) {
      if (vl1 == NULL || vl2 == NULL) break;
      switch (kl->type) {
	 case PROP_DOUBLE:
	    if ((fabs(vl1->value.dval - vl2->value.dval) / vl1->value.dval) > 
			kl->slop.dval) {
	       if (do_print) {
		  if (mismatches == 0)
		     Fprintf(stdout, "%s vs. %s:\n",
				E1->object->instance, E2->object->instance);
		  Fprintf(stdout, " %s circuit1: %g   circuit2: %g   "
			"(delta=%2.1g%%, cutoff=%2.1g%%)\n",
			kl->key, vl1->value.dval, vl2->value.dval,
			100 * fabs(vl1->value.dval - vl2->value.dval) / vl1->value.dval,
			100 * kl->slop.dval);
	       }
	       mismatches++;
	    }
	    break;
	 case PROP_INTEGER:
	    if (abs(vl1->value.ival - vl2->value.ival) > kl->slop.ival) {
	       if (do_print) {
		  if (mismatches == 0)
		     Fprintf(stdout, "%s vs. %s:\n",
				E1->object->instance, E2->object->instance);
		  Fprintf(stdout, " %s circuit1: %d   circuit2: %d   "
			"(delta=%d, cutoff=%d)\n",
			kl->key, vl1->value.ival, vl2->value.ival,
			abs(vl1->value.ival - vl2->value.ival), kl->slop.ival);
	       }
	       mismatches++;
	    }
	    break;
	 case PROP_STRING:
	    if (kl->slop.ival == 0) {
	       if (strcasecmp(vl1->value.string, vl2->value.string)) {
		  if (do_print) {
		     if (mismatches == 0)
		        Fprintf(stdout, "%s vs. %s:\n",
				E1->object->instance, E2->object->instance);
		     Fprintf(stdout, " %s circuit1: %s   circuit2: %s   "
			"(exact match req'd)\n",
			kl->key, vl1->value.string, vl2->value.string);
		  }
	          mismatches++;
	       }
	    }
	    else {
	       if (strncasecmp(vl1->value.string, vl2->value.string, kl->slop.ival)) {
		  if (do_print) {
		     if (mismatches == 0)
		        Fprintf(stdout, "%s vs. %s:\n",
				E1->object->instance, E2->object->instance);
		     Fprintf(stdout,  " %s circuit1: %s   circuit2: %s   "
			"(check to %d chars.)\n",
			kl->key, vl1->value.string, vl2->value.string, kl->slop.ival);
		  }
	          mismatches++;
	       }
	    }
	    break;
      }
      vl1 = vl1->next;
      vl2 = vl2->next;
      if ((vl1 == NULL && vl2 != NULL) || (vl1 != NULL && vl2 == NULL))
	 return -1;	/* Different number of properties of interest */
   }
   return mismatches;
}

/*----------------------------------------------------------------------*/
/* Return 0 if perfect matching found, else return number of		*/
/* automorphisms, and return -1 if invalid matching found. 		*/
/*----------------------------------------------------------------------*/

int VerifyMatching(void)
{
  int ret;
  struct ElementClass *EC;
  struct NodeClass *NC;
  struct Element *E;
  struct Node *N;
  int C1, C2;

  if (BadMatchDetected) return(-1);
  
  ret = 0;
  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
    C1 = C2 = 0;
    for (E = EC->elements; E != NULL; E = E->next) 
      (E->graph == Circuit1->file) ? C1++ : C2++;
    if (C1 != C2) return(-1);
    if (C1 != 1)
       ret++;
    else if (PropertyErrorDetected == 0) {
       if (PropertyCheck(EC, 0) != 0)
          PropertyErrorDetected = 1;
    }
  }

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    C1 = C2 = 0;
    for (N = NC->nodes; N != NULL; N = N->next) {
      (N->graph == Circuit1->file) ? C1++ : C2++;
    }
    if (C1 != C2) return(-1);
    if (C1 != 1) ret++;
  }
  return(ret);
}

void PrintAutomorphisms(void)
{
  struct ElementClass *EC;
  struct NodeClass *NC;
  struct Element *E;
  struct Node *N;
  int C1, C2;

  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
    C1 = C2 = 0;
    for (E = EC->elements; E != NULL; E = E->next) 
      (E->graph == Circuit1->file) ? C1++ : C2++;
    if (C1 != C2) continue;
    if (C1 != 1) {
      Printf("Element Automorphism:\n");
      for (E = EC->elements; E != NULL; E = E->next)
	Printf("  Circuit %d: %s\n",E->graph, E->object->instance);
      Printf("------------------\n");
    }
  }

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    C1 = C2 = 0;
    for (N = NC->nodes; N != NULL; N = N->next) 
      (N->graph == Circuit1->file) ? C1++ : C2++;
    if (C1 != C2) continue;
    if (C1 != 1) {
      Printf("Node Automorphism:\n");
      for (N = NC->nodes; N != NULL; N = N->next)
	Printf("  Circuit %d: %s\n",N->graph, N->object->name);
      Printf("------------------\n");
    }
  }
}

/*
 *-------------------------------------------------------------------------
 *
 * PermuteAutomorphisms --
 *
 * For each node automorphism, check if all nodes in the group are ports
 * of the cell.  If so, make all of those pins permutable for each
 * instance in the two circuits.
 *
 *-------------------------------------------------------------------------
 */

void PermuteAutomorphisms(void)
{
   struct ElementClass *EC;
   struct NodeClass *NC;
   struct Element *E;
   struct Node *N, *N2;
   int C1, C2, numpins;
   char *model;

   for (NC = NodeClasses; NC != NULL; NC = NC->next) {
      struct Node *N1, *N2;
      C1 = C2 = 0;
      N1 = N2 = NULL;
      numpins = 0;
      for (N = NC->nodes; N != NULL; N = N->next) {
	 if (IsPort(N->object->type)) numpins++;
     
         if (N->graph == Circuit1->file) {
	    C1++;
	    N1 = N;
         }
         else {
	    C2++;
	    N2 = N;
         }
      }
      if (C1 == C2 && C1 != 1 && numpins == (C1 + C2)) {
	 N = NC->nodes;
	 model = (N->graph == Circuit1->file) ? Circuit1->name : Circuit2->name;
	
	 for (N2 = N; N2 != NULL; N2 = N2->next) {
	    if (N2->graph == N->graph)
	       PermuteSetup(model, N->object->name, N2->object->name);
	 }   
	 for (N2 = N; N2 != NULL; N2 = N2->next)
	    if (N2->graph != N->graph)
	       break;

	 model = (N2->graph == Circuit1->file) ? Circuit1->name : Circuit2->name;
	 for (N = N2; N != NULL; N = N->next) {
	    if (N->graph == N2->graph)
	       PermuteSetup(model, N2->object->name, N->object->name);
	 }   
      }
   }
}

/*
 *-------------------------------------------------------------------------
 *
 * ResolveAutomorphisms --
 *
 * Arbitrarily equivalence one pair of elements within an automorphic class
 * return value is the same as VerifyMatching()
 *
 *-------------------------------------------------------------------------
 */

int ResolveAutomorphisms(void)
{
  struct ElementClass *EC;
  struct NodeClass *NC;
  struct Element *E;
  struct Node *N;
  int C1, C2;

  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
    struct Element *E1, *E2;
    C1 = C2 = 0;
    E1 = E2 = NULL;
    for (E = EC->elements; E != NULL; E = E->next) {
      if (E->graph == Circuit1->file) {
	C1++;
	E1 = E;
      }
      else {
	C2++;
	E2 = E;
      }
    }
    if (C1 == C2 && C1 != 1) {
      unsigned long newhash;
      Magic(newhash);
      E1->hashval = newhash;
      E2->hashval = newhash;
      goto converge;
    }
  }

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    struct Node *N1, *N2;
    C1 = C2 = 0;
    N1 = N2 = NULL;
    for (N = NC->nodes; N != NULL; N = N->next) {
      if (N->graph == Circuit1->file) {
	C1++;
	N1 = N;
      }
      else {
	C2++;
	N2 = N;
      }
    }
    if (C1 == C2 && C1 != 1) {
      unsigned long newhash;
      Magic(newhash);
      N1->hashval = newhash;
      N2->hashval = newhash;
      goto converge;
    }
  }

 converge:
  FractureElementClass(&ElementClasses); 
  FractureNodeClass(&NodeClasses); 
  ExhaustiveSubdivision = 1;
  while (!Iterate() && VerifyMatching() != -1) ; 
  return(VerifyMatching());
}

/*------------------------------------------------------*/
/* PermuteSetup --					*/
/* Add an entry to the "PinPermutations" linked list.	*/
/* Return 1 if OK, 0 if error				*/
/*							*/
/* NOTE:  When comparing two netlists, it is only	*/
/* necessary to permute pins on devices of one of them.	*/
/* So if a device has the same name in two circuits,	*/
/* e.g., "pmos", and is defined as a subcircuit so that	*/
/* it is not automatically permuted according to the	*/
/* default rules, then it is valid to use LookupCell,	*/
/* which returns the first one encountered, mark those	*/
/* pins as permuted, and not touch the other "pmos".	*/
/*------------------------------------------------------*/

int PermuteSetup(char *model, char *pin1, char *pin2)
{
    struct Permutation *newperm, *perm;
    struct nlist *tp;
    struct objlist *obj1, *obj2;

    tp = LookupCell(model);
    if (tp == NULL) {
        Printf("No such model %s\n", model);
       	return 0;
    }
    obj1 = LookupObject(pin1, tp);
    if (obj1 == NULL) {
        Printf("No such pin %s in model %s\n", pin1, model);
       	return 0;
    }
    obj2 = LookupObject(pin2, tp);
    if (obj2 == NULL) {
        Printf("No such pin %s in model %s\n", pin2, model);
       	return 0;
    }

    /* Now check that this permutation is not already in the list. */

    for (perm = PinPermutations; perm != NULL; perm = perm->next)
       if ((*matchfunc)(perm->model, model) && (*matchfunc)(perm->pin1, pin1) &&
		(*matchfunc)(perm->pin1, pin2))
	  return 1;

    newperm = (struct Permutation *)CALLOC(1, sizeof(struct Permutation));
    newperm->model = tp->name;
    newperm->pin1 = obj1->name;
    newperm->pin2 = obj2->name;
    newperm->next = PinPermutations;
    PinPermutations = newperm;
    return 1;
}

/*------------------------------------------------------*/
/* PermuteForget --					*/
/* Remove an entry from the "PinPermutations" linked	*/
/* list.  This makes it more convenient to use the	*/
/* default permutations and declare individual		*/
/* exceptions.						*/
/* Return 1 if OK, 0 if error				*/
/*------------------------------------------------------*/

int PermuteForget(char *model, char *pin1, char *pin2)
{
    struct Permutation *lastperm, *perm;
    struct nlist *tp;
    struct objlist *obj1, *obj2;

    tp = LookupCell(model);
    if (tp == NULL) {
        Printf("No such model %s\n", model);
       	return 0;
    }
    obj1 = LookupObject(pin1, tp);
    if (obj1 == NULL) {
        Printf("No such pin %s in model %s\n", pin1, model);
       	return 0;
    }
    obj2 = LookupObject(pin2, tp);
    if (obj2 == NULL) {
        Printf("No such pin %s in model %s\n", pin2, model);
       	return 0;
    }

    /* Now remove this permutation from the list, if it's there. */

    lastperm = NULL;
    for (perm = PinPermutations; perm != NULL; perm = perm->next) {
       if ((*matchfunc)(perm->model, model) && (*matchfunc)(perm->pin1, pin1) &&
		(*matchfunc)(perm->pin1, pin2)) {
	   if (lastperm == NULL)
	      PinPermutations = perm->next;
	   else
	      lastperm->next = perm->next;
	   FREE(perm);
	   break;
       }	
       lastperm = perm;
    }
    return 1;
}

/*------------------------------------------------------*/
/* Permute --						*/
/* For each entry in the "PinPermutations" linked list,	*/
/* set the magic numbers of pin1 and pin2 to be the	*/
/* same.						*/
/* Return 1 if OK, 0 if error				*/
/*------------------------------------------------------*/

int Permute()
{
   struct Permutation *perm;
   struct ElementClass *EC;
   struct Element *E;
   struct NodeList *NL;
   struct objlist *ob;
   unsigned long one, two;

   for (perm = PinPermutations; perm != NULL; perm = perm->next) {
      for (EC = ElementClasses; EC != NULL; EC = EC->next) {
         for (E = EC->elements; E != NULL; E = E->next) {
            if ((*matchfunc)(E->object->model, perm->model)) {
	       one = two = 0;
	       ob = E->object;
	       for (NL = E->nodelist; NL != NULL && !one; NL = NL->next) {
		  if ((*matchfunc)(perm->pin1, ob->name + strlen(ob->instance) + 1)) 
		     one = NL->pin_magic;
		  ob = ob->next;
	       }
	       ob = E->object;
	       for (NL = E->nodelist; NL != NULL && !two; NL = NL->next) {
		  if ((*matchfunc)(perm->pin2, ob->name + strlen(ob->instance) + 1)) 
		     two = NL->pin_magic;
		  ob = ob->next;
	       }
	       if (one == 0) {
		  Printf("Class %s does not have pin %s.\n", perm->model, perm->pin1);
		  if (two == 0)
		     Printf("Class %s does not have pin %s.\n", perm->model, perm->pin2);
		  return (0);
	       }
	       if (two == 0) {
		  Printf("Class %s does not have pin %s.\n", perm->model, perm->pin2);
		  return (0);
 	       }
	       /* update magic numbers */
	       for (NL = E->nodelist; NL != NULL; NL = NL->next) 
		  if (NL->pin_magic == one) NL->pin_magic = two;
	    }
	 }
      }
   }
   return(1);
}
	
/*------------------------------------------------------*/
/* Force two elements two be declared matching between	*/
/* the two circuits being compared			*/
/* Return 1 on success, 0 on failure			*/
/*------------------------------------------------------*/

int EquivalenceElements(char *name1, char *name2)
{
  struct ElementClass *EC;
  struct Element *E, *E1, *E2;

  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
    E1 = E2 = NULL;
    for (E = EC->elements; E != NULL; E = E->next) {
      if (E->graph==Circuit1->file && E1==NULL &&
		(*matchfunc)(E->object->instance, name1)) E1=E;
      if (E->graph==Circuit2->file && E2==NULL &&
		(*matchfunc)(E->object->instance, name2)) E2=E;
    }
    if (E1 != NULL || E2 != NULL) {
      struct ElementClass *NewList, *EndOfNewList;
      if (E1 == NULL || E2 == NULL) 
	return(0);  /* did not find both in same equivalence class */
      /* otherwise, create a new equivalence class */
      for (E = EC->elements; E != NULL; E = E->next) {
	if (E == E1 || E == E2) E->hashval = 1;
	else E->hashval = 0;
      }
      /* now make new equivalence classes, and tear EC out of old list */
      NewList = MakeElist(EC->elements);
      for (EndOfNewList = NewList; EndOfNewList->next != NULL; 
	   EndOfNewList = EndOfNewList->next) ;
      EndOfNewList->next = EC->next;
      if (EC == ElementClasses) {
	FreeElementClass (ElementClasses);
	ElementClasses = NewList;
      }
      else {
	struct ElementClass *EC3;
	for (EC3 = ElementClasses; EC3->next != EC; EC3 = EC3->next) ;
	EC3->next = NewList;
	FreeElementClass(EC);
      }
      return(1);
    }
  }
  return(0);
}

/*------------------------------------------------------*/
/* Force two nodes to be declared matching between the	*/
/* two circuits being compared.				*/
/* Return 1 on success, 0 on failure			*/
/*------------------------------------------------------*/

int EquivalenceNodes(char *name1, char *name2)
{
  int node1, node2;
  struct objlist *ob;
  struct NodeClass *NC;
  struct Node *N, *N1, *N2;

  ob = LookupObject(name1, Circuit1);
  if (ob == NULL) return(0);
  node1 = ob->node;

  ob = LookupObject(name2, Circuit2);
  if (ob == NULL) return(0);
  node2 = ob->node;

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    N1 = N2 = NULL;
    for (N = NC->nodes; N != NULL; N = N->next) {
      if (N->graph== Circuit1->file && N1==NULL && N->object->node == node1) N1=N;
      if (N->graph== Circuit2->file && N2==NULL && N->object->node == node2) N2=N;
    }
    if (N1 != NULL || N2 != NULL) {
      struct NodeClass *NewList, *EndOfNewList;
      if (N1 == NULL || N2 == NULL) 
	return(0);  /* did not find both in same equivalence class */
      /* otherwise, create a new equivalence class */
      for (N = NC->nodes; N != NULL; N = N->next) {
	if (N == N1 || N == N2) N->hashval = 1;
	else N->hashval = 0;
      }
      /* now make new equivalence classes, and tear NC out of old list */
      NewList = MakeNlist(NC->nodes);
      for (EndOfNewList = NewList; EndOfNewList->next != NULL; 
	   EndOfNewList = EndOfNewList->next) ;
      EndOfNewList->next = NC->next;
      if (NC == NodeClasses) {
	FreeNodeClass (NodeClasses);
	NodeClasses = NewList;
      }
      else {
	struct NodeClass *NC3;
	for (NC3 = NodeClasses; NC3->next != NC; NC3 = NC3->next) ;
	NC3->next = NewList;
	FreeNodeClass(NC);
      }
      return(1);
    }
  }
  return(0);
}

/*------------------------------------------------------*/
/* Ignore any class named "name" in the input.  If	*/
/* netlists exist at the time this routine is called,	*/
/* then remove all elements of this class from the	*/
/* database.						*/
/*------------------------------------------------------*/

int IgnoreClass(char *name, int file)
{
   struct IgnoreList *newIgnore;

   newIgnore = (struct IgnoreList *)MALLOC(sizeof(struct IgnoreList));
   newIgnore->next = ClassIgnore;
   ClassIgnore = newIgnore;
   newIgnore->class = (char *)MALLOC(1 + strlen(name));
   strcpy(newIgnore->class, name);
   newIgnore->file = file;

   /* Remove existing classes from database */
   ClassDelete(name, file);

   return 0;
}

/*------------------------------------------------------*/
/* Declare that the device class "name1" in file1	*/
/* is the same as the device class "name2" in file2	*/
/*							*/
/* If file1 and file2 are -1, then these are names to	*/
/* be checked as netcmp works through the hierarchy.  	*/
/* Otherwise, look up the structure for each file and	*/
/* set the classhash of the second to that of the first	*/
/*							*/
/* Return 1 on success, 0 on failure			*/
/*------------------------------------------------------*/

int EquivalenceClasses(char *name1, int file1, char *name2, int file2)
{
   char *class1, *class2;
   struct Correspond *newc;
   struct nlist *tp, *tp2;

   if (file1 != -1 && file2 != -1) {

      tp = LookupClassEquivalent(name1, file1, file2);
      if ((*matchfunc)(tp->name, name2)) return 1;	/* Already equivalent */

      tp = LookupCellFile(name1, file1);
      tp2 = LookupCellFile(name2, file2);
      tp2->classhash = tp->classhash;
      return 1;
   }

   /* Create a new class correspondence entry.  Use the names in the	*/
   /* records found by Lookup() so that the don't need to allocate and	*/
   /* free the string records.						*/

   newc = (struct Correspond *)CALLOC(1, sizeof(struct Correspond));
   newc->class1 = strsave(name1);
   newc->file1 = file1;
   newc->class2 = strsave(name2);
   newc->file2 = file2;

   newc->next = ClassCorrespondence;
   ClassCorrespondence = newc;

   return 1;
}

#ifdef TCL_NETGEN

/*------------------------------------------------------*/
/*------------------------------------------------------*/

int reorderpins(struct hashlist *p, int file)
{
  struct nlist *ptr;
  struct objlist *ob, *ob2, *ob3, *obp, *obn;
  int j, numnodes, instnodes, *nodeorder = NULL, *nodeorphan, orphans;
  char **portnames, **portorphan, *newname, *cname;

  ptr = (struct nlist *)(p->ptr);

  if (ptr->file != file) {
     /* On Circuit1 cells, cull nodes attached to disconnected pins	*/
     /* (pins not connecting to any device inside the cell)		*/

     for (ob = ptr->cell; ob != NULL; ob = ob->next) {
        if (ob->type == FIRSTPIN) {
	   if ((*matchfunc)(ob->model, Circuit1->name)) {
	      obp = ob;
              for (ob2 = Circuit1->cell; ob2 != NULL; ob2 = ob2->next) {
	         if (ob2->type != PORT) break;
		 if (ob2->node < 0) {
		    obp->node = -1;
		 }
		 ob = obp;
	         obp = obp->next;
	         if (obp == NULL) break;	/* Error condition; shouldn't happen */
	      }
	   }
	}
     }

     return 1;
  }

  /* To-do:  When hashing, search over insttab hash record	*/

  for (ob = ptr->cell; ob != NULL; ob = ob->next) {
     if (ob->type == FIRSTPIN) {
	if ((*matchfunc)(ob->model, Circuit2->name)) {
	   Printf("Reordering pins on instance %s\n", ob->instance);

	   instnodes = 0;
	   obn = NULL;
           for (ob2 = ob; ob2 != NULL; ob2 = ob2->next) {
	      if ((ob2->type < FIRSTPIN) || (ob2->instance == NULL) ||
			!(*matchfunc)(ob2->instance, ob->instance)) {
		 obn = ob2;	/* Save so we don't have to use match next time */
		 break;
	      }
	      instnodes++;
	   }

	   /* We don't actually want to reorder pins, just reorder the node	*/
	   /* number records.							*/

	   if (nodeorder == NULL) {		/* 1st time through only */
	      numnodes = 0;
              for (ob2 = Circuit2->cell; ob2 != NULL; ob2 = ob2->next) {
	         if (ob2->type != PORT) break;
		 numnodes++;
	      }
	      nodeorder = (int *)CALLOC(numnodes, sizeof(int));
	      portnames = (char **)CALLOC(numnodes, sizeof(char *));

	      nodeorphan = (int *)CALLOC(numnodes, sizeof(int));
	      portorphan = (char **)CALLOC(numnodes, sizeof(char *));
	      orphans = 0;
	   }

	   if (instnodes < numnodes) {
	      /* Add dummy nodes to instance, at end of pin list */
	      for (ob2 = ob; ob2 != NULL; ob2 = ob2->next) {
		 if (ob2->next == obn) {
		    for (j = numnodes; j > instnodes; j--) {
		       obp = (struct objlist *)CALLOC(1, sizeof(struct objlist));
		       obp->name = (char *)MALLOC(10 + strlen(ob2->instance));
		       sprintf(obp->name, "%s/dummy%d", ob2->instance, j);
		       obp->type = j;
		       obp->model = strsave(ob2->model);
		       obp->instance = strsave(ob2->instance);
		       obp->node = 0;		/* Special node number */
		       obp->next = ob2->next;
		       ob2->next = obp;
		    }
		    break;
		 }
	      }
	   }

	   for (j = 0; j < numnodes; j++) *(nodeorder + j) = -2;

	   obp = ob;
           for (ob2 = Circuit2->cell; ob2 != NULL; ob2 = ob2->next) {
	      if (ob2->type != PORT) break;
	      j = (int)ob2->model;

	      if ((j < numnodes) && (j >= 0)) {
		 /* Cull nodes on disconnected pins---this should be dependent	*/
		 /* upon a global option, not done unconditionally!		*/
	         *(nodeorder + j) = (ob2->node < 0) ? 0 : obp->node;
	         *(portnames + j) = obp->name;
	      }
	      else if (j == -1) {
		 for (ob3 = ob2->next; IsPort(ob3->type); ob3 = ob3->next);
		 for (; ob3 != NULL; ob3 = ob3->next) {
		    if (ob3->node == ob2->node) {
		       *(nodeorphan + orphans) = (ob2->node < 0) ? 0 : obp->node;
		       *(portorphan + orphans) = obp->name;
		       orphans++;
		       break;
		    }
	 	 }
	      }
	      obp = obp->next;
	      if (obp == NULL) break;	/* Error condition; shouldn't happen */
	   }

	   // Add back non-matching nodes, if they had connections in the circuit
	   while (++j < numnodes && orphans > 0) {
	      orphans--;
	      *(nodeorder + j) = *(nodeorphan + orphans);
	      *(portnames + j) = *(portorphan + orphans);
	   }

	   for (j = 0; j < numnodes; j++) {
	      if (*(nodeorder + j) < -1) {
		 Fprintf(stdout, "Unconnected port %d on instance %s\n", j,
				ob->instance);
	         *(nodeorder + j) = -1;
	      }
	   }

	   obp = ob;
	   j = 0;
           for (ob2 = obp; ob2 != NULL; ob2 = ob2->next) {
	      ob2->node = *(nodeorder + j);
	      if (*(portnames + j) == NULL) {
		 ob2->name = (char *)MALLOC(10);
		 sprintf(ob2->name, "dummy%d", j);
	      }
	      else
	         ob2->name = *(portnames + j);
	      j++;
	      if (j == numnodes) {
	         ob = ob2;
		 break;
	      }
	   }
 	}
     }
  }
  if (nodeorder != NULL) {
     FREE(nodeorder);
     FREE(portnames);
     FREE(nodeorphan);
     FREE(portorphan);
  }
  return(1);
}

/*------------------------------------------------------*/
/* Declare that the device class "name1" is equivalent	*/
/* to class "name2".  This is the same as the above	*/
/* routine, except that the cells must be at the top	*/
/* of the compare queue, and must already be proven	*/
/* equivalent by LVS.  Determine a pin correspondence,	*/
/* then modify all instances of "name2" to match all	*/
/* instances of "name1" by pin reordering.  If either	*/
/* cell has disconnected pins, they are shuffled to the	*/
/* end of the pin list.  If two or more pins correspond	*/
/* to net automorphisms, then they are added to the	*/
/* list of permuted pins.				*/
/*							*/
/* Return 1 on success, 0 on failure			*/
/*------------------------------------------------------*/

int MatchPins()
{
   char *cover;
   struct nlist *tc1, *tc2;
   struct objlist *ob1, *ob2, *obn, *obp, *ob1s, *ob2s;
   struct NodeClass *NC;
   struct Node *N1, *N2;
   int i, j, k, m, swapped, numnodes;
   int result = 1, haspins = 0;
   char ostr[89];

   // FIXME --- tc1 = Circuit1, tc2 = Circuit2
   tc1 = LookupCellFile(Circuit1->name, Circuit1->file);
   tc2 = LookupCellFile(Circuit2->name, Circuit2->file);

   for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
      if (ob2->type != PORT) break;
      else haspins = 1;
      ob2->model = (char *)(-1);
   }
   numnodes = 0;
   for (ob1 = tc1->cell; ob1 != NULL; ob1 = ob1->next) {
      if (ob1->type != PORT) break;
      else haspins = 1;
      numnodes++;
   }

   if (haspins == 0) {
      // Neither cell has any ports, so this is probably a top-level
      // cell and there is nothing to do.
      return 1;
   }

   cover = (char *)CALLOC(numnodes, sizeof(char));

   if (Debug == 0) {
       /* Format side-by-side comparison of pins */
       Fprintf(stdout, "\nSubcircuit pins:\n");
       *(ostr + 43) = '|';
       *(ostr + 87) = '\n';
       *(ostr + 88) = '\0';
       for (i = 0; i < 43; i++) *(ostr + i) = ' ';
       for (i = 44; i < 87; i++) *(ostr + i) = ' ';
       snprintf(ostr, 43, "Circuit 1: %s", tc1->name);
       snprintf(ostr + 44, 43, "Circuit 2: %s", tc2->name);
       for (i = 0; i < 88; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
       Fprintf(stdout, ostr);
       for (i = 0; i < 43; i++) *(ostr + i) = '-';
       for (i = 44; i < 87; i++) *(ostr + i) = '-';
       Fprintf(stdout, ostr);
   }

   for (NC = NodeClasses; NC != NULL; NC = NC->next) {
      for (N1 = NC->nodes; N1 != NULL; N1 = N1->next) {
         if (N1->graph == Circuit1->file) {
	    obn = N1->object;
	    if (IsPort(obn->type)) {
	       i = 0;
	       for (ob1 = tc1->cell; ob1 != NULL; ob1 = ob1->next, i++) {
	          if ((IsPort(ob1->type)) && (*matchfunc)(ob1->name, obn->name)) {
                     for (N2 = NC->nodes; N2 != NULL; N2 = N2->next) {
	                if (N2->graph != Circuit1->file)
		           break;
	             }
	             if (N2 == NULL) return 0;
		     obp = N2->object;
		     j = 0;
	             for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next, j++) {
	                if ((IsPort(ob2->type)) && (*matchfunc)(ob2->name, obp->name)) {
			   if (Debug == 0) {
			      for (m = 0; m < 43; m++) *(ostr + m) = ' ';
			      for (m = 44; m < 87; m++) *(ostr + m) = ' ';
			      sprintf(ostr, "%s", obn->name);
			      sprintf(ostr + 44, "%s", obp->name);
			      for (m = 0; m < 88; m++)
				 if (*(ostr + m) == '\0') *(ostr + m) = ' ';
			      Fprintf(stdout, ostr);
			   }
			   else {
			      Fprintf(stdout, "Cell %s port %d \"%s\""
					" = cell %s port %d \"%s\"\n",
					tc1->name, i, obn->name,
					tc2->name, j, obp->name);
			   }
			   ob2->model = (char *)i;		/* save order */
			   *(cover + i) = (char)1;
			   break;
			}
		     }
		     if (ob2 == NULL) {
			if (Debug == 0) {
			   for (m = 0; m < 43; m++) *(ostr + m) = ' ';
			   for (m = 44; m < 87; m++) *(ostr + m) = ' ';
			   sprintf(ostr, "%s", obn->name);
			   sprintf(ostr + 44, "(no matching pin)");
			   for (m = 0; m < 88; m++)
				 if (*(ostr + m) == '\0') *(ostr + m) = ' ';
			   Fprintf(stdout, ostr);
			}
			else {
		           Fprintf(stderr, "No matching pin in cell %s for "
					"cell %s pin %s\n",
					tc2->name, tc1->name, obn->name);
			}
			result = 0;
		     }
		     break;
		  }
	       }
	       if (ob1 == NULL) {
		  if (Debug == 0) {
		     for (m = 0; m < 43; m++) *(ostr + m) = ' ';
		     for (m = 44; m < 87; m++) *(ostr + m) = ' ';
		     sprintf(ostr, "%s", obn->name);
		     sprintf(ostr + 44, "(no matching pin)");
		     for (m = 0; m < 88; m++)
			if (*(ostr + m) == '\0') *(ostr + m) = ' ';
		     Fprintf(stdout, ostr);
		  }
		  else {
		     Fprintf(stderr, "No netlist match for cell %s pin %s\n",
				tc1->name, obn->name);
		  }
		  result = 0;
	       }
	    }
	 }
      }
   }

   if (result != 0) {

      /* Shuffle disconnected pins to the end */

      for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
         if (ob2->type != PORT) break;
         if (ob2->model == (char *)(-1)) {

            if (ob2->node < 0) {
	       ob2->model = (char *)numnodes++;
            }
	    else {
	       if (Debug == 0) {
		  for (m = 0; m < 43; m++) *(ostr + m) = ' ';
		  for (m = 44; m < 87; m++) *(ostr + m) = ' ';
		  sprintf(ostr, "(no matching pin)");
		  sprintf(ostr + 44, "%s", ob2->name);
		  for (m = 0; m < 88; m++)
		     if (*(ostr + m) == '\0') *(ostr + m) = ' ';
		  Fprintf(stdout, ostr);
	       }
	       else {
	          Fprintf(stderr, "No netlist match for cell %s pin %s\n",
				tc2->name, ob2->name);
	       }
	       result = 0;
            }
         }
      }

      /* If cell 2 has fewer nodes than cell 1, then add dummy (unconnected)	*/
      /* pins to cell 2.  If these correspond to numbers missing in the	match	*/
      /* sequence, then fill in the missing numbers.  Otherwise, add the	*/
      /* extra nodes to the end.						*/

      j = i = 0;
      for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
	 j++;
	 if (ob2->next->type != PORT) break;
      }

      while (j < numnodes) {
	 while (*(cover + i) != (char)0) i++;
	 if (i >= numnodes) break;

	 /* If the equivalent node in tc1 is not disconnected	*/
	 /* (node != -1) then we have a match error, and should	*/
	 /* stop.						*/

	 ob1 = tc1->cell;
	 for (k = i; k > 0 && ob1 != NULL; k--)
	    ob1 = ob1->next;

	 if (ob1 == NULL || ob1->type != PORT || ob1->node >= 0) {
	    if (ob1 != NULL && IsPort(ob1->type)) {

	       if (Debug == 0) {
		  for (m = 0; m < 43; m++) *(ostr + m) = ' ';
		  for (m = 44; m < 87; m++) *(ostr + m) = ' ';
		  sprintf(ostr, "%s", ob1->name);
		  sprintf(ostr + 44, "(no matching pin)");
		  for (m = 0; m < 88; m++)
		     if (*(ostr + m) == '\0') *(ostr + m) = ' ';
		  Fprintf(stdout, ostr);
	       }
	       else {
	          Fprintf(stderr, "No netlist match for cell %s pin %s\n",
				tc1->name, ob1->name);
	       }
	    }
	    else {
	       if (Debug == 0) {
		  for (m = 0; m < 43; m++) *(ostr + m) = ' ';
		  for (m = 44; m < 87; m++) *(ostr + m) = ' ';
		  sprintf(ostr, "%d", i);
		  sprintf(ostr + 44, "(no matching pin)");
		  for (m = 0; m < 88; m++)
		     if (*(ostr + m) == '\0') *(ostr + m) = ' ';
		  Fprintf(stdout, ostr);
	       }
	       else {
	          Fprintf(stderr, "No netlist match for cell %s pin number %d\n",
				tc1->name, i);
	       }
	    }
	    result = 0;
	    j++;
	    i++;
	    continue;	// Make sure we print all errors
	 }

	 obn = (struct objlist *)CALLOC(1, sizeof(struct objlist));
	 obn->name = (char *)MALLOC(10);
	 sprintf(obn->name, "dummy%d", i);
	 obn->type = PORT;
	 obn->model = (char *)i;
	 obn->instance = NULL;
	 obn->node = -1;
	 obn->next = ob2->next;
	 ob2->next = obn;
	 ob2 = obn;

	 j++;
	 i++;
      }
      FREE(cover);
   }

   if (Debug == 0) {
      for (i = 0; i < 87; i++) *(ostr + i) = '-';
      Fprintf(stdout, ostr);
   }

   if (result != 0) {

      /* Reorder pins in Circuit2 instances to match Circuit1 */

      RecurseCellFileHashTable(reorderpins, Circuit2->file);

      /* Reorder pins in Circuit2 cell to match Circuit1 */

      do {
         swapped = 0;
         obn = NULL;
         for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
	    if (ob2->next != NULL && IsPort(ob2->type) && IsPort(ob2->next->type)) {
	       if (ob2->model > ob2->next->model) {
	          swapped++;
	          if (obn != NULL) {
		     obn->next = ob2->next;
	             ob2->next = ob2->next->next;
	             obn->next->next = ob2;
	          }
	          else {
		     tc2->cell = ob2->next;
	             ob2->next = ob2->next->next;
	             tc2->cell->next = ob2;
	          }
		  break;
	       }
	    }
	    obn = ob2;
         }
      } while (swapped > 0);
   }

   /* Whether or not pins matched, reset ob2's pin indexes to 0 */

   for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
      if (ob2->type != PORT) break;
      ob2->model = NULL;
   }

   /* If pins cannot be made equivalent, mark the cells as non-matching */
   /* so that the hierarchical LVS will flatten them.			*/

   if (result == 0) {
      tc1->flags &= ~CELL_MATCHED;
      tc2->flags &= ~CELL_MATCHED;
   }
   return result;
}

/*------------------------------------------------------*/
/* Find the equivalent node to object "ob" in the other	*/
/* circuit.  Return a pointer to the object structure	*/
/* of the equivalent node or NULL if there is no	*/
/* equivalent.						*/
/*							*/
/* It is the responsibility of the calling function to	*/
/* decide what to do about automorphisms.		*/
/*							*/
/* Return 1 if a match is found, 0 if a match is not	*/
/* found, and -1 if the node itself is not found.	*/
/*------------------------------------------------------*/

int EquivalentNode(char *name, struct nlist *circuit, struct objlist **retobj)
{
  short ckt1;
  struct NodeClass *NC;
  struct Node *N1, *N2;
  struct objlist *ob;
  int retval = -1;

  if (Circuit1 == NULL || Circuit2 == NULL) return retval;

  if (circuit != NULL) {
     ob = LookupObject(name, circuit);
     if (ob == NULL) return retval;
  }
  else {
     ob = LookupObject(name, Circuit1);
     if (ob == NULL) {
        ob = LookupObject(name, Circuit2);
        if (ob == NULL) return retval;
     }
  }

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    for (N1 = NC->nodes; N1 != NULL; N1 = N1->next) {
      if (N1->object == ob) {
	retval = 0;		/* Node exists. . . */
	ckt1 = N1->graph;
        for (N2 = NC->nodes; N2 != NULL; N2 = N2->next)
	  if (N2->graph != ckt1) {
	    *retobj = N2->object;
	    return 1;
	  }
      }
    }
  }
  return retval;
}

/*------------------------------------------------------*/
/* Find the equivalent element to object "ob" in the	*/
/* other circuit.  Return a pointer to the object 	*/
/* structure of the equivalent element or NULL if there	*/
/* is no equivalent.					*/
/*							*/
/* It is the responsibility of the calling function to	*/
/* decide what to do about automorphisms.		*/
/*							*/
/* Return 1 if a match was found, 0 if the element has	*/
/* no match, and -1 if the element was not found.	*/
/*------------------------------------------------------*/

int EquivalentElement(char *name, struct nlist *circuit, struct objlist **retobj)
{
  short ckt1;
  struct ElementClass *EC;
  struct Element *E1, *E2;
  struct objlist *ob;
  int retval = -1;

  if (Circuit1 == NULL || Circuit2 == NULL) return retval;

  if (circuit != NULL) {
     ob = LookupInstance(name, circuit);
     if (ob == NULL) return retval;
  }
  else {
     ob = LookupInstance(name, Circuit1);
     if (ob == NULL) {
        ob = LookupInstance(name, Circuit2);
        if (ob == NULL) return retval;
     }
  }

  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
     for (E1 = EC->elements; E1 != NULL; E1 = E1->next) {
        if (E1->object == ob) {
	   retval = 0;
	   ckt1 = E1->graph;
           for (E2 = EC->elements; E2 != NULL; E2 = E2->next)
	      if (E2->graph != ckt1) {
	         *retobj = E2->object;
	         return 1;
	      }
        }
     }
  }
  return retval;
}

/*------------------------------------------------------*/
/* Flatten the two cells at the top of the compare	*/
/* queue.						*/
/*------------------------------------------------------*/

void FlattenCurrent()
{
   if (Circuit1 != NULL && Circuit2 != NULL) {
      Printf("Flattening subcell %s\n", Circuit1->name);
      FlattenInstancesOf(Circuit1->name, Circuit1->file);

      Printf("Flattening subcell %s\n", Circuit2->name);
      FlattenInstancesOf(Circuit2->name, Circuit2->file);
   }
}

/*------------------------------------------------------*/
/* Handler is only used when netgen is run from a	*/
/* terminal, not the Tk console.			*/
/*------------------------------------------------------*/

void handler(int sig)
{
   /* Don't do anything else here! */
   InterruptPending = 1;
}

/*------------------------------------------------------*/
/* Set up the interrupt flag (both methods) and signal	*/
/* handler (terminal-based method only).		*/
/*------------------------------------------------------*/

void enable_interrupt()
{
   InterruptPending = 0;
   oldinthandler = signal(SIGINT, handler);
}

void disable_interrupt()
{
   if (InterruptPending)
      InterruptPending = 0;
   signal(SIGINT, oldinthandler);
}

#else

static jmp_buf jmpenv;

/* static void handler(void) */
static void handler(int sig)
{
  Fprintf(stderr,"\nInterrupt (%d)!!\n", sig);
  Fflush(stderr);
  longjmp(jmpenv,1);
}

/*----------------------------------------------------------------------*/
/* Note that this cover-all routine is not called from the Tcl/Tk	*/
/* version, which replaces it with the script "lvs".			*/
/* return 1 if the two are identical.  Try to resolve automorphisms.	*/
/*----------------------------------------------------------------------*/

int Compare(char *cell1, char *cell2)
{
  int automorphisms;

  CreateTwoLists(cell1, -1, cell2, -1);
  Permute();

  /* Permutations should go here. . . */

  while (!Iterate()) ; 

  ExhaustiveSubdivision = 1;
  while (!Iterate());

  automorphisms = VerifyMatching();
  if (automorphisms == -1) {
    MatchFail(cell1, cell2);
    Printf("Circuits do not match.\n");
    PrintIllegalClasses();
    return(0);
  }
  if (automorphisms == 0) Fprintf(stdout, "Circuits match correctly.\n");
  if (PropertyErrorDetected) {
     Fprintf(stdout, "There were property errors.\n");
     PrintPropertyResults();
  }
  if (automorphisms == 0) return(1);

  Fprintf(stdout, "Circuits match with %d automorphisms.\n", automorphisms);
  if (VerboseOutput) PrintAutomorphisms();

  /* arbitrarily resolve automorphisms */
  Fprintf(stdout, "\n");
  Fprintf(stdout, "Arbitrarily resolving automorphisms:\n");
  while ((automorphisms = ResolveAutomorphisms()) > 0) ;
  if (automorphisms == -1) {
    MatchFail(cell1, cell2);
    Fprintf(stdout, "Circuits do not match.\n");
    return(0);
  }
  Fprintf(stdout, "Circuits match correctly.\n");
  return(1);
}


void NETCOMP(void)
/* a simple command interpreter to manage embedding/routing */
{
  char name[100];
  char name2[100];
  char ch;
  
  setjmp(jmpenv);
  signal(SIGINT,handler);
  MagicSeed(time(NULL));
  
  do {
    promptstring("NETCMP command: ", name);
    ch = name[0];

    switch (ch) {
    case 'c':
      promptstring("Enter cell 1: ",name);
      promptstring("Enter cell 2: ",name2);
      CreateTwoLists(name, -1, name2, -1);
#ifdef DEBUG_ALLOC
      PrintCoreStats();
#endif
      break;
    case 'i':
      if (!Iterate()) Printf("Please iterate again.\n");
      else Printf("No fractures made: we're done.\n");
      break;
    case 's':
      SummarizeElementClasses(ElementClasses);
      SummarizeNodeClasses(NodeClasses);
      break;
    case 'P':
      PrintElementClasses(ElementClasses, -1, 0);
      PrintNodeClasses(NodeClasses, -1, 0);
      break;
    case 'r':
      while (!Iterate()) ; 
      /* fall through to 'v' below */
    case 'v':
      if (ElementClasses == NULL || NodeClasses == NULL)
	Printf("Must initialize data structures first.\n");
      else {
	int automorphisms;
	automorphisms = VerifyMatching();
	if (automorphisms == -1) {
	  PrintIllegalClasses();
	  Fprintf(stdout, "Graphs do not match.\n");
	}
	else {
	  if (automorphisms) 
	    Printf("Circuits match with %d automorphisms.\n", automorphisms);
	  else Printf("Circuits match correctly.\n");
	}
      }
      break;
    case 'R':
      if (ElementClasses == NULL || NodeClasses == NULL)
	Printf("Must initialize data structures first.\n");
      else {
	int automorphisms;
	while (!Iterate()) ; 
	automorphisms = VerifyMatching();
	if (automorphisms == -1) Fprintf(stdout, "Graphs do not match.\n");
	else {
	  Printf("Graphs match with %d automorphisms.\n", automorphisms);
	  while ((automorphisms = ResolveAutomorphisms()) > 0)
	    Printf("  automorphisms = %d.\n", automorphisms);
	  if (automorphisms == -1) Fprintf(stdout, "Graphs do not match.\n");
	  else Printf("Circuits match correctly.\n");
	}
      }
      break;
    case 'a':
      PrintAutomorphisms();
      break;
    case 'e':
      /* equivalence two elements */
      Printf("Equivalence two elements.\n");
      promptstring("Enter element in circuit 1: ",name);
      promptstring("Enter element in circuit 2: ",name2);
      if (EquivalenceElements(name, name2)) 
	Printf("Elements %s and %s are equivalent.\n", name, name2);
      else Printf("Unable to equivalence elements %s and %s.\n",name, name2);
      break;
    case 'n':
      /* equivalence two nodes */
      Printf("Equivalence two nodes.\n");
      promptstring("Enter node in circuit 1: ",name);
      promptstring("Enter node in circuit 2: ",name2);
      if (EquivalenceNodes(name, -1, name2, -1))
	Printf("Nodes %s and %s are equivalent.\n", name, name2);
      else Printf("Unable to equivalence nodes %s and %s.\n",name, name2);
      break;
    case 'p':
      {
	char model[100];
	/* equivalence two pins on a given class of element */
	Printf("Allow permutation of two pins.\n");
	promptstring("Enter cellname: ",model);
	promptstring("Enter pin 1: ",name);
	promptstring("Enter pin 2: ",name2);
	if (PermuteSetup(model, name, name2)) Printf("%s == %s\n",name, name2);
	else Printf("Unable to permute pins %s, %s.\n",name, name2);
	break;
      }
    case 't':
      if (PermuteSetup("n", "drain", "source")) 
	Printf("n-channel: source == drain.\n");
      if (PermuteSetup("p", "drain", "source")) 
	Printf("p-channel: source == drain.\n");
      if (PermuteSetup("e", "bottom_a", "bottom_b")) 
	Printf("poly cap: permuting poly1 regions.\n");
      if (PermuteSetup("r", "end_a", "end_b")) 
	Printf("resistor: permuting endpoints.\n");
      if (PermuteSetup("c", "top", "bottom")) 
	Printf("capacitor: permuting sides.\n");
      break;
    case 'x':
      ExhaustiveSubdivision = !ExhaustiveSubdivision;
      Printf("Exhaustive subdivision %s.\n", 
	     ExhaustiveSubdivision ? "ENABLED" : "DISABLED");
      break;
    case 'o':
      RegroupDataStructures();
      break;
    case 'q': break;
    case 'Q' : exit(0);
    default:
      Printf("(c)reate internal data structure\n");
      Printf("do an (i)teration\n");
      Printf("(r)un to completion (convergence)\n");
      Printf("(R)un to completion (resolve automorphisms)\n");
      Printf("(v)erify results\n");
      Printf("print (a)utomorphisms\n");
      Printf("equate two (e)lements\n");
      Printf("equate two (n)odes\n");
      Printf("(p)ermute pins on elements\n");
      Printf("enable (t)ransistor permutations\n");
      Printf("toggle e(x)haustive subdivision\n");
      Printf("(P)rint internal data structure\n");
      Printf("(s)ummarize internal data structure\n");
      Printf("start (o)ver (reset data structures)\n");
      Printf("(q)uit NETCMP, (Q)uit NETGEN\n");
      break;
    }
  } while (ch != 'q');
  signal(SIGINT,SIG_DFL);
}

/* endif TCL_NETGEN */
#endif
	

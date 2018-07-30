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

/* netgen.c  -- most of the netlist manipulation routines and
                embedded-language specification routines.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>	/* for strtof() */
#include <stdarg.h>
#include <ctype.h>	/* toupper() */
#ifdef IBMPC
#include <alloc.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "netfile.h"
#include "print.h"

int Debug = 0;
int VerboseOutput = 1;  /* by default, we get verbose output */
int IgnoreRC = 0;

int NextNode;

int Composition = NONE;
int QuickSearch = 0;

int AddToExistingDefinition = 0;  /* default: overwrite cell when reopened */

extern int errno;	/* Defined in stdlib.h */

#define MAX_STATIC_STRINGS 5
static char staticstrings[MAX_STATIC_STRINGS][200];
static int laststring;

char *Str(char *format, ...)
{
  va_list ap;

  laststring++;
  laststring = laststring % MAX_STATIC_STRINGS;

  va_start(ap, format);
  vsprintf(staticstrings[laststring], format, ap);
  va_end(ap);
  return(staticstrings[laststring]);
}

/*----------------------------------------------------------------------*/
/* Add a new property key to the current cell (linked list)		*/
/* (To-do: check that this property is not already in the list)		*/
/*----------------------------------------------------------------------*/

void PropertyDouble(char *key, double slop)
{
   struct keylist *kl;

   if (CurrentCell == NULL) 
      Printf("No current cell for PropertyDouble()\n");
   else {
      kl = NewProperty();
      kl->key = strsave(key);
      kl->type = PROP_DOUBLE;
      kl->next = CurrentCell->proplist;
      kl->slop.dval = slop;
      CurrentCell->proplist = kl;
   }
}

/*----------------------------------------------------------------------*/

void PropertyInteger(char *key, int slop)
{
   struct keylist *kl;

   if (CurrentCell == NULL) 
      Printf("No current cell for PropertyInteger()\n");
   else {
      kl = NewProperty();
      kl->key = strsave(key);
      kl->type = PROP_INTEGER;
      kl->next = CurrentCell->proplist;
      kl->slop.ival = slop;
      CurrentCell->proplist = kl;
   }
}

/*----------------------------------------------------------------------*/

void PropertyString(char *key, int range)
{
   struct keylist *kl;

   if (CurrentCell == NULL) 
      Printf("No current cell for PropertyString()\n");
   else {
      kl = NewProperty();
      kl->key = strsave(key);
      kl->type = PROP_STRING;
      kl->next = CurrentCell->proplist;
      kl->slop.ival = (range >= 0) ? range : 0;
      CurrentCell->proplist = kl;
   }
}

/*----------------------------------------------------------------------*/
/* Declare the element class of the current cell			*/
/*----------------------------------------------------------------------*/

void SetClass(unsigned char class)
{
	if (CurrentCell == NULL) 
	  Printf("No current cell for SetClass()\n");
	else
	  CurrentCell->class = class;
}

/*----------------------------------------------------------------------*/

void ReopenCellDef(char *name, int fnum)
{ 	
  struct objlist *ob;

  if (Debug) Printf("Reopening cell definition: %s\n",name);
  GarbageCollect();
  if ((CurrentCell = LookupCellFile(name, fnum)) == NULL) {
    Printf("Undefined cell: %s\n", name);
    return;
  }
  /* cell exists, so append to the end of it */
  NextNode = 1;
  CurrentTail = CurrentCell->cell;
  for (ob = CurrentTail; ob != NULL; ob = ob->next) {
    CurrentTail = ob;
    if (ob->node >= NextNode) NextNode = ob->node + 1;
  }
}

/*----------------------------------------------------------------------*/

void CellDef(char *name, int fnum)
{
    struct nlist *np;

	if (Debug) Printf("Defining cell: %s\n",name);
	GarbageCollect();
	if ((CurrentCell = LookupCellFile(name, fnum)) != NULL) {
	  if (AddToExistingDefinition) {
	    ReopenCellDef(name, fnum);
	    return ;
	  }
	  else {
	    Printf("Cell: %s exists already, and will be overwritten.\n", name);
	    CellDelete(name, fnum);
	  }
	}
	/* install a new cell in lookup table (hashed) */
	np = InstallInCellHashTable(name, fnum);
	CurrentCell = LookupCellFile(name, fnum);
	CurrentCell->class = CLASS_SUBCKT;	/* default */
	CurrentCell->proplist = NULL;		/* default */
	CurrentCell->flags = 0;

	LastPlaced = NULL;
	CurrentTail = NULL;
	FreeNodeNames(CurrentCell);
	NextNode = 1;
}

/*----------------------------------------------------------------------*/
/* Same as CellDef() above, but mark cell as case-insensitive.		*/
/* This routine is used only by	the ReadSpice() function.		*/
/*----------------------------------------------------------------------*/

void CellDefNoCase(char *name, int file)
{
   CellDef(name, file);
   CurrentCell->file = file;
   CurrentCell->flags |= CELL_NOCASE;
}

/*----------------------------------------------------------------------*/

int IsIgnored(char *name, int file)
{
    struct IgnoreList *ilist;
    char *nptr = name;

    for (ilist = ClassIgnore; ilist; ilist = ilist->next)
    {
	if ((file == -1) || (ilist->file == -1) || (file == ilist->file)) 
	    if ((*matchfunc)(ilist->class, nptr))
		return 1;
    }
   return 0;
}

/*----------------------------------------------------------------------*/

void Port(char *name)
{
	struct objlist *tp;
	
	if (Debug) Printf("   Defining port: %s\n",name);
	if ((tp = GetObject()) == NULL) {
	  perror("Failed GetObject in Port");
	  return;
	}
	tp->name = strsave(name);
	tp->type = PORT;  /* port type */
	tp->model = NULL;
	tp->instance = NULL;
	tp->node = -1;  /* null node */
	tp->next = NULL;
	AddToCurrentCell (tp);
}

/*----------------------------------------------------------------------*/

void Node(char *name)
{
	struct objlist *tp;
	
	if (Debug) Printf("   Defining internal node: %s\n",name);
	if ((tp = GetObject()) == NULL) {
	  perror("Failed GetObject in Node");
	  return;
	}
	tp->name = strsave(name);
	tp->type = NODE;  /* internal node type */
	tp->model = NULL;
	tp->instance = NULL;
	tp->node = -1;  /* null node */
	tp->next = NULL;
	AddToCurrentCell (tp);
}

/*----------------------------------------------------------------------*/

void Global(char *name)
{
    struct objlist *tp;

    // Check if "name" is already in the current cell as a global node
    // or a port.  If it is, then we're done.  Otherwise, add "name" as
    // a new global in CurrentCell.

    for (tp = CurrentCell->cell; tp; tp = tp->next)
	if (tp->type == GLOBAL || tp->type == UNIQUEGLOBAL || tp->type == PORT)
	    if ((*matchfunc)(tp->name, name))
		return;
	
    if (Debug) Printf("   Defining global node: %s\n",name);
    if ((tp = GetObject()) == NULL) {
	perror("Failed GetObject in Global");
	return;
    }
    tp->name = strsave(name);
    tp->type = GLOBAL;		/* internal node type */
    tp->model = NULL;
    tp->instance = NULL;
    tp->node = -1;		/* null node */
    tp->next = NULL;
    AddToCurrentCell (tp);
}

/*----------------------------------------------------------------------*/

void UniqueGlobal(char *name)
{
	struct objlist *tp;
	
	if (Debug) Printf("   Defining unique global node: %s\n",name);
	if ((tp = GetObject()) == NULL) {
	  perror("Failed GetObject in UniqueGlobal");
	  return;
	}

	tp->name = strsave(name);
	tp->type = UNIQUEGLOBAL;  /* internal node type */
	tp->model = NULL;
	tp->instance = NULL;
	tp->node = -1;  /* null node */
	tp->next = NULL;
	AddToCurrentCell (tp);
}

/*----------------------------------------------------------------------*/

void Instance(char *model, char *instancename)
{
  struct objlist *tp, *tp2;
  struct nlist *instanced_cell;
  int portnum;
  char tmpname[1000], tmpname2[1000];
  int firstobj, fnum;
	
  if (Debug) Printf("   Instance: %s of class: %s\n",
		    instancename, model);
  if (CurrentCell == NULL) {
    Printf("No current cell for Instance(%s,%s)\n", model,instancename);
    return;
  }
  fnum = CurrentCell->file;
  if (IsIgnored(model, fnum)) {
    Printf("Class '%s' instanced in input but is being ignored.\n", model);
    return;
  }
  instanced_cell = LookupCellFile(model, fnum);
  if (instanced_cell == NULL) {
    Printf("Attempt to instance undefined model '%s'\n", model);
    return;
  }
  /* class exists */
  instanced_cell->number++;		/* one more allocated */
  portnum = 1;
  firstobj = 1;
  for (tp2 = instanced_cell->cell; tp2 != NULL; tp2 = tp2->next) 
    if (IsPort(tp2->type)) {
      /* it is a port */
      tp = GetObject();
      if (tp == NULL) {
	perror("Failed GetObject in Instance()");
	return;
      }
      strcpy(tmpname,instancename);
      strcat(tmpname,SEPARATOR);
      strcat(tmpname,tp2->name);
      tp->name = strsave(tmpname);
      tp->model = strsave(model);
      tp->instance = strsave(instancename);
      tp->type = portnum++;	/* instance type */
      tp->node = -1;		/* null node */
      tp->next = NULL;
      AddToCurrentCell (tp);
      if (firstobj) {
	AddInstanceToCurrentCell(tp);
	firstobj = 0;
      }
    }
  /* now run through list of new objects, processing global ports */
  for (tp2 = instanced_cell->cell; tp2 != NULL; tp2 = tp2->next) { 
    /* check to see if it is a global port */
    if (tp2->type == GLOBAL) {
      if (Debug) Printf("   processing global port: %s\n",
			tp2->name);
      strcpy(tmpname,instancename);
      strcat(tmpname,SEPARATOR);
      strcat(tmpname,tp2->name);
      /* see if element already exists */
      if (LookupObject(tp2->name,CurrentCell) != NULL)
	join(tp2->name, tmpname);
      else {
	/* define global node if not already there */
	Global(tp2->name);
	join(tp2->name, tmpname);
      }
    }
    else if (tp2->type == UNIQUEGLOBAL) {
      if (Debug) Printf("   processing unique global port: %s\n",
			tp2->name);
      strcpy(tmpname,CurrentCell->name);
      strcat(tmpname,INSTANCE_DELIMITER);
      strcat(tmpname,instancename);
      strcat(tmpname,SEPARATOR);
      strcat(tmpname,tp2->name);
      /* make this element UniqueGlobal */
      UniqueGlobal(tmpname);
      strcpy(tmpname2,instancename);
      strcat(tmpname2,SEPARATOR);
      strcat(tmpname2,tp2->name);
      Connect(tmpname,tmpname2);
    }
  }
  /* now run through list of new objects, checking for shorted ports */
  for (tp2 = instanced_cell->cell; tp2 != NULL; tp2 = tp2->next) {
    /* check to see if it is a unique port */
    /* remember to NOT consider unconnected ports (node = -1) 
       as being shorted out */

    if (IsPort(tp2->type)) {
      struct objlist *ob;

      ob = LookupObject(tp2->name, instanced_cell);
      if (ob->node != -1 && !(*matchfunc)(tp2->name, 
			NodeAlias(instanced_cell, ob))) {
	if (Debug) Printf("shorted ports found on Instance\n");
	strcpy(tmpname,instancename);
	strcat(tmpname,SEPARATOR);
	strcat(tmpname,tp2->name);
	strcpy(tmpname2,instancename);
	strcat(tmpname2,SEPARATOR);
	strcat(tmpname2, NodeAlias(instanced_cell, ob));
	join(tmpname,tmpname2);
      }
    }
  }
}

/*----------------------------------------------------------------------*/

char *Next(char *name)
{
    int filenum = CurrentCell->file;

	/* generate a unique instance name with 'name') */
	char buffer[1024];
	int n;
	
	n = 0;
	if (QuickSearch) {
	  struct nlist *tp;
	  tp = LookupCellFile(name, filenum);
	  if (tp != NULL)
	    n = tp->number; /* was +1, but would miss #2 */
	}
	do {
	  n++;
	  sprintf(buffer, "%s%d", name, n);
	} while (LookupInstance(buffer,CurrentCell) != NULL);
	return (strsave(buffer));
}

/*
 *---------------------------------------------------------------------
 * This procedure provides a versatile interface to Instance/Connect.
 * Cell() accepts a variable length list of arguments, in either of
 * two forms:  (i) named arguments -- take the form "port=something"
 * (ii) unnamed arguments, which are bound to ports in the order they
 * appear. Arguments are read until all cell ports have been connected,
 * or until a NULL is encountered.
 *
 * Returns the name of the instance, which remains valid at least
 * until the next call to Cell().
 *---------------------------------------------------------------------
 */

char *Cell(char *inststr, char *model, ...)
{
  va_list ap;
  char *nodelist;
  char tmpname[100];
  struct nlist *instanced_cell;
  struct objlist *head, *tp, *tp2;
  struct objlist *namedporthead, *namedportp, *namedlisthead, *namedlistp;
  int portnum, portlist, done;
  char namedport[200]; /* tmp buffers */
  int filenum;

  static char *instancename = NULL;
  char *instnameptr;

  if (CurrentCell == NULL) {
     Printf("No current cell defined for call to Cell().\n");
     return NULL;
  }
  else
     filenum = CurrentCell->file;
	
  if (Debug) Printf("   calling cell: %s\n",model);
  if (IsIgnored(model, filenum)) {
    Printf("Class '%s' instanced in input but is being ignored.\n", model);
    return NULL;
  }
  instanced_cell = LookupCellFile(model, filenum);
  if (instanced_cell == NULL) {
    Printf("Attempt to instance undefined class '%s'\n", model);
    return NULL;
  }
  /* class exists */
  tp2 = instanced_cell->cell;
  portnum = 0;
  while (tp2 != NULL) {
    if (IsPort(tp2->type)) portnum++;
    tp2 = tp2->next;
  }
	
  /* now generate lists of nodes using variable length parameter list */
  va_start(ap, model);
  head = NULL;
  namedporthead = namedlisthead = NULL;
  done = 0;
  portlist = 0;
  while (!done && portlist < portnum) {
    struct objlist *tmp;
    char *equals;

    nodelist = va_arg(ap, char *);
    if (nodelist == NULL) break; /* out of while loop */

    if (strchr(nodelist,'=') != NULL) {
      /* we have a named element */
      struct objlist *tmpport, *tmpname;
      struct nlist *oldCurCell;
      int ports;

      strcpy(namedport, nodelist);
      equals = strchr(namedport, '=');
      *equals = '\0';
      equals++;  /* point to first char of node */

      /* need to get list out of cell: 'model' */
      oldCurCell = CurrentCell;
      CurrentCell = instanced_cell;
      tmpport = List(namedport);
      CurrentCell = oldCurCell;
      tmpname = List(equals);

      if ((ports = ListLen(tmpport)) != ListLen(tmpname)) {
	Printf("List %s has %d elements, list %s has %d\n",
	       namedport, ListLen(tmpport), equals, ListLen(tmpname));
	done = 1;
      }
      else if (tmpport == NULL) {
	Printf("List %s has no elements\n", namedport);
	done = 1;
      }
      else if (tmpname == NULL) {
	Printf("List %s has no elements\n", equals);
	done = 1;
      }
      else {
	portlist += ports;
	namedporthead = ListCat(namedporthead, tmpport);
	namedlisthead = ListCat(namedlisthead, tmpname);
      }
    }
    else {
      /* unnamed element, so add it to the list */
      tmp = List(nodelist);
      if (tmp == NULL) {
	Printf("No such pin '%s' in Cell(%s); Current cell = %s\n",
             nodelist, model, CurrentCell->name);
	done = 1;
      }
      else {
	portlist += ListLen(tmp);
	head = ListCat(head, tmp);
      }
    }
  }
  va_end(ap);

  if (inststr == NULL) {
    if (instancename != NULL)
       FreeString(instancename);
    QuickSearch = 1;
    instancename = Next(model);
    QuickSearch = 0;
    instnameptr = instancename;
  }
  else
     instnameptr = inststr;

  Instance(model, instnameptr);
  tp = head;
  for (tp2 = instanced_cell->cell; tp2 != NULL; tp2 = tp2->next) {
    if (IsPort(tp2->type)) {
      strcpy(tmpname, instnameptr);
      strcat(tmpname, SEPARATOR);
      strcat(tmpname, tp2->name);
      namedlistp = namedlisthead;
      namedportp = namedporthead;
      while (namedportp != NULL) {
	if ((*matchfunc)(namedportp->name, tp2->name)) {
	  join(namedlistp->name, tmpname);
	  break; /* out of while loop */
	}
	namedlistp = namedlistp->next;
	namedportp = namedportp->next;
      }
      if (namedportp == NULL) {
	/* port was NOT a named port, so connect to unnamed list */
	if (tp == NULL) {
	  Printf( "Not enough ports in Cell().\n");
	  break; /* out of for loop */
	}
	else {
	  join(tp->name, tmpname);
	  tp = tp->next;
	}
      }
    }
  }
  return instnameptr;
}

/*----------------------------------------------------------------------*/
/* These default classes correspond to .sim file format types and other	*/
/* basic classes, and may be used by any netlist-reading routine to	*/
/* define basic types.	The classes are only defined when called (in	*/
/* contrast to netgen v. 1.3 and earlier, where they were pre-defined)	*/
/*----------------------------------------------------------------------*/

char *P(char *fname, char *inststr, char *gate, char *drain, char *source)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("p", fnum) == NULL) {
       CellDef("p", fnum);
       Port("drain");
       Port("gate");
       Port("source");
       PropertyDouble("length", 0.01);
       PropertyDouble("width", 0.01);
       SetClass(CLASS_PMOS);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "p", drain, gate, source);
}

/*----------------------------------------------------------------------*/

char *P4(char *fname, char *inststr, char *drain, char *gate, char *source, char *bulk)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("p4", fnum) == NULL) {
       CellDef("p4", fnum);
       Port("drain");
       Port("gate");
       Port("source");
       Port("well");
       PropertyDouble("length", 0.01);
       PropertyDouble("width", 0.01);
       SetClass(CLASS_PMOS4);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "p4", drain, gate, source, bulk);
}

/*----------------------------------------------------------------------*/

char *N(char *fname, char *inststr, char *gate, char *drain, char *source)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("n", fnum) == NULL) {
       CellDef("n", fnum);
       Port("drain");
       Port("gate");
       Port("source");
       PropertyDouble("length", 0.01);
       PropertyDouble("width", 0.01);
       SetClass(CLASS_NMOS);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "n", drain, gate, source);
}

/*----------------------------------------------------------------------*/

char *N4(char *fname, char *inststr, char *drain, char *gate, char *source, char *bulk)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("n4", fnum) == NULL) {
       CellDef("n4", fnum);
       Port("drain");
       Port("gate");
       Port("source");
       Port("bulk");
       PropertyDouble("length", 0.01);
       PropertyDouble("width", 0.01);
       SetClass(CLASS_NMOS4);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "n4", drain, gate, source, bulk);
}

/*----------------------------------------------------------------------*/

char *E(char *fname, char *inststr, char *top, char *bottom_a, char *bottom_b)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("e", fnum) == NULL) {
       CellDef("e", fnum);
       Port("top");
       Port("bottom_a");
       Port("bottom_b");
       PropertyDouble("length", 0.01);
       PropertyDouble("width", 0.01);
       SetClass(CLASS_ECAP);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "e", top, bottom_a, bottom_b);
}

/*----------------------------------------------------------------------*/

char *B(char *fname, char *inststr, char *collector, char *base, char *emitter)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("b", fnum) == NULL) {
       CellDef("b", fnum);
       Port("collector");
       Port("base");
       Port("emitter");
       SetClass(CLASS_NPN);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "b", collector, base, emitter);
}

/*----------------------------------------------------------------------*/

char *Res(char *fname, char *inststr, char *end_a, char *end_b)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("r", fnum) == NULL) {
       CellDef("r", fnum);
       Port("end_a");
       Port("end_b");
       PropertyDouble("value", 0.01);
       SetClass(CLASS_RES);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "r", end_a, end_b);
}

/*----------------------------------------------------------------------*/

char *Res3(char *fname, char *inststr, char *rdummy, char *end_a, char *end_b)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("r3", fnum) == NULL) {
       CellDef("r3", fnum);
       Port("dummy");
       Port("end_a");
       Port("end_b");
       PropertyDouble("value", 0.01);
       SetClass(CLASS_RES3);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "r3", rdummy, end_a, end_b);
}

/*----------------------------------------------------------------------*/

char *XLine(char *fname, char *inststr, char *node1, char *node2,
		char *node3, char *node4)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("t", fnum) == NULL) {
       CellDef("t", fnum);
       Port("node1");
       Port("node2");
       Port("node3");
       Port("node4");
       SetClass(CLASS_XLINE);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "t", node1, node2, node3, node4);
}

/*----------------------------------------------------------------------*/

char *Cap(char *fname, char *inststr, char *top, char *bottom)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("c", fnum) == NULL) {
       CellDef("c", fnum);
       Port("top");
       Port("bottom");
       PropertyDouble("value", 0.01);
       SetClass(CLASS_CAP);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "c", top, bottom);
}

/*----------------------------------------------------------------------*/

char *Cap3(char *fname, char *inststr, char *top, char *bottom, char *cdummy)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("c3", fnum) == NULL) {
       CellDef("c3", fnum);
       Port("top");
       Port("bottom");
       Port("dummy");
       PropertyDouble("value", 0.01);
       SetClass(CLASS_CAP3);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "c3", top, bottom, cdummy);
}

/*----------------------------------------------------------------------*/
/* Determine if two property keys are matching strings			*/
/* Return 1 on match, 0 on failure to match				*/
/*----------------------------------------------------------------------*/

int PropertyKeyMatch(char *key1, char *key2)
{
   /* For now, an unsophisticated direct string match */

   if (!strcasecmp(key1, key2)) return 1;
   return 0;
}

/*----------------------------------------------------------------------*/
/* Determine if two property values are matching.			*/
/* Return 1 on match, 0 on failure to match				*/
/*----------------------------------------------------------------------*/

int PropertyValueMatch(char *value1, char *value2)
{
   /* For now, an unsophisticated direct string match */

   if (!strcasecmp(value1, value2)) return 1;
   return 0;
}

/*----------------------------------------------------------------------*/
/* Add a key:value property pair to the list of property pairs		*/
/*----------------------------------------------------------------------*/

void AddProperty(struct keyvalue **topptr, char *key, char *value)
{
    struct keyvalue *kv;

    if (Debug) Printf("   Defining key:value property pair: %s:%s\n", key, value);
    if ((kv = NewKeyValue()) == NULL) {
	perror("Failed NewKeyValue in Property");
	return;
    }
    kv->key = strsave(key);
    kv->value = strsave(value);
    kv->next = *topptr;
    *topptr = kv;
}

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void AddScaledProperty(struct keyvalue **topptr, char *key, char *value, double scale)
{
    struct keyvalue *kv;

    if (Debug) Printf("   Defining key:value property pair: %s:%s\n", key, value);
    if ((kv = NewKeyValue()) == NULL) {
	perror("Failed NewKeyValue in Property");
	return;
    }
    kv->key = strsave(key);
    kv->value = strsave(value);
    kv->next = *topptr;
    *topptr = kv;
}

/*----------------------------------------------------------------------*/
/* Free up a property list 						*/
/*----------------------------------------------------------------------*/

void DeleteProperties(struct keyvalue **topptr)
{
    struct keyvalue *kv, *nextkv;
    
    kv = *topptr;
    while (kv != NULL)
    {
	nextkv = kv->next;
	FreeString(kv->key);
	FreeString(kv->value);
	FREE(kv);
	kv = nextkv;
    }
    *topptr = NULL;
}

/*----------------------------------------------------------------------*/
/* Add a list of properties to the current cell				*/
/*									*/
/* This routine does the greatest part of the work for the property-	*/
/* handling mechanism.  It determines which properties are of interest,	*/
/* floats them to the head of the list, and converts their values to	*/
/* the required types.  Any remaining properties are appended to the	*/
/* end of the list and may be used for netlist manipulation (e.g., 	*/
/* flattening and saving), but will not be used for netlist compares.	*/
/*----------------------------------------------------------------------*/

void LinkProperties(char *model, struct keyvalue *topptr)
{
    char key[100];
    struct keyvalue *kv, *newkv;
    struct keylist *kl;
    struct valuelist *vl, *lastvl;
    struct objlist *tp;
    struct nlist *cell;
    int isnum, filenum;

    if (topptr == NULL) return;

    if (CurrentCell == NULL) {
	Printf("LinkProperties() called with no current cell.\n");
	return;
    }
    else
	filenum = CurrentCell->file;

    if (IsIgnored(model, filenum)) {
	Printf("Class '%s' instanced in input but is being ignored.\n", model);
	return;
    }
    cell = LookupCellFile(model, filenum);
    if (cell == NULL) {
	Printf("No cell '%s' found to link properties to.\n", model);
	return;
    }

    tp = GetObject();
    tp->type = PROPERTY;
    tp->name = strsave("properties");
    tp->node = -2;		/* Don't report as disconnected node */
    tp->next = NULL;

    /* For convenience, use "instance" and "model" records. Use */
    /* "instance" to save values whose keys match those defined	*/
    /* in the cell, and use "model" to save everything else.	*/

    /* Save a copy of the key:value pairs in tp->model */

    tp->model = NULL;
    for (kv = topptr; kv != NULL; kv = kv->next)
    {
	newkv = NewKeyValue();
	newkv->key = strsave(kv->key);
	newkv->value = strsave(kv->value);
	newkv->next = (struct keyvalue *)tp->model;
	tp->model = (char *)newkv;
    }

    /* Find matching property keys in the cell, and arrange	*/
    /* them in order under the "instance" record.		*/

    tp->instance = NULL;
    lastvl = NULL;

    for (kl = cell->proplist; kl != NULL; kl = kl->next) {
        for (kv = (struct keyvalue *)tp->model; kv != NULL; kv = kv->next) {
	    if ((*matchfunc)(kl->key, kv->key)) {
	       vl = NewPropValue();
	       isnum = StringIsValue(kv->value);
	       switch (kl->type) {
		  case PROP_DOUBLE:
		     if (isnum)
		        vl->value.dval = ConvertStringToFloat(kv->value);
		     else
		        vl->value.dval = ConvertStringToFloat(ConvertParam(kv->value));
		     break;
		  case PROP_STRING:
		     vl->value.string = kv->value;  /* NOT a copy! */
		     break;
		  case PROP_INTEGER:
		     if (isnum)
		        vl->value.ival = ConvertStringToInteger(kv->value);
		     else
		        vl->value.ival = ConvertStringToInteger(ConvertParam(kv->value));
		     break;
	       }
	       vl->next = NULL;
	       if (lastvl == NULL)
	          tp->instance = (char *)vl;
	       else
		  lastvl->next = (struct valuelist *)vl;
	       
	       lastvl = vl;
	       break;
	    }
	}
        if (kv == NULL) {
	    /* The list is missing a needed parameter */
	    Printf("Warning:  instance of %s is missing property \"%s\"!\n",
			model, kl->key);
	    vl = NewPropValue();
	    switch (kl->type) {
	       case PROP_DOUBLE:
		  vl->value.dval = (double)0;
		  break;
	       case PROP_STRING:
		  vl->value.string = NULL;
		  break;
	       case PROP_INTEGER:
		  vl->value.ival = (int)0;
		  break;
	    }
	    vl->next = NULL;
	    if (lastvl == NULL)
	       tp->instance = (char *)vl;
	    else
	       lastvl->next = (struct valuelist *)vl;
	       
	    lastvl = vl;
	}
    }

    AddToCurrentCell(tp);
}

/*--------------------------------------------------------------*/
/* Copy properties from one object to another (used when	*/
/* flattening cells).						*/
/*--------------------------------------------------------------*/

void CopyProperties(struct objlist *obj_to, struct objlist *obj_from)
{
   struct keyvalue *kv, *kvcur, *kvlast;
   struct valuelist *vl, *vlcur, *vllast;
   
   vllast = NULL;
   for (vl = (struct valuelist *)obj_from->instance; vl != NULL; vl = vl->next) {
      vlcur = NewPropValue();
      vlcur->value = vl->value;
      vlcur->next = NULL;
      if (vllast)
	 vllast->next = vlcur;
      else
	 obj_to->instance = (char *)vlcur;
      vllast = vlcur;
   }

   kvlast = NULL;
   for (kv = (struct keyvalue *)obj_from->model; kv != NULL; kv = kv->next) {
      kvcur = NewKeyValue();
      kvcur->key = strsave(kv->key);
      kvcur->value = strsave(kv->value);
      kvcur->next = NULL;
      if (kvlast)
         kvlast = kvcur;
      else
	 obj_to->model = (char *)kvcur;
      kvlast = kvcur;
   }
}

/*--------------------------------------------------------------*/
/* Convert a string to an integer.				*/
/* At the moment, we do nothing with error conditions.		*/
/*--------------------------------------------------------------*/

int ConvertStringToInteger(char *string)
{
   long ival = 0;
   char *eptr = NULL;

   ival = strtol(string, &eptr, 10);
   return (int)ival;   
}

/*--------------------------------------------------------------*/
/* Check if a string is a valid number (with optional metric	*/
/* unit suffix).						*/
/* Returns 1 if the string is a proper value, 0 if not.		*/
/*--------------------------------------------------------------*/

int StringIsValue(char *string)
{
   double fval;
   char *eptr = NULL;

   fval = strtod(string, &eptr);
   if (eptr > string)
   {
      while (isspace(*eptr)) eptr++;
      switch (tolower(*eptr)) {
	 case 'g':	/* giga */
	 case 'k':	/* kilo */
	 case 'c':	/* centi */
	 case 'm':	/* milli */
	 case 'u':	/* micro */
	 case 'n':	/* nano */
	 case 'p':	/* pico */
	 case 'f':	/* femto */
	 case 'a':	/* atto */
	 case '\0':	/* no units */
	    return 1;
      }
   }
   return 0;
}

/*--------------------------------------------------------------*/
/* Convert a string with possible metric notation into a float.	*/
/* This follows SPICE notation with case-insensitive prefixes,	*/
/* using "meg" to distinguish 1x10^6 from "m" 1x10^-3		*/
/*--------------------------------------------------------------*/

double ConvertStringToFloat(char *string)
{
   double fval;
   char *eptr = NULL;

   fval = strtod(string, &eptr);
   if (eptr > string)
   {
      while (isspace(*eptr)) eptr++;
      switch (tolower(*eptr)) {
	 case 'g':	/* giga */
	    fval *= 1.0e9;
	    break;
	 case 'k':	/* kilo */
	    fval *= 1.0e3;
	    break;
	 case 'c':	/* centi */
	    fval *= 1.0e-2;
	    break;
	 case 'm':	/* milli */
	    if (tolower(*(eptr + 1)) == 'e' &&
			tolower(*(eptr + 2)) == 'g')
	       fval *= 1.0e6;
	    else
	       fval *= 1.0e-3;
	    break;
	 case 'u':	/* micro */
	    fval *= 1.0e-6;
	    break;
	 case 'n':	/* nano */
	    fval *= 1.0e-9;
	    break;
	 case 'p':	/* pico */
	    fval *= 1.0e-12;
	    break;
	 case 'f':	/* femto */
	    fval *= 1.0e-15;
	    break;
	 case 'a':	/* atto */
	    fval *= 1.0e-18;
	    break;
      }
   }
   return fval;
}

/*--------------------------------------------------------------*/
/* Convert a string into a double, scale it, and pass it back	*/
/* as another string value.					*/
/*--------------------------------------------------------------*/

char *ScaleStringFloatValue(char *vstr, double scale)
{
   static char newstr[32];
   double fval, afval;

   fval = ConvertStringToFloat(vstr);
   fval *= scale;
   
   snprintf(newstr, 31, "%g", fval);
   return newstr;
}

/*----------------------------------------------------------------------*/
/* Find a parameter name in the current cell, and return a pointer to	*/
/* its default (string) value						*/
/*----------------------------------------------------------------------*/

char *ConvertParam(char *paramname)
{
   struct keyvalue *kv;
   struct keylist *plist;
   struct objlist *tp;

   /* Find parameter in parameter list, and find what type it	*/
   /* is supposed to be.  Then find it again in the cell's	*/
   /* value list, and return the value as a type float.		*/

   for (plist = CurrentCell->proplist; plist; plist = plist->next) {
      if ((*matchfunc)(plist->key, paramname)) {
	 if (plist->type != PROP_STRING) return paramname; 	// Error
	 for (tp = CurrentCell->cell; tp; tp = tp->next)
	    if (tp->type == PROPERTY)
	       for (kv = (struct keyvalue *)tp->model; kv; kv = kv->next)
		  if ((*matchfunc)(kv->key, paramname))
		     return kv->value;
      }
   }
   return paramname;	// Error condition
}

/*----------------------------------------------------------------------*/
/* Workhorse subroutine for the Connect() function			*/
/*----------------------------------------------------------------------*/

void join(char *node1, char *node2)
{
	struct objlist *tp1, *tp2, *tp3;
	int nodenum, oldnode;

	if (CurrentCell == NULL) {
		Printf( "No current cell for join(%s,%s)\n",
			node1,node2);
		return;
	}
	tp1 = LookupObject(node1, CurrentCell);
	if (tp1 == NULL) {
		Printf("No node '%s' found in current cell '%s'\n",
			node1, CurrentCell->name);
		return;
	}
	tp2 = LookupObject(node2, CurrentCell);
	if (tp2 == NULL) {
		Printf("No node '%s' found in current cell '%s'\n",
			node2, CurrentCell->name);
		return;
	}
	if (Debug) Printf("         joining: %s == %s (",
		           tp1->name,tp2->name);
	
	/* see if either node has an assigned node number */
	if ((tp1->node == -1) && (tp2->node == -1)) {
		tp1->node = NextNode;
		tp2->node = NextNode++;
		if (Debug) Printf("New ");
	}
	else if (tp1->node == -1) tp1->node = tp2->node;
	else if (tp2->node == -1) tp2->node = tp1->node;
	else {
		if (tp1->node < tp2->node) {
			nodenum = tp1->node;
			oldnode = tp2->node;
		} else {
			nodenum = tp2->node;
			oldnode = tp1->node;
		}
		/* now search through entire list, updating nodes as needed */
		for (tp3 = CurrentCell->cell; tp3 != NULL; tp3 = tp3->next) 
			if (tp3->node == oldnode)  tp3->node = nodenum;
	}
	if (Debug) Printf("Node = %d)\n",tp1->node);
}

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void Connect(char *tplt1, char *tplt2)
{
	struct objlist *list1, *list2;
	int n1, n2;  /* lengths of two lists */

	if (Debug) Printf("      Connect(%s,%s)\n",tplt1,tplt2);
	if (CurrentCell == NULL) {
		Printf( "No current cell for Connect(%s,%s)\n",
			tplt1,tplt2);
		return;
	}
	list1 = List(tplt1);
	n1 = ListLen(list1);
	list2 = List(tplt2);
	n2 = ListLen(list2);
	if (n1==n2) {
	  while (list1 != NULL) {
	    join(list1->name,list2->name);
	    list1 = list1->next;
	    list2 = list2->next;
	  }
	}
	else if (n1==1 && n2>0) {
		while (list2 != NULL) {
			join(list1->name,list2->name);
			list2 = list2->next;
		}
	}
	else if (n2==1 && n1>0) {
		while (list1 != NULL) {
			join(list1->name,list2->name);
			list1 = list1->next;
		}
	}
	else Printf("Unequal element lists: '%s' has %d, '%s' has %d.\n",
		    tplt1,n1,tplt2,n2);
}
		
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void PortList(char *prefix, char *list_template)
{
  struct objlist *list;
  char buffer[1024];
  int buflen;
  int i;
	
  for (list = List(list_template); list != NULL; list = list->next) {
    strcpy(buffer,prefix);
    strcat(buffer,list->name);
    buflen = strlen(buffer);
    for (i=0; i < buflen; i++)
      if (buffer[i] == SEPARATOR[0]) buffer[i] = PORT_DELIMITER[0];
    Port(buffer);
    join(buffer,list->name);
  }
}
		
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void Place(char *name)
{
  char *freename;
  char buffer1[1024], buffer2[1024];
  char prefix[20];
	
  QuickSearch = (LastPlaced != NULL);
  freename = Next(name);
  Instance(name,freename);
  if (Composition == HORIZONTAL) {
    sprintf(buffer2,"%s%s%s%s%s", freename, SEPARATOR, "W", PORT_DELIMITER, "*");
    if (LastPlaced != NULL) {
      sprintf(buffer1,"%s%s%s%s%s", 
	      LastPlaced->instance, SEPARATOR, "E", PORT_DELIMITER, "*");
      Connect (buffer1,buffer2);
    }
    else {  /* promote left-hand ports */
      sprintf(prefix,"%s%s","W", PORT_DELIMITER);
      PortList(prefix,buffer2);  
    }
    buffer2[strlen(buffer2)-3] = 'N';
    sprintf(prefix,"%s%s", "N", PORT_DELIMITER);
    PortList(prefix,buffer2);
    buffer2[strlen(buffer2)-3] = 'S';
    sprintf(prefix,"%s%s", "S", PORT_DELIMITER);
    PortList(prefix,buffer2);
  }
  else if (Composition == VERTICAL) {
    sprintf(buffer2,"%s%s%s%s%s",
	    freename, SEPARATOR, "S", PORT_DELIMITER, "*");
    if (LastPlaced != NULL) {
      sprintf(buffer1,"%s%s%s%s%s",
	      LastPlaced->instance, SEPARATOR, "N", PORT_DELIMITER, "*");
      Connect (buffer1,buffer2);
    }
    else { /* promote bottom ports */
      sprintf(prefix,"%s%s","S", PORT_DELIMITER);
      PortList(prefix,buffer2);  
    }
    buffer2[strlen(buffer2)-3] = 'E';
    sprintf(prefix,"%s%s", "E", PORT_DELIMITER);
    PortList(prefix,buffer2);
    buffer2[strlen(buffer2)-3] = 'W';
    sprintf(prefix,"%s%s", "W", PORT_DELIMITER);
    PortList(prefix,buffer2);
  }
  LastPlaced = LookupInstance(freename,CurrentCell);
  QuickSearch = 0;
  FreeString(freename);
}

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void Array(char *Cell, int num)
{
        int i;

	for (i=0; i<num; i++) {
	  if (Debug) Printf(".");
	  Place(Cell);
	}
}

	
/* if TRUE, always connect all nodes upon EndCell() */
int NoDisconnectedNodes = 0;

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void ConnectAllNodes(char *model, int file)
/* Within the definition of 'model', traverse the object list
   and connect all the otherwise disconnected nodes (i.e., those
   with node==-1) to unique node numbers */
{
  int nodenum;
  struct nlist *tp;
  struct objlist *ob;

  if ((tp = LookupCellFile(model, file)) == NULL) {
    Printf("Cell: %s does not exist.\n", model);
    return;
  }
  
  nodenum = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->node >= nodenum) nodenum = ob->node + 1;

  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->node == -1) ob->node = nodenum++;
}

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void EndCell(void)
{
  char buffer1[1024];
  char prefix[10];
	
  if (CurrentCell == NULL) return;
  
  if (Composition == HORIZONTAL) {
    if (LastPlaced != NULL) {
      sprintf(buffer1,"%s%s%s%s%s",
	      LastPlaced->instance, SEPARATOR, "E", PORT_DELIMITER, "*");
      sprintf(prefix,"%s%s", "E", PORT_DELIMITER);
      PortList(prefix,buffer1);
    }
  }
  else if (Composition == VERTICAL) { /* vcomposing */
    if (LastPlaced != NULL) {
      sprintf(buffer1,"%s%s%s%s%s",
	      LastPlaced->instance, SEPARATOR, "N", PORT_DELIMITER, "*");
      sprintf(prefix,"%s%s", "N", PORT_DELIMITER);
      PortList(prefix,buffer1);
    }
  }
  LastPlaced = NULL;
  CacheNodeNames(CurrentCell);
  if (NoDisconnectedNodes)  ConnectAllNodes(CurrentCell->name, CurrentCell->file);
  CurrentCell = NULL;
  CurrentTail = NULL;
}

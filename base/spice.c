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

/* spice.c -- Input / output for SPICE and ESACAP formats */

#include "config.h"

#include <stdio.h>
#if 0
#include <stdarg.h>  /* what about varargs, like in pdutils.c ??? */
#endif

#ifdef IBMPC
#include <stdlib.h>  /* for calloc(), free() */
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "netfile.h"
#include "print.h"

void SpiceSubCell(struct nlist *tp, int IsSubCell)
{
  struct objlist *ob;
  int node, maxnode;
  char *model;

  /* check to see that all children have been dumped */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      struct nlist *tp2;

      tp2 = LookupCellFile(ob->model, tp->file);
      if ((tp2 != NULL) && !(tp2->dumped) && (tp2->class == CLASS_SUBCKT)) 
	SpiceSubCell(tp2, 1);
    }
  }

  /* print preface, if it is a subcell */
  if (IsSubCell) {
    FlushString(".SUBCKT %s ",tp->name);
    for (ob = tp->cell; ob != NULL; ob = ob->next) 
      if (IsPortInPortlist(ob, tp)) FlushString("%d ", ob->node);
    FlushString("\n");
  }

  /* print names of all nodes, prefixed by comment character */
  maxnode = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->node > maxnode) maxnode = ob->node;

/* was:  for (node = 0; node <= maxnode; node++)  */
  for (node = 1; node <= maxnode; node++) 
    FlushString("# %3d = %s\n", node, NodeName(tp, node));

  /* traverse list of objects */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
     if (ob->type == FIRSTPIN) {
        int drain_node, gate_node, source_node;
	char spice_class;
	struct nlist *tp2;

	tp2 = LookupCellFile(ob->model, tp->file);
	model = tp2->name;

	/* Convert class numbers (defined in netgen.h) to SPICE classes */
	switch (tp2->class) {
	   case CLASS_NMOS4: case CLASS_PMOS4: case CLASS_FET4:
	   case CLASS_NMOS: case CLASS_PMOS: case CLASS_FET3:
	   case CLASS_FET: case CLASS_ECAP:
	      spice_class = 'M';
	      break;
	   case CLASS_NPN: case CLASS_PNP: case CLASS_BJT:
	      spice_class = 'Q';
	      break;
	   case CLASS_RES: case CLASS_RES3:
	      spice_class = 'R';
	      break;
	   case CLASS_DIODE:
	      spice_class = 'D';
	      break;
	   case CLASS_INDUCTOR:
	      spice_class = 'L';
	      break;
	   case CLASS_CAP: case CLASS_CAP3:
	      spice_class = 'C';
	      break;
	   case CLASS_SUBCKT: case CLASS_MODULE:
	      spice_class = 'X';
	      break;
	   case CLASS_XLINE:
	      spice_class = 'T';
	      break;
	   default:
	      Printf ("Bad device class found.\n");
	      continue;		/* ignore it. . . */
	}
	
        FlushString("%c%s", spice_class, ob->instance);

        /* Print out nodes.  FETs switch node order */

	switch (tp2->class) {

	   /* 3-terminal FET devices---handled specially */
	   case CLASS_NMOS: case CLASS_PMOS: case CLASS_FET3:
	      ob = ob->next;
              FlushString(" %s", ob->name);		/* drain */
	      ob = ob->next;
              FlushString(" %s", ob->name);		/* gate */
	      ob = ob->next;
              FlushString(" %s", ob->name);		/* source */
	      ob = ob->next;
	      if (tp2->class == CLASS_NMOS)
                 FlushString(" GND!");		/* default substrate */
	      else if (tp2->class == CLASS_PMOS)
                 FlushString(" VDD!");		/* default well */
	      else 
                 FlushString(" BULK");		/* default bulk---unknown */
	      break;

	   /* All other devices have nodes in order of SPICE syntax */
	   default:
	      while (ob->next != NULL && ob->next->type >= FIRSTPIN) {
                 ob = ob->next;
                 FlushString(" %s", ob->name);
	      }
	      break;
	}

	/* caps and resistors, print out device value */


	/* print out device type (model/subcircuit name) */

	switch (tp2->class) {
	   case CLASS_CAP:
	      if (matchnocase(model, "c")) {
		 ob = ob->next;
		 if (ob->type == PROPERTY) {
		    struct valuelist *vl;
		    vl = (struct valuelist *)ob->instance;
		    if (vl) FlushString(" %g", vl->value.dval);
		 }
	      }
	      else
		 FlushString(" %s", model); 	/* semiconductor capacitor */
	      break;

	   case CLASS_RES:
	      if (matchnocase(model, "r")) {
		 ob = ob->next;
		 if (ob->type == PROPERTY) {
		    struct valuelist *vl;
		    vl = (struct valuelist *)ob->instance;
		    if (vl) FlushString(" %g", vl->value.dval);
		 }
	      }
	      else
		 FlushString(" %s", model); 	/* semiconductor resistor */
	      break;

	   default:
	      FlushString(" %s", model);	/* everything else */
	}
	   
	/* write properties (if any) */
	if (ob) ob = ob->next;
	if (ob && ob->type == PROPERTY) {
	   struct keyvalue *kv;
	   for (kv = (struct keyvalue *)ob->model; kv != NULL; kv = kv->next) {
	      FlushString(" %s=%s", kv->key, kv->value);
	   }
	}
	FlushString("\n");
    }
  }
	
  if (IsSubCell) FlushString(".ENDS\n");
  tp->dumped = 1;
}


void SpiceCell(char *name, int fnum, char *filename)
{
  struct nlist *tp;
  char FileName[500];

  tp = LookupCellFile(name, fnum);

  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  if (filename == NULL || strlen(filename) == 0) 
    SetExtension(FileName, name, SPICE_EXTENSION);
  else 
    SetExtension(FileName, filename, SPICE_EXTENSION);

  if (!OpenFile(FileName, 80)) {
    perror("ext(): Unable to open output file.");
    return;
  }
  ClearDumpedList();
  /* all spice decks begin with comment line */
  FlushString("SPICE deck for cell %s written by Netgen %s.%s\n\n", 
	      name, NETGEN_VERSION, NETGEN_REVISION);
  SpiceSubCell(tp, 0);
  CloseFile(FileName);
}

/*------------------------------------------------------*/
/* Routine to update instances with proper pin names,	*/
/* if the instances were called before the cell		*/
/* definition.						*/
/*------------------------------------------------------*/

int renamepins(struct hashlist *p, int file)
{
   struct nlist *ptr, *tc;
   struct objlist *ob, *ob2, *obp;

   ptr = (struct nlist *)(p->ptr);

   if (ptr->file != file)
      return 1;

   for (ob = ptr->cell; ob != NULL; ob = ob->next) {
      if (ob->type == FIRSTPIN) {
	 tc = LookupCellFile(ob->model, file);
	 obp = ob;
	 for (ob2 = tc->cell; ob2 != NULL; ob2 = ob2->next) {
	    if (ob2->type != PORT) break;
	    if (!matchnocase(ob2->name, obp->name + strlen(obp->instance) + 1)) {
	       // Printf("Cell %s pin correspondence: %s vs. %s\n",
	       // 	tc->name, obp->name, ob2->name);
	       FREE(obp->name);
	       obp->name = (char *)MALLOC(strlen(obp->instance) + strlen(ob2->name) + 2);
	       sprintf(obp->name, "%s/%s", obp->instance, ob2->name);
	    }
	    obp = obp->next;
	 }
      }
   }
}

/*------------------------------------------------------*/
/* Structure for stacking nested subcircuit definitions */
/*------------------------------------------------------*/

struct cellstack {
   char *cellname;
   struct cellstack *next;
};

/*------------------------------------------------------*/
/* Push a subcircuit name onto the stack		*/
/*------------------------------------------------------*/

void PushStack(char *cellname, struct cellstack **top)
{
   struct cellstack *newstack;

   newstack = (struct cellstack *)CALLOC(1, sizeof(struct cellstack));
   newstack->cellname = cellname; 
   newstack->next = *top;
   *top = newstack; 
}

/*------------------------------------------------------*/
/* Pop a subcircuit name off of the stack		*/
/*------------------------------------------------------*/

void PopStack(struct cellstack **top)
{
   struct cellstack *stackptr;

   stackptr = *top;
   if (!stackptr) return;
   *top = stackptr->next;
   FREE(stackptr);
}

/* Forward declaration */
extern void IncludeSpice(char *, int, struct cellstack **);

/*------------------------------------------------------*/
/* Read a SPICE deck					*/
/*------------------------------------------------------*/

void ReadSpiceFile(char *fname, int filenum, struct cellstack **CellStackPtr)
{
  int cdnum = 1, rdnum = 1, ndev, multi;
  int warnings = 0, update = 0;
  char *eqptr, devtype;
  struct keyvalue *kvlist = NULL;
  char inst[100], model[100], instname[100];
  struct nlist *tp;
  struct objlist *parent, *sobj, *nobj, *lobj, *pobj;

  while (!EndParseFile()) {
    SkipTok(); /* get the next token */
    if (EndParseFile()) break;

    if (nexttok[0] == '*') SkipNewLine();

    else if (toupper(nexttok[0]) == 'Q') {
      char emitter[100], base[100], collector[100];

      if (!(*CellStackPtr)) {
	CellDefNoCase(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, 99); SpiceTokNoNewline(); 
      strncpy(collector, nexttok, 99); SpiceTokNoNewline();
      strncpy(base, nexttok, 99);   SpiceTokNoNewline();
      strncpy(emitter, nexttok, 99);  SpiceTokNoNewline();
      /* make sure all the nodes exist */
      if (LookupObject(collector, CurrentCell) == NULL) Node(collector);
      if (LookupObject(base, CurrentCell) == NULL) Node(base);
      if (LookupObject(emitter, CurrentCell) == NULL) Node(emitter);

      /* Read the device model */
      snprintf(model, 99, "%s", nexttok);

      ndev = 1;
      while (nexttok != NULL)
      {
	 /* Parse for M and other parameters */

	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    if (!strcasecmp(nexttok, "M"))
		sscanf(eqptr + 1, "%d", &ndev);
	    else 
		AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
      }

      if (LookupCellFile(model, filenum) == NULL) {
	 CellDefNoCase(model, filenum);
	 Port("collector");
	 Port("base");
	 Port("emitter");
	 SetClass(CLASS_BJT);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
      }

      multi = (ndev > 1) ? 1 : 0;
      if (!multi) sprintf(instname, "%s%s", model, inst);
      while (ndev > 0)
      {
         if (multi) sprintf(instname, "%s%s.%d", model, inst, ndev);
	 Cell(instname, model, collector, base, emitter);
	 LinkProperties(model, kvlist);
	 ndev--;
      }
      DeleteProperties(&kvlist);
    }
    else if (toupper(nexttok[0]) == 'M') {
      char drain[100], gate[100], source[100], bulk[100];

      if (!(*CellStackPtr)) {
	CellDefNoCase(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, 99); SpiceTokNoNewline(); 
      strncpy(drain, nexttok, 99);  SpiceTokNoNewline();
      strncpy(gate, nexttok, 99);   SpiceTokNoNewline();
      strncpy(source, nexttok, 99); SpiceTokNoNewline();
      /* make sure all the nodes exist */
      if (LookupObject(drain, CurrentCell) == NULL) Node(drain);
      if (LookupObject(gate, CurrentCell) == NULL) Node(gate);
      if (LookupObject(source, CurrentCell) == NULL) Node(source);

      /* handle the substrate node */
      strncpy(bulk, nexttok, 99); SpiceTokNoNewline();
      if (LookupObject(bulk, CurrentCell) == NULL) Node(bulk);

      /* Read the device model */
      snprintf(model, 99, "%s", nexttok);

      ndev = 1;
      while (nexttok != NULL)
      {
	 /* Parse for parameters; treat "M" separately */

	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    if (!strcasecmp(nexttok, "M"))
		sscanf(eqptr + 1, "%d", &ndev);
	    else if (!strcasecmp(nexttok, "L"))
		AddProperty(&kvlist, "length", eqptr + 1);
	    else if (!strcasecmp(nexttok, "W"))
		AddProperty(&kvlist, "width", eqptr + 1);
	    else 
		AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
      }

      /* Treat each different model name as a separate device class	*/
      /* The model name is prefixed with "M/" so that we know this is a	*/
      /* SPICE transistor.						*/

      if (LookupCellFile(model, filenum) == NULL) {
	 CellDefNoCase(model, filenum);
	 Port("drain");
	 Port("gate");
	 Port("source");
	 Port("bulk");
	 PropertyDouble("length", 0.01);
	 PropertyDouble("width", 0.01);
	 SetClass(CLASS_FET);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
      }

      multi = (ndev > 1) ? 1 : 0;
      if (!multi) sprintf(instname, "%s%s", model, inst);
      while (ndev > 0)
      {
         if (multi) sprintf(instname, "%s%s.%d", model, inst, ndev);
	 Cell(instname, model, drain, gate, source, bulk);
	 LinkProperties(model, kvlist);
	 ndev--;
      }
      DeleteProperties(&kvlist);
      SpiceSkipNewLine();
    }
    else if (toupper(nexttok[0]) == 'C') {	/* 2-port capacitors */
      int usemodel = 0;

      if (IgnoreRC) {
	 SpiceSkipNewLine();
      }
      else {
        char ctop[200], cbot[200];

        if (!(*CellStackPtr)) {
	  CellDefNoCase(fname, filenum);
	  PushStack(fname, CellStackPtr);
        }
        strncpy(inst, nexttok + 1, 99); SpiceTokNoNewline(); 
        strncpy(ctop, nexttok, 99); SpiceTokNoNewline();
        strncpy(cbot, nexttok, 99); SpiceTokNoNewline();

        /* make sure all the nodes exist */
        if (LookupObject(ctop, CurrentCell) == NULL) Node(ctop);
        if (LookupObject(cbot, CurrentCell) == NULL) Node(cbot);

	/* Get capacitor value (if present), save as property "value" */
	if (nexttok != NULL) {
	   if (StringIsValue(nexttok)) {
	      AddProperty(&kvlist, "value", nexttok);
	      SpiceTokNoNewline();
	   }
	}

	/* Semiconductor (modeled) capacitor.  But first need to make	*/
	/* sure that this does not start the list of parameters.	*/

	model[0] = '\0';
	if ((nexttok != NULL) && ((eqptr = strchr(nexttok, '=')) == NULL))
	   snprintf(model, 99, "%s", nexttok);

	/* Any other device properties? */
	ndev = 1;
        while (nexttok != NULL)
        {
	   SpiceTokNoNewline();
	   if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	   if ((eqptr = strchr(nexttok, '=')) != NULL) {
	      *eqptr = '\0';
	      if (!strcasecmp(nexttok, "M"))
		 sscanf(eqptr + 1, "%d", &ndev);
	      else
	         AddProperty(&kvlist, nexttok, eqptr + 1);
	   }
	   else if (!strncmp(nexttok, "$[", 2)) {
	      // Support for CDL modeled capacitor format
	      snprintf(model, 99, "%s", nexttok + 2);
	      if ((eqptr = strchr(model, ']')) != NULL)
		 *eqptr = '\0';
	   }
	}

	if (model[0] == '\0')
	   strcpy(model, "c");		/* Use default capacitor model */
	else
	{
	   if (LookupCellFile(model, filenum) == NULL) {
	      CellDefNoCase(model, filenum);
	      Port("top");
	      Port("bottom");
	      PropertyDouble("value", 0.01);
	      SetClass(CLASS_CAP);
              EndCell();
	      ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
	   }
	   usemodel = 1;
	}

	multi = (ndev > 1) ? 1 : 0;
	if (!multi) sprintf(instname, "%s%s", model, inst);

	while (ndev > 0) {
	   if (multi) sprintf(instname, "%s%s.%d", model, inst, ndev);
	   if (usemodel)
              Cell(instname, model, ctop, cbot);
	   else
              Cap((*CellStackPtr)->cellname, instname, ctop, cbot);
	   LinkProperties(model, kvlist);
	   ndev--;
	}
	DeleteProperties(&kvlist);
      }
    }
    else if (toupper(nexttok[0]) == 'R') {	/* 2-port resistors */
      int usemodel = 0;

      if (IgnoreRC) {
	 SpiceSkipNewLine();
      }
      else {
        char rtop[200], rbot[200];

        if (!(*CellStackPtr)) {
	  CellDefNoCase(fname, filenum);
	  PushStack(fname, CellStackPtr);
        }
        strncpy(inst, nexttok + 1, 99); SpiceTokNoNewline(); 
        strncpy(rtop, nexttok, 99);  SpiceTokNoNewline();
        strncpy(rbot, nexttok, 99);   SpiceTokNoNewline();
        /* make sure all the nodes exist */
        if (LookupObject(rtop, CurrentCell) == NULL) Node(rtop);
        if (LookupObject(rbot, CurrentCell) == NULL) Node(rbot);

	/* Get resistor value (if present); save as property "value" */

	if (nexttok != NULL) {
	   if (StringIsValue(nexttok)) {
	      AddProperty(&kvlist, "value", nexttok);
	      SpiceTokNoNewline();
	   }
        }

	/* Semiconductor (modeled) resistor.  But first need to make	*/
	/* sure that this does not start the list of parameters.	*/

	model[0] = '\0';
	if ((nexttok != NULL) && ((eqptr = strchr(nexttok, '=')) == NULL))
	   snprintf(model, 99, "%s", nexttok);

	/* Any other device properties? */
	ndev = 1;
        while (nexttok != NULL) {
	   SpiceTokNoNewline();
	   if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	   if ((eqptr = strchr(nexttok, '=')) != NULL) {
	      *eqptr = '\0';
	      if (!strcasecmp(nexttok, "M"))
		 sscanf(eqptr + 1, "%d", &ndev);
	      else
	         AddProperty(&kvlist, nexttok, eqptr + 1);
	   }
	   else if (!strncmp(nexttok, "$[", 2)) {
	      // Support for CDL modeled resistor format
	      snprintf(model, 99, "%s", nexttok + 2);
	      if ((eqptr = strchr(model, ']')) != NULL)
		 *eqptr = '\0';
	   }
	}

	if (model[0] != '\0')
	{
	   if (LookupCellFile(model, filenum) == NULL) {
	      CellDefNoCase(model, filenum);
	      Port("end_a");
	      Port("end_b");
	      PropertyDouble("value", 0.01);
	      SetClass(CLASS_RES);
              EndCell();
	      ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
	   }
	   usemodel = 1;
	}
	else
	   strcpy(model, "r");		/* Use default resistor model */

	multi = (ndev > 1) ? 1 : 0;
	if (!multi) sprintf(instname, "%s%s", model, inst);

	while (ndev > 0) {
	   if (multi) sprintf(instname, "%s%s.%d", model, inst, ndev);
	   if (usemodel)
	      Cell(instname, model, rtop, rbot);
	   else
              Res((*CellStackPtr)->cellname, instname, rtop, rbot);
	   LinkProperties(model, kvlist);
	   ndev--;
	}
	DeleteProperties(&kvlist);
      }
    }
    else if (toupper(nexttok[0]) == 'D') {	/* diode */
      char cathode[100], anode[100];

      if (!(*CellStackPtr)) {
	CellDefNoCase(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, 99); SpiceTokNoNewline(); 
      strncpy(anode, nexttok, 99);   SpiceTokNoNewline();
      strncpy(cathode, nexttok, 99); SpiceTokNoNewline();
      /* make sure all the nodes exist */
      if (LookupObject(anode, CurrentCell) == NULL) Node(anode);
      if (LookupObject(cathode, CurrentCell) == NULL) Node(cathode);

      /* Read the device model */
      snprintf(model, 99, "%s", nexttok);

      ndev = 1;
      while (nexttok != NULL)
      {
	 /* Parse for M and other parameters */

	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    if (!strcasecmp(nexttok, "M"))
		sscanf(eqptr + 1, "%d", &ndev);
	    else 
		AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
      }

      if (LookupCellFile(model, filenum) == NULL) {
	 CellDefNoCase(model, filenum);
	 Port("anode");
	 Port("cathode");
	 SetClass(CLASS_DIODE);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
      }
      multi = (ndev > 1) ? 1 : 0;
      if (!multi) sprintf(instname, "%s%s", model, inst);
      while (ndev > 0)
      {
         if (multi) sprintf(instname, "%s%s.%d", model, inst, ndev);
	 Cell(instname, model, anode, cathode);
	 LinkProperties(model, kvlist);
	 ndev--;
      }
      DeleteProperties(&kvlist);
    }
    else if (toupper(nexttok[0]) == 'T') {	/* transmission line */
      int usemodel = 0;

      if (IgnoreRC) {
	 SpiceSkipNewLine();
      }
      else {
        char node1[200], node2[200], node3[200], node4[200];

        if (!(*CellStackPtr)) {
	  CellDefNoCase(fname, filenum);
	  PushStack(fname, CellStackPtr);
        }
        strncpy(inst, nexttok + 1, 99); SpiceTokNoNewline(); 
        strncpy(node1, nexttok, 99);  SpiceTokNoNewline();
        strncpy(node2, nexttok, 99);   SpiceTokNoNewline();
        strncpy(node3, nexttok, 99);   SpiceTokNoNewline();
        strncpy(node4, nexttok, 99);   SpiceTokNoNewline();
        /* make sure all the nodes exist */
        if (LookupObject(node1, CurrentCell) == NULL) Node(node1);
        if (LookupObject(node2, CurrentCell) == NULL) Node(node2);
        if (LookupObject(node3, CurrentCell) == NULL) Node(node3);
        if (LookupObject(node4, CurrentCell) == NULL) Node(node4);

	/* Lossy (modeled) transmission line.  But first need to make	*/
	/* sure that this does not start the list of parameters.	*/

	model[0] = '\0';
	if ((nexttok != NULL) && ((eqptr = strchr(nexttok, '=')) == NULL))
	   snprintf(model, 99, "%s", nexttok);

	/* Any other device properties? */
	ndev = 1;
        while (nexttok != NULL) {
	   SpiceTokNoNewline();
	   if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	   if ((eqptr = strchr(nexttok, '=')) != NULL) {
	      *eqptr = '\0';
	      if (!strcasecmp(nexttok, "M"))
		 sscanf(eqptr + 1, "%d", &ndev);
	      else
	         AddProperty(&kvlist, nexttok, eqptr + 1);
	   }
	}

	if (model[0] != '\0')
	{
	   if (LookupCellFile(model, filenum) == NULL) {
	      CellDefNoCase(model, filenum);
	      Port("node1");
	      Port("node2");
	      Port("node3");
	      Port("node4");
	      SetClass(CLASS_XLINE);
              EndCell();
	      ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
	   }
	   usemodel = 1;
	}
	else
	   strcpy(model, "t");		/* Use default xline model */

	multi = (ndev > 1) ? 1 : 0;
	if (!multi) sprintf(instname, "%s%s", model, inst);

	while (ndev > 0) {
	   if (multi) sprintf(instname, "%s%s.%d", model, inst, ndev);
	   if (usemodel)
	      Cell(instname, model, node1, node2, node3, node4);
	   else
              XLine((*CellStackPtr)->cellname, instname, node1, node2,
			node3, node4);
	   LinkProperties(model, kvlist);
	   ndev--;
	}
	DeleteProperties(&kvlist);
      }
    }
    else if (toupper(nexttok[0]) == 'L') {	/* inductor */
      char end_a[100], end_b[100];

      if (!(*CellStackPtr)) {
	CellDefNoCase(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, 99); SpiceTokNoNewline(); 
      strncpy(end_a, nexttok, 99);   SpiceTokNoNewline();
      strncpy(end_b, nexttok, 99); SpiceTokNoNewline();
      /* make sure all the nodes exist */
      if (LookupObject(end_a, CurrentCell) == NULL) Node(end_a);
      if (LookupObject(end_b, CurrentCell) == NULL) Node(end_b);

      /* Read the device model */
      snprintf(model, 99, "%s", nexttok);

      ndev = 1;
      while (nexttok != NULL)
      {
	 /* Parse for M and other parameters */

	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    if (!strcasecmp(nexttok, "M"))
		sscanf(eqptr + 1, "%d", &ndev);
	    else 
		AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
      }

      if (LookupCellFile(model, filenum) == NULL) {
	 CellDefNoCase(model, filenum);
	 Port("end_a");
	 Port("end_b");
	 SetClass(CLASS_INDUCTOR);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
      }
      multi = (ndev > 1) ? 1 : 0;
      if (!multi) sprintf(instname, "%s%s", model, inst);
      while (ndev > 0)
      {
         if (multi) sprintf(instname, "%s%s.%d", model, inst, ndev);
	 Cell(instname, model, end_a, end_b);
	 LinkProperties(model, kvlist);
	 ndev--;
      }
      DeleteProperties(&kvlist);
    }
    else if (toupper(nexttok[0]) == 'X') {	/* subcircuit instances */
      char instancename[100], subcktname[100];

      struct portelement {
	char *name;
	struct portelement *next;
      };

      struct portelement *head, *tail, *scan, *scannext;
      struct objlist *obptr;

      snprintf(instancename, 99, "%s", nexttok + 1);
      strcpy(instancename, nexttok + 1);
      if (!(*CellStackPtr)) {
	CellDefNoCase(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      
      head = NULL;
      tail = NULL;
      SpiceTokNoNewline();
      ndev = 1;
      while (nexttok != NULL) {
	/* must still be a node or a parameter */
	struct portelement *new_port;

	// CDL format compatibility:  Ignore "/" before the subcircuit name
	if (matchnocase(nexttok, "/")) {
           SpiceTokNoNewline();
	   continue;
	}

	// Ignore token called "PARAMS:"
	if (!strcasecmp(nexttok, "PARAMS:")) {
           SpiceTokNoNewline();
	   continue;
	}

	// We need to look for parameters of the type "name=value" BUT
	// we also need to make sure that what we think is a parameter
	// is actually a circuit name with an equals sign character in it.

	if (((eqptr = strchr(nexttok, '=')) != NULL) &&
	    	((tp = LookupCellFile(nexttok, filenum)) == NULL))
	{
	    *eqptr = '\0';
	    if (!strcasecmp(nexttok, "M"))
		sscanf(eqptr + 1, "%d", &ndev);
	    else 
		AddProperty(&kvlist, nexttok, eqptr + 1);
	}
	else
	{
	    new_port = (struct portelement *)CALLOC(1, sizeof(struct portelement));
	    new_port->name = strsave(nexttok);
	    if (head == NULL) head = new_port;
	    else tail->next = new_port;
	    new_port->next = NULL;
	    tail = new_port;
	}
	SpiceTokNoNewline();
      }

      /* find the last element of the list, which is not a port,
         but the class type */
      scan = head;
      while (scan != NULL && scan->next != tail && scan->next != NULL)
	 scan = scan->next;
      tail = scan;
      if (scan->next != NULL) scan = scan->next;
      tail->next = NULL;

      /* Create cell name and revise instance name based on the cell name */
      /* For clarity, if "instancename" does not contain the cellname,	  */
      /* then prepend the cellname to the instance name.  HOWEVER, if any */
      /* netlist is using instancename/portname to name nets, then we	  */
      /* will have duplicate node names with conflicting records.  So at  */
      /* very least prepend an "/" to it. . .				  */

      /* NOTE:  Previously an 'X' was prepended to the name, but this	  */
      /* caused serious and common errors where, for example, the circuit */
      /* defined cells NOR and XNOR, causing confusion between node	  */
      /* names.								  */

      if (strncmp(instancename, scan->name, strlen(scan->name))) {
         snprintf(subcktname, 99, "%s%s", scan->name, instancename);
         strcpy(instancename, subcktname);
      }
      else {
         snprintf(subcktname, 99, "/%s", instancename);
         strcpy(instancename, subcktname);
      }
      snprintf(subcktname, 99, "%s", scan->name);

      FREE (scan->name);
      if (scan == head) {
	 head = NULL;
	 Fprintf(stderr, "Error:  Cell %s has no pins and cannot "
		"be instantiated\n", scan->name);
	 InputParseError(stderr);
      }
      FREE (scan);

      /* Check that the subcell exists.  If not, print a warning and	*/
      /* generate an empty subcircuit entry matching the call.		*/

      tp = LookupCellFile(subcktname, filenum);
      if (tp == NULL) {
	 char defport[8];
	 int i;

	 snprintf(model, 99, "%s", subcktname);
	 Fprintf(stdout, "Call to undefined subcircuit %s\n"
		"Creating placeholder cell definition.\n", subcktname);
	 CellDefNoCase(subcktname, filenum);
         for (scan = head, i = 1; scan != NULL; scan = scan->next, i++) {
	    sprintf(defport, "%d", i);	
	    Port(defport);
	 }
	 SetClass(CLASS_MODULE);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);		/* Reopen */
	 update = 1;
      }

      /* nexttok is now NULL, scan->name points to class */
      multi = (ndev > 1) ? 1 : 0;
      if (multi) strcat(instancename, ".");
      while (ndev > 0) {
         if (multi) {
	    char *dotptr = strrchr(instancename, '.');
	    sprintf(dotptr + 1, "%d", ndev);
	 }
         Instance(subcktname, instancename);
	 LinkProperties(model, kvlist);
	 ndev--;

         /* (Diagnostic) */
         /* Fprintf(stderr, "instancing subcell: %s (%s):", subcktname, instancename); */
         /*
         for (scan = head; scan != NULL; scan = scan->next)
	   Fprintf(stderr," %s", scan->name);
         Fprintf(stderr,"\n");
         */
      
         obptr = LookupInstance(instancename, CurrentCell);
         if (obptr != NULL) {
           scan = head;
	   if (scan != NULL)
           do {
	     if (LookupObject(scan->name, CurrentCell) == NULL) Node(scan->name);
	     join(scan->name, obptr->name);
	     obptr = obptr->next;
	     scan = scan->next;
           } while (obptr != NULL && obptr->type > FIRSTPIN && scan != NULL);

           if ((obptr == NULL && scan != NULL) ||
		(obptr != NULL && scan == NULL && obptr->type > FIRSTPIN)) {
	     if (warnings <= 100) {
	        Fprintf(stderr,"Parameter list mismatch in %s: ", instancename);

	        if (obptr == NULL)
		   Fprintf(stderr, "Too many parameters in call!\n");
	        else if (scan == NULL)
		   Fprintf(stderr, "Not enough parameters in call!\n");
	        InputParseError(stderr);
	        if (warnings == 100)
	           Fprintf(stderr, "Too many warnings. . . will not report any more.\n");
             }
	     warnings++;
	   }
         }	// repeat over ndev
      }
      DeleteProperties(&kvlist);

      /* free up the allocated list */
      scan = head;
      while (scan != NULL) {
	scannext = scan->next;
	FREE(scan->name);
	FREE(scan);
	scan = scannext;
      }
    }
    else if (matchnocase(nexttok, ".SUBCKT")) {
      SpiceTokNoNewline();

      /* Save pointer to current cell */
      if (CurrentCell != NULL)
         parent = CurrentCell->cell;
      else
	 parent = NULL;

      /* Check for existence of the cell.  We may need to rename it. */

      snprintf(model, 99, "%s", nexttok);
      tp = LookupCellFile(nexttok, filenum);

      /* Check for name conflict with duplicate cell names	*/
      /* This may mean that the cell was used before it was	*/
      /* defined, but CDL files sometimes just redefine the	*/
      /* same cell over and over.  So check if it's empty.	*/

      if ((tp != NULL) && (tp->class != CLASS_MODULE)) {
	 int n;
	 char *ds;

	 ds = strrchr(model, '[');
	 if ((ds != NULL) && (*(ds + 1) == '['))
	    sscanf(ds + 2, "%d", &n);
	 else {
	    ds = model + strlen(model);
	    sprintf(ds, "[[0]]");
	    n = -1;
	 }

	 Printf("Duplicate cell %s in file; renaming.\n", nexttok);
         while (tp != NULL) {
	    n++;
	    /* Append "[[n]]" to the preexisting model name to force uniqueness */
	    sprintf(ds, "[[%d]]", n);
            tp = LookupCellFile(model, filenum);
	 }
	 InstanceRename(nexttok, model, filenum);
	 CellRehash(nexttok, model, filenum);
         CellDefNoCase(nexttok, filenum);
         tp = LookupCellFile(nexttok, filenum);
      }
      else if (tp != NULL) {	/* Make a new definition for an empty cell */
	 FreePorts(nexttok);
	 CellDelete(nexttok, filenum);
	 CellDefNoCase(model, filenum);
	 tp = LookupCellFile(model, filenum);
      }
      else if (tp == NULL) {	/* Completely new cell, no name conflict */
         CellDefNoCase(model, filenum);
         tp = LookupCellFile(model, filenum);
      }

      if (tp != NULL) {

	 PushStack(tp->name, CellStackPtr);

         /* Tokens on the rest of the line are ports or		*/
	 /* properties.  Treat everything with an "=" as a	*/
	 /* property, all others as ports. "M=" is *not* a	*/
	 /* valid property meaning "number of" in a SUBCKT	*/
	 /* line, and if it exists, it should be recorded as	*/
	 /* a property, and (to be done) NOT treated as		*/
	 /* referring to number of devices in a subcircuit	*/
	 /* call.						*/

         SpiceTokNoNewline();
         while (nexttok != NULL) {

	    // Because of somebody's stupid meddling with
	    // SPICE syntax, we have to check for and ignore
	    // any use of the keyword "PARAMS:"

	    if (!strcasecmp(nexttok, "PARAMS:")) {
		SpiceTokNoNewline();
		continue;
	    }

	    if ((eqptr = strchr(nexttok, '=')) != NULL)
	    {
		*eqptr = '\0';
		PropertyString(nexttok, 0);	// Only String properties allowed

		// Save default values in a property list
		AddProperty(&kvlist, nexttok, eqptr + 1);
	    }
	    else
		Port(nexttok);

	    SpiceTokNoNewline();
         }
         SetClass(CLASS_SUBCKT);
	 LinkProperties(model, kvlist);
	 DeleteProperties(&kvlist);

	 /* Copy all global nodes from parent into child cell */
	 for (sobj = parent; sobj != NULL; sobj = sobj->next) {
	    if (IsGlobal(sobj->type)) {
	       Global(sobj->name);
	    }
	 }
      }
      else {

skip_ends:
	 /* There was an error, so skip to the end of the	*/
	 /* subcircuit definition				*/

	 while (1) {
	    SpiceSkipNewLine();
	    SkipTok();
	    if (EndParseFile()) break;
	    if (matchnocase(nexttok, ".ENDS")) break;
	 }
      }
    }
    else if (matchnocase(nexttok, ".ENDS")) {
      /* If any pins are marked unconnected, see if there are	*/
      /* other pins of the same name that have connections.	*/
      /* Also remove any unconnected globals (just for cleanup) */

      lobj = NULL;
      for (sobj = CurrentCell->cell; sobj != NULL;) {
	 nobj = sobj->next;
	 if (sobj->node < 0) {
	    if (IsGlobal(sobj->type)) {
	       if (lobj != NULL)
	          lobj->next = sobj->next;
	       else
	          CurrentCell->cell = sobj->next;
	       FreeObjectAndHash(sobj, CurrentCell);
	    }
	    else if (IsPort(sobj->type)) {
		for (pobj = CurrentCell->cell; pobj && (pobj->type == PORT);
			pobj = pobj->next) {
		    if (pobj == sobj) continue;
		    if (matchnocase(pobj->name, sobj->name) && pobj->node >= 0) {
			sobj->node = pobj->node;
			break;
		    }
		}
		lobj = sobj;
	    }
	    else
		lobj = sobj;
	 }
	 else
	    lobj = sobj;
	 sobj = nobj;
      }

      EndCell();

      // This condition will be true if no nodes or components were
      // created in the top-level cell before the first subcircuit
      // definition, so it is not necessarily an error. . .

      // if (*(CellStackPtr) && ((*CellStackPtr)->next == NULL))
      //    Printf(".ENDS encountered outside of a subcell.\n");
      // else

      if (*CellStackPtr) PopStack(CellStackPtr);
      if (*CellStackPtr) ReopenCellDef((*CellStackPtr)->cellname, filenum);
      SkipNewLine();
    }
    else if (matchnocase(nexttok, ".MODEL")) {
      unsigned char class = CLASS_SUBCKT;
      struct nlist *ncell;

      /* A .MODEL statement can refine our knowledge of whether a Q or	*/
      /* M device is type "n" or "p", allowing us to properly translate	*/
      /* to other formats (e.g., .sim).	  If there are no .MODEL	*/
      /* statements, the "equate classes" command must be used.		*/

      SpiceTokNoNewline();
      snprintf(model, 99, "%s", nexttok);
      SpiceTokNoNewline();

      if (!strcasecmp(nexttok, "NMOS")) {
	 class = CLASS_NMOS;
      }
      else if (!strcasecmp(nexttok, "PMOS")) {
	 class = CLASS_PMOS;
      }
      else if (!strcasecmp(nexttok, "PNP")) {
	 class = CLASS_PNP;
      }
      else if (!strcasecmp(nexttok, "NPN")) {
	 class = CLASS_NPN;
      }

      /* Convert class of "model" to "class" */
      if (class != CLASS_SUBCKT) {
         ncell = LookupCellFile(model, filenum);
         if (ncell) ncell->class = class;
      }

      SpiceSkipNewLine();
    }

    // Handle some commonly-used cards

    else if (matchnocase(nexttok, ".GLOBAL")) {
      while (nexttok != NULL) {
	 int numnodes = 0;
         SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;

	 // First handle backward references
	 if (CurrentCell != NULL)
 	    numnodes = ChangeScopeCurrent(nexttok, NODE, GLOBAL);	 

	 /* If there are no backward references, then treat it	*/
	 /* as a forward reference				*/

         if (numnodes == 0) {
	    // If there is no current cell, make one
	    if (!(*CellStackPtr)) {
	       CellDefNoCase(fname, filenum);
	       PushStack(fname, CellStackPtr);
	    }
	    Global(nexttok);
	 }
      }
      SpiceSkipNewLine();
    }
    else if (matchnocase(nexttok, ".INCLUDE")) {
      char *iname, *iptr, *quotptr, *pathend;

      SpiceTokNoNewline();

      // Any file included in another SPICE file needs to be
      // interpreted relative to the path of the parent SPICE file,
      // unless it's an absolute pathname.

      pathend = strrchr(fname, '/');
      iptr = nexttok;
      while (*iptr == '\'' || *iptr == '\"' || *iptr == '`') iptr++;
      if ((pathend != NULL) && (*iptr != '/')) {
	 *pathend = '\0';
	 iname = (char *)MALLOC(strlen(fname) + strlen(iptr) + 2);
	 sprintf(iname, "%s/%s", fname, iptr);
	 *pathend = '/';
      }
      else
	 iname = STRDUP(iptr);

      // Eliminate any single or double quotes around the filename
      iptr = iname;
      quotptr = iptr;
      while (*quotptr != '\'' && *quotptr != '\"' && *quotptr != '`' &&
		*quotptr != '\0' && *quotptr != '\n') quotptr++;
      if (*quotptr == '\'' || *quotptr == '\"' || *quotptr == '`') *quotptr = '\0';
	
      IncludeSpice(iptr, filenum, CellStackPtr);
      FREE(iname);
      SpiceSkipNewLine();
    }
    else if (matchnocase(nexttok, ".END")) {
      /* Well, don't take *my* word for it.  But we won't flag a warning. */
    }
    else {
      if (warnings <= 100) {
	 Fprintf(stderr, "Ignoring line starting with token: %s\n", nexttok);
	 InputParseError(stderr);
	 if (warnings == 100)
	    Fprintf(stderr, "Too many warnings. . . will not report any more.\n");
      }
      warnings++;
      SpiceSkipNewLine();
    }
  }
  if (update != 0) RecurseCellFileHashTable(renamepins, filenum);

  if (warnings)
     Fprintf(stderr, "File %s read with %d warning%s.\n", fname,
		warnings, (warnings == 1) ? "" : "s");
}

/*----------------------------------------------*/
/* Top-level SPICE file read routine		*/
/*----------------------------------------------*/

char *ReadSpice(char *fname, int *fnum)
{
  struct cellstack *CellStack = NULL;
  int filenum;

  if ((filenum = OpenParseFile(fname)) < 0) {
    char name[100];

    SetExtension(name, fname, SPICE_EXTENSION);
    if ((filenum = OpenParseFile(name)) < 0) {
      Fprintf(stderr,"No file: %s\n",name);
      *fnum = filenum;
      return NULL;
    }    
  }

  /* Make sure all SPICE file reading is case insensitive */
  matchfunc = matchnocase;
  matchintfunc = matchfilenocase;
  hashfunc = hashnocase;

  /* All spice files should start with a comment line,	*/
  /* but we won't depend upon it.  Any comment line	*/
  /* will be handled by the main SPICE file processing.	*/

  ReadSpiceFile(fname, filenum, &CellStack);
  CloseParseFile();

  // Cleanup
  while (CellStack != NULL) PopStack(&CellStack);

  // Important:  If the file is a library, containing subcircuit
  // definitions but no components, then it needs to be registered
  // as an empty cell.  Otherwise, the filename is lost and cells
  // cannot be matched to the file!

  if (LookupCellFile(fname, filenum) == NULL) CellDefNoCase(fname, filenum);

  // Make sure CurrentCell is clear
  CurrentCell = NULL;

  *fnum = filenum;
  return fname;
}

/*--------------------------------------*/
/* SPICE file include routine		*/
/*--------------------------------------*/

void IncludeSpice(char *fname, int parent, struct cellstack **CellStackPtr)
{
  int filenum = -1;
  char name[256];

  /* If fname does not begin with "/", then assume that it is	*/
  /* in the same relative path as its parent.			*/
  
  if (fname[0] != '/') {
     char *ppath;
     if (*CellStackPtr && ((*CellStackPtr)->cellname != NULL)) {
	strcpy(name, (*CellStackPtr)->cellname);
	ppath = strrchr(name, '/');
	if (ppath != NULL)
           strcpy(ppath + 1, fname);
	else
           strcpy(name, fname);
        filenum = OpenParseFile(name);
     }
  }

  /* If we failed the path relative to the parent, then try the	*/
  /* filename alone (relative to the path where netgen was	*/
  /* executed).							*/

  if (filenum < 0) {
     if ((filenum = OpenParseFile(fname)) < 0) {

	/* If that fails, see if a standard SPICE extension	*/
	/* helps.  But really, we're getting desperate at this	*/
	/* point.						*/

        SetExtension(name, fname, SPICE_EXTENSION);
        if ((filenum = OpenParseFile(name)) < 0) {
           Fprintf(stderr,"No file: %s\n",name);
           return;
        }    
     }
  }
  ReadSpiceFile(fname, parent, CellStackPtr);
  CloseParseFile();
}

/*--------------------------------------*/
/*--------------------------------------*/

void EsacapSubCell(struct nlist *tp, int IsSubCell)
{
  struct objlist *ob;
  int node, maxnode;

  /* check to see that all children have been dumped */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      struct nlist *tp2;

      tp2 = LookupCellFile(ob->model, tp->file);
      if ((tp2 != NULL) && !(tp2->dumped) && (tp2->class == CLASS_SUBCKT)) 
	EsacapSubCell(tp2, 1);
    }
  }

  /* print preface, if it is a subcell */
  if (IsSubCell) {
    FlushString("# %s doesn't know how to generate ESACAP subcells\n");
    FlushString("# Look in spice.c \n\n");
    FlushString(".SUBCKT %s ",tp->name);
    for (ob = tp->cell; ob != NULL; ob = ob->next) 
      if (IsPortInPortlist(ob, tp)) FlushString("%d ", ob->node);
    FlushString("# End of bogus ESACAP subcell\n");
    FlushString("\n");
  }

  /* print names of all nodes, prefixed by comment character */
  maxnode = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->node > maxnode) maxnode = ob->node;

/* was: for (node = 0; node <= maxnode; node++) */
  for (node = 1; node <= maxnode; node++) 
    FlushString("# %3d = %s\n", node, NodeName(tp, node));


  /* traverse list of objects */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      int drain_node, gate_node, source_node;
      /* print out element, but special-case transistors */
      if (match (ob->model, "n") || matchnocase(ob->model, "p")) {
	FlushString("X%s ",ob->instance);
	/* note: this code is dependent on the order defined in Initialize()*/
	gate_node = ob->node;
	ob = ob->next;
	drain_node = ob->node;
	ob = ob->next;
	source_node = ob->node;
	FlushString("(%d %d %d ",drain_node, gate_node, source_node);
	/* write fake substrate connections: NSUB and PSUB */
	/* write fake transistor sizes: NL, NW, PL and PW */
	/* write fake transistor classes: NCHANNEL and PCHANNEL  */
	if (matchnocase(ob->model, "n")) 
	  FlushString("NSUB)=SMOS(TYPE=NCHANNEL,W=NW,L=NL);\n");
	else FlushString("PSUB)=SMOS(TYPE=PCHANNEL,W=PW,L=PL);\n");
      }
      else {
	/* it must be a subckt */
	FlushString("### BOGUS SUBCKT: X%s %d ", ob->instance, ob->node);
	while (ob->next != NULL && ob->next->type > FIRSTPIN) {
	  ob = ob->next;
	  FlushString("%d ",ob->node);
	}
	FlushString("X%s\n", ob->model);
      }
    }
  }
	
  if (IsSubCell) FlushString(".ENDS\n");
  tp->dumped = 1;
}

/*--------------------------------------*/
/*--------------------------------------*/

void EsacapCell(char *name, char *filename)
{
  struct nlist *tp;
  char FileName[500];

  tp = LookupCellFile(name, -1);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  if (filename == NULL || strlen(filename) == 0) 
    SetExtension(FileName, name, ESACAP_EXTENSION);
  else 
    SetExtension(FileName, filename, ESACAP_EXTENSION);

  if (!OpenFile(FileName, 80)) {
    perror("ext(): Unable to open output file.");
    return;
  }
  ClearDumpedList();
  /* all Esacap decks begin with the following comment line */
  FlushString("# ESACAP deck for cell %s written by Netgen %s.%s\n\n", 
	      name, NETGEN_VERSION, NETGEN_REVISION);
  EsacapSubCell(tp, 0);
  FlushString("# end of ESACAP deck written by Netgen %s.%s\n\n",
		NETGEN_VERSION, NETGEN_REVISION);
  CloseFile(FileName);
}

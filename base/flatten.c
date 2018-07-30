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


/*  flatten.c -- flatten hierarchical netlists, either totally, or
                 just particular classes of subcells
*/		 

#include "config.h"

#include <stdio.h>

#ifdef IBMPC
#include <alloc.h>
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "print.h"

#define OLDPREFIX 1

void flattenCell(char *name, int file)
{
  struct objlist *ParentParams;
  struct objlist *NextObj;
  struct objlist *ChildObjList;
  struct nlist *ThisCell;
  struct nlist *ChildCell;
  struct objlist *tmp, *ob2, *ob3;
  int	notdone, rnodenum;
  char	tmpstr[200];
  int	nextnode, oldmax;
#if !OLDPREFIX
  int     prefixlength;
#endif

  if (Debug) 
    Printf("Flattening cell: %s\n", name);
  if (file == -1)
     ThisCell = LookupCell(name);
  else
     ThisCell = LookupCellFile(name, file);
  if (ThisCell == NULL) {
    Printf("No cell %s found.\n", name);
    return;
  }
  FreeNodeNames(ThisCell);

  ParentParams = ThisCell->cell;
  nextnode = 0;
  for (tmp = ParentParams; tmp != NULL; tmp = tmp->next) 
    if (tmp->node >= nextnode) nextnode = tmp->node + 1;

  notdone = 1;
  while (notdone) {
    notdone = 0;
    for (ParentParams = ThisCell->cell; ParentParams != NULL;
	 ParentParams = NextObj) {
      if (Debug) Printf("Parent = %s, type = %d\n",
			ParentParams->name, ParentParams->type);
      NextObj = ParentParams->next;
      if (ParentParams->type != FIRSTPIN) continue;
      ChildCell = LookupCellFile(ParentParams->model, ThisCell->file);
      if (Debug) Printf(" Flattening instance: %s, primitive = %s\n",
			ParentParams->name, (ChildCell->class == CLASS_SUBCKT) ?
			"no" : "yes");
      if (ChildCell->class != CLASS_SUBCKT) continue;
      if (ChildCell == ThisCell) continue;	// Avoid infinite loop

      /* not primitive, so need to flatten this instance */
      notdone = 1;
      /* if this is a new instance, flatten it */
      if (ChildCell->dumped == 0) flattenCell(ParentParams->model, ChildCell->file);

      ChildObjList = CopyObjList(ChildCell->cell);

      /* update node numbers in child to unique numbers */
      oldmax = 0;
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (tmp->node > oldmax) oldmax = tmp->node;
      if (nextnode <= oldmax) nextnode = oldmax + 1;

      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (tmp->node <= oldmax && tmp->node != -1) {
	  UpdateNodeNumbers(ChildObjList, tmp->node, nextnode);
	  nextnode ++;
	}

      /* copy nodenumbers of ports from parent */
      ob2 = ParentParams;
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (IsPort(tmp->type)) {
	  if (tmp->node != -1) {
	    if (Debug) 
	      Printf("  Sealing port: %d to node %d\n", tmp->node, ob2->node);
	    UpdateNodeNumbers(ChildObjList, tmp->node, ob2->node);
	  }

	/* in pathological cases, the lengths of the port lists may
           change.  This is an error, but that is no reason to allow
           the code to core dump.  We avoid this by placing a 
           superfluous check on ob2->type
        */

	  if (ob2 != NULL)
	    ob2 = ob2->next;
	}
    

      /* delete all port elements from child */
      while (IsPort(ChildObjList->type)) {
	/* delete all ports at beginning of list */
	if (Debug) Printf("deleting leading port from child\n");
	tmp = ChildObjList->next;
	FreeObjectAndHash(ChildObjList, ChildCell);
	ChildObjList = tmp;
      }
      tmp = ChildObjList;
      while (tmp->next != NULL) {
	if (IsPort((tmp->next)->type)) {
	  ob2 = (tmp->next)->next;
	  if (Debug) Printf("deleting a port from child\n");
	  FreeObjectAndHash(tmp->next, ChildCell);
	  tmp->next = ob2;
	} 
	else tmp = tmp->next;
      }

      /* for each element in child, prepend 'prefix' */
#if !OLDPREFIX
      /* replaces all the sprintf's below */
      strcpy(tmpstr,ParentParams->instance);
      strcat(tmpstr,SEPARATOR);
      prefixlength = strlen(tmpstr);
#endif
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) {
	if (tmp->type == PROPERTY) continue;
	else if (IsGlobal(tmp->type)) {
	   /* Keep the name but search for node of same name in parent	*/
	   /* and replace the node number, if found.			*/

	   for (ob2 = ThisCell->cell; ob2 != NULL; ob2 = ob2->next) {
	      if (ob2->type == tmp->type) {
	         if ((*matchfunc)(tmp->name, ob2->name)) {
		    if (ob2->node >= 0) {
		       // Replace all child objects with this node number
		       rnodenum = tmp->node;
		       for (ob3 = ChildObjList; ob3 != NULL; ob3 = ob3->next) {
			  if (ob3->node == rnodenum)
			     ob3->node = ob2->node;
		       }
		       break;
		    }
		 }
	      }
	   }
#ifdef HASH_OBJECTS
	   HashPtrInstall(tmp->name, tmp, ThisCell->objtab, OBJHASHSIZE);
#endif
	   continue;
	}

#if OLDPREFIX	
	sprintf(tmpstr, "%s%s%s", ParentParams->instance, SEPARATOR,
		tmp->name);
#else
	strcpy(tmpstr+prefixlength,tmp->name);
#endif
	if (Debug) Printf("Renaming %s to %s\n", tmp->name, tmpstr);
	FreeString(tmp->name);
	tmp->name = strsave(tmpstr);
#if OLDPREFIX
	sprintf(tmpstr, "%s%s%s", ParentParams->instance, SEPARATOR,
		tmp->instance);
#else
	strcpy(tmpstr+prefixlength,tmp->instance);
#endif
	FreeString(tmp->instance);
	tmp->instance = strsave(tmpstr);
#ifdef HASH_OBJECTS
	HashPtrInstall(tmp->name, tmp, ThisCell->objtab, OBJHASHSIZE);
	if (tmp->type == FIRSTPIN) 
	  HashPtrInstall(tmp->instance, tmp, ThisCell->insttab, OBJHASHSIZE);
#endif
      }

      /* splice instance out of parent */
      if (ParentParams == ThisCell->cell) {
	/* ParentParams are the very first thing in the list */
	ThisCell->cell = ChildObjList;
	for (ob2 = ChildObjList; ob2->next != NULL; ob2 = ob2->next) ;
      }
      else {
	/* find ParentParams in ThisCell list */
	for (ob2 = ThisCell->cell; ob2->next != ParentParams; ob2=ob2->next); 
	for (ob2->next = ChildObjList; ob2->next != NULL; ob2 = ob2->next) ;
      }
      /* now, ob2 is last element in child list, so skip and reclaim parent */
      tmp = ParentParams;
      do {
	tmp = tmp->next;
      } while ((tmp != NULL) && (tmp->type > FIRSTPIN));
      ob2->next = tmp;
      while (ParentParams != tmp) {
	ob2 = ParentParams->next;

	/* ParentParams are PORTS */
	FreeObjectAndHash(ParentParams, ThisCell);
	ParentParams = ob2;
      }
      NextObj = ParentParams;
    }				/* repeat until no more instances found */
  }
  CacheNodeNames(ThisCell);
  ThisCell->dumped = 1;		/* indicate cell has been flattened */
}

/*--------------------------------------------------------------*/
/* flattenInstancesOf --					*/
/*								*/
/* Causes all instances of 'instance'  within cell 'name' to be	*/
/* flattened.  For the purpose of on-the-fly flattening of .ext	*/
/* files as they are read in, "name" can be NULL, in which case	*/
/* CurrentCell (global variable) is taken as the parent.	*/
/*								*/
/* NOTE:  do not flatten 'instance' itself !! 			*/
/*--------------------------------------------------------------*/

void flattenInstancesOf(char *name, int fnum, char *instance)
{
  struct objlist *ParentParams;
  struct objlist *NextObj;
  struct objlist *ChildObjList;
  struct nlist *ThisCell;
  struct  nlist *ChildCell;
  struct objlist *tmp, *ob2, *ob3;
  int	notdone, rnodenum;
  char	tmpstr[200];
  int	nextnode, oldmax;
#if !OLDPREFIX
  int     prefixlength;
#endif

  if (name == NULL) {
    if (CurrentCell == NULL) {
      Printf("Error: no current cell.\n");
      return;
    }
    else
      ThisCell = CurrentCell;
  }
  else {
    if (Debug) 
       Printf("Flattening instances of %s within cell: %s\n", instance, name);
    if (fnum == -1)
       ThisCell = LookupCell(name);
    else
       ThisCell = LookupCellFile(name, fnum);
    if (ThisCell == NULL) {
      Printf("No cell %s found.\n", name);
      return;
    }
  }
  FreeNodeNames(ThisCell);

  ParentParams = ThisCell->cell;
  nextnode = 0;
  for (tmp = ParentParams; tmp != NULL; tmp = tmp->next) 
    if (tmp->node >= nextnode) nextnode = tmp->node + 1;

  notdone = 1;
  while (notdone) {
    notdone = 0;
    ParentParams = ThisCell->cell;
    for (ParentParams = ThisCell->cell; ParentParams != NULL;
	 ParentParams = NextObj) {
      if (Debug) Printf("Parent = %s, type = %d\n",
			ParentParams->name, ParentParams->type);
      NextObj = ParentParams->next;
      if (ParentParams->type != FIRSTPIN) continue;
      if (!(*matchfunc)(ParentParams->model, instance)) continue;

      ChildCell = LookupCellFile(ParentParams->model, ThisCell->file);
      if (Debug)
	 Printf(" Flattening instance: %s, primitive = %s\n",
			ParentParams->instance, (ChildCell->class == 
			CLASS_SUBCKT) ? "no" : "yes");
      if (ChildCell->class != CLASS_SUBCKT) continue;
      if (ChildCell == ThisCell) continue;	// Avoid infinite loop

      /* not primitive, so need to flatten this instance */
      notdone = 1;
      /* if this is a new instance, flatten it */
      /* if (ChildCell->dumped == 0) flattenCell(ParentParams->model, file); */

      ChildObjList = CopyObjList(ChildCell->cell);

      /* update node numbers in child to unique numbers */
      oldmax = 0;
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (tmp->node > oldmax) oldmax = tmp->node;
      if (nextnode <= oldmax) nextnode = oldmax + 1;

      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (tmp->node <= oldmax && tmp->node > 0) {
	  if (Debug) Printf("Update node %d --> %d\n", tmp->node, nextnode);
	  UpdateNodeNumbers(ChildObjList, tmp->node, nextnode);
	  nextnode++;
	}

      /* copy nodenumbers of ports from parent */
      ob2 = ParentParams;
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (IsPort(tmp->type)) {
	  if (tmp->node > 0) {
	    if (ob2->node == -1) {

	       // Before commiting to attaching to a unconnected node, see
	       // if there is another node in ParentParams with the same
	       // name and a valid node number.  If so, connect them.  In
	       // the broader case, it may be necessary to consider all
	       // nodes, not just those with node == -1, and call join()
	       // here to update all node numbers in the parent cell.
	       // In that case, a more efficient method is needed for
	       // tracking same-name ports.

	       for (ob3 = ParentParams; ob3 && ob3->type >= FIRSTPIN; ob3 = ob3->next) {
		   if (ob3 == ob2) continue;
		   if ((*matchfunc)(ob3->name, ob2->name) && ob3->node != -1) {
		       ob2->node = ob3->node;
		       break;
		   }
	       }
	    }
	    if (Debug) {
	       // Printf("  Sealing port: %d to node %d\n", tmp->node, ob2->node);
	       Printf("Update node %d --> %d\n", tmp->node, ob2->node);
	    }
	    UpdateNodeNumbers(ChildObjList, tmp->node, ob2->node);
	  }

	/* in pathological cases, the lengths of the port lists may
           change.  This is an error, but that is no reason to allow
           the code to core dump.  We avoid this by placing a 
           superfluous check on ob2->type
        */

	  if (ob2 != NULL)
	    ob2 = ob2->next;
	}

      /* Using name == NULL to indicate that a .ext file is being */
      /* flattened on the fly.  This is quick & dirty.		  */

      if (name != NULL) {
        /* delete all port elements from child */
        while ((ChildObjList != NULL) && IsPort(ChildObjList->type)) {
	  /* delete all ports at beginning of list */
	  if (Debug) Printf("deleting leading port from child\n");
	  tmp = ChildObjList->next;
	  FreeObjectAndHash(ChildObjList, ChildCell);
	  ChildObjList = tmp;
        }
        tmp = ChildObjList;
        while (tmp && (tmp->next != NULL)) {
	  if (IsPort((tmp->next)->type)) {
	    ob2 = (tmp->next)->next;
	    if (Debug) Printf("deleting a port from child\n");
	    FreeObjectAndHash(tmp->next, ChildCell);
	    tmp->next = ob2;
	  } 
	  else tmp = tmp->next;
        }
      }

      /* for each element in child, prepend 'prefix' */
#if !OLDPREFIX
      /* replaces all the sprintf's below */
      strcpy(tmpstr,ParentParams->instance);
      strcat(tmpstr,SEPARATOR);
      prefixlength = strlen(tmpstr);
#endif
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) {
	if (tmp->type == PROPERTY) continue;
	else if (IsGlobal(tmp->type)) {
	   /* Keep the name but search for node of same name in parent	*/
	   /* and replace the node number, if found.			*/

	   for (ob2 = ThisCell->cell; ob2 != NULL; ob2 = ob2->next) {
	      /* Type in parent may be a port, not a global */
	      if (ob2->type == tmp->type || ob2->type == PORT) {
	         if ((*matchfunc)(tmp->name, ob2->name)) {
		    if (ob2->node >= 0) {
		       // Replace all child objects with this node number
		       rnodenum = tmp->node;
		       for (ob3 = ChildObjList; ob3 != NULL; ob3 = ob3->next) {
			  if (ob3->node == rnodenum)
			     ob3->node = ob2->node;
		       }
		       break;
		    }
		 }
	      }
	   }
#ifdef HASH_OBJECTS
	   // Don't hash this if the parent had a port of this name
	   if (!ob2 || ob2->type != PORT)
	      HashPtrInstall(tmp->name, tmp, ThisCell->objtab, OBJHASHSIZE);
#endif
	   continue;
	}

#if OLDPREFIX	
	sprintf(tmpstr, "%s%s%s", ParentParams->instance, SEPARATOR,
		tmp->name);
#else
	strcpy(tmpstr+prefixlength,tmp->name);
#endif
	if (Debug) Printf("Renaming %s to %s\n", tmp->name, tmpstr);
	FreeString(tmp->name);
	tmp->name = strsave(tmpstr);
#if OLDPREFIX
	sprintf(tmpstr, "%s%s%s", ParentParams->instance, SEPARATOR,
		tmp->instance);
#else
	strcpy(tmpstr+prefixlength,tmp->instance);
#endif
	FreeString(tmp->instance);
	tmp->instance = strsave(tmpstr);
#ifdef HASH_OBJECTS
	HashPtrInstall(tmp->name, tmp, ThisCell->objtab, OBJHASHSIZE);
	if (tmp->type == FIRSTPIN) 
	  HashPtrInstall(tmp->instance, tmp, ThisCell->insttab, OBJHASHSIZE);
#endif
      }

      /* splice instance out of parent */
      if (ParentParams == ThisCell->cell) {
	/* ParentParams are the very first thing in the list */
	ThisCell->cell = ChildObjList;
	for (ob2 = ChildObjList; ob2 && ob2->next != NULL; ob2 = ob2->next) ;
      }
      else {
	/* find ParentParams in ThisCell list */
	for (ob2 = ThisCell->cell; ob2 && ob2->next != ParentParams; ob2=ob2->next); 
	if (ob2)
	   for (ob2->next = ChildObjList; ob2->next != NULL; ob2 = ob2->next) ;
      }
      /* now, ob2 is last element in child list, so skip and reclaim parent */
      tmp = ParentParams;
      do {
	tmp = tmp->next;
      } while ((tmp != NULL) && (tmp->type > FIRSTPIN));
      if (ob2) ob2->next = tmp;
      while (ParentParams != tmp) {
	ob2 = ParentParams->next;
	FreeObjectAndHash(ParentParams, ThisCell);
	ParentParams = ob2;
      }
      NextObj = ParentParams;
    }				/* repeat until no more instances found */
  }
  CacheNodeNames(ThisCell);
  ThisCell->dumped = 1;		/* indicate cell has been flattened */
}


void Flatten(char *name, int file)
{
	ClearDumpedList(); /* keep track of flattened cells */
	flattenCell(name, file);
}


static char *model_to_flatten;

int flattenoneentry(struct hashlist *p, int file)
{
   struct nlist *ptr;

   ptr = (struct nlist *)(p->ptr);
   if (file == ptr->file)
      if (!(*matchfunc)(ptr->name, model_to_flatten) && (ptr->class == CLASS_SUBCKT))
	 flattenInstancesOf(ptr->name, file, model_to_flatten);
   return(1);
}


void FlattenInstancesOf(char *model, int file)
{
   ClearDumpedList(); /* keep track of flattened cells */
   model_to_flatten = strsave(model);
   RecurseCellFileHashTable(flattenoneentry, file);
   FREE(model_to_flatten);
}

/*
 *-----------------------------------------------------------
 * convertGlobalsOf ---
 *
 *   Called once for each cell that instantiates one or
 *   more cells "model_to_flatten" (global variable).  A
 *   global variable has just been added to the front of
 *   the cell's master pin list.  Get the global variable
 *   name.  Find it in the parent cell or else create it
 *   if it does not exist.  Add the new node to the pin
 *   list for each instance call.
 *   
 *-----------------------------------------------------------
 */

void convertGlobalsOf(char *name, int fnum, char *instance)
{
   struct objlist *ParentParams;
   struct objlist *ChildOb, *Ob2, *Ob;
   struct objlist *newpin, *newnode, *snode, *lnode;
   struct nlist *ThisCell;
   struct nlist *ChildCell;
   int maxnode, maxpin;

   if (name == NULL) {
      if (CurrentCell == NULL) {
	 Printf("Error: no current cell.\n");
	 return;
      }
      else
	 ThisCell = CurrentCell;
   }
   else {
      if (fnum == -1)
         ThisCell = LookupCell(name);
      else
         ThisCell = LookupCellFile(name, fnum);
      if (ThisCell == NULL) {
	 Printf("No cell %s found.\n", name);
	 return;
      }
   }

   FreeNodeNames(ThisCell);

   for (ParentParams = ThisCell->cell; ParentParams != NULL;
		ParentParams = ParentParams->next) {
      if (ParentParams->type != FIRSTPIN) continue;
      if (!(*matchfunc)(ParentParams->model, instance)) continue;

      // Move forward to last pin in the pin list.  The "type" record
      // holds the pin numbering, so we want to find the maximum pin
      // number and keep going from there.

      maxpin = 0;
      while (ParentParams->next != NULL) {
	 if (ParentParams->type >= maxpin) maxpin = ParentParams->type + 1;
	 if (ParentParams->next->type < FIRSTPIN) break;
	 else if (!(*matchfunc)(ParentParams->instance, ParentParams->next->instance))
	    break;
	 ParentParams = ParentParams->next;
      }
      if (ParentParams->type >= maxpin) maxpin = ParentParams->type + 1;

      ChildCell = LookupCellFile(ParentParams->model, ThisCell->file);
      ChildOb = ChildCell->cell;

      // The node to make local will be the last pin in the child
      while (IsPort(ChildOb->type) && ChildOb->next != NULL
		&& IsPort(ChildOb->next->type))
	 ChildOb = ChildOb->next;

      newpin = GetObject();
      if (newpin == NULL) return;	/* Memory allocation error */

      newpin->next = ParentParams->next;
      ParentParams->next = newpin;
      newpin->instance = (ParentParams->instance) ?
		strsave(ParentParams->instance) : NULL;
      newpin->name = (char *)MALLOC(strlen(newpin->instance) +
		strlen(ChildOb->name) + 2);
      sprintf(newpin->name, "%s/%s", newpin->instance, ChildOb->name);
      newpin->model = strsave(ParentParams->model);
      newpin->type = maxpin;
      newpin->node = 0;		/* placeholder */

      // Find the next valid unused node number

      maxnode = -1;
      for (Ob2 = ThisCell->cell; Ob2 != NULL; Ob2 = Ob2->next)
	 if (Ob2->node >= maxnode) maxnode = Ob2->node + 1;

      // Does the global node exist in the parent?  Note that
      // the node may have been declared as a port in the parent,
      // which is fine;  we just don't create a new node in the
      // parent for it.

      for (Ob2 = ThisCell->cell; Ob2 != NULL; Ob2 = Ob2->next) {
	 if (IsGlobal(Ob2->type) || IsPort(Ob2->type))
	    if ((*matchfunc)(Ob2->name, ChildOb->name)) {
	       // This node may never have been used in the parent.  If
	       // so, give it a valid node number in the parent.
	       if (Ob2->node == -1) Ob2->node = maxnode;
	       newpin->node = Ob2->node;
	       break;
	    }
      }
      if (Ob2 == NULL) {	// No such node;  create it
	 newnode = GetObject();

	 // Place the node after the pin list of the parent cell.
	 lnode = NULL;
	 for (snode = ThisCell->cell; snode && IsPort(snode->type);
		snode = snode->next)
	    lnode = snode;
	 if (lnode == NULL) {
	    newnode->next = ThisCell->cell;
	    ThisCell->cell = newnode;
	 }
	 else {
	    newnode->next = lnode->next;
	    lnode->next = newnode;
	 }
	 newnode->type = GLOBAL;
	 newnode->node = maxnode;
         newnode->name = (ChildOb->name) ? strsave(ChildOb->name) : NULL;
         newnode->instance = (ParentParams->instance) ?
		strsave(ParentParams->instance) : NULL;
         newnode->model = strsave(ParentParams->model);
	 newpin->node = maxnode;
#ifdef HASH_OBJECTS
	 HashPtrInstall(newnode->name, newnode, ThisCell->objtab, OBJHASHSIZE);
#endif
      }

      // Remove any references to the net as a GLOBAL type in the instance

      /*
      Ob2 = ParentParams;
      for (Ob = ParentParams->next; Ob != NULL && Ob->type != FIRSTPIN;) {
	 if (IsGlobal(Ob->type)) {
	    Ob2->next = Ob->next;
	    FreeObjectAndHash(Ob, ThisCell);
	    Ob = Ob2->next;
	 }
	 else {
	    Ob2 = Ob;
	    Ob = Ob->next;
	 }
      }
      */

      // Now there should be only one object of this name in the instance,
      // which is the pin, and we will set the hash table to point to it.

#ifdef HASH_OBJECTS
      HashPtrInstall(newpin->name, newpin, ThisCell->objtab, OBJHASHSIZE);
#endif

   }
   CacheNodeNames(ThisCell);
}

/*
 *-----------------------------------------------------------
 * convertglobals ---
 *
 *  Routine to search database for cells that instantiate
 *  cell "model_to_flatten".  For each cell, call the routine
 *  convertGlobalsOf().  Do not call convertGlobalsOf() on
 *  self, and only call convertGlobalsOf() on cells in the
 *  same file.
 *
 *-----------------------------------------------------------
 */

int convertglobals(struct hashlist *p, int file)
{
   struct nlist *ptr;

   ptr = (struct nlist *)(p->ptr);
   if (file == ptr->file)
      if (!(*matchfunc)(ptr->name, model_to_flatten))
         convertGlobalsOf(ptr->name, file, model_to_flatten);
   return 1;
}

/*
 *-----------------------------------------------------------
 * ConvertGlobals ---
 *
 *   Remove global node references in a subcircuit by changing
 *   them to local nodes and adding a port.  Check all parent
 *   cells, adding the global node if it does not exist, and
 *   connecting it to the port of the instance.
 *
 *-----------------------------------------------------------
 */

void ConvertGlobals(char *name, int filenum)
{
   struct nlist *ThisCell;
   struct objlist *ObjList, *Ob2, *NewObj;
   int globalnet, result;

   if (Debug)
      Printf("Converting globals in cell: %s\n", name);

   if (filenum == -1)
      ThisCell = LookupCell(name);	// Good luck with that.
   else
      ThisCell = LookupCellFile(name, filenum);

   if (ThisCell == NULL) {
      Printf("No cell %s found.\n", name);
      return;
   }

   /* Remove the cached node names, because we are changing them */
   FreeNodeNames(ThisCell);

   /* First check if this object has any ports.  If not, it is a top-	*/
   /* level cell, and we do not need to process global nodes.		*/

   for (ObjList = ThisCell->cell; ObjList != NULL; ObjList = ObjList->next) {
      if (IsPort(ObjList->type))
	 break;
      else
	 return;
   }

   for (ObjList = ThisCell->cell; ObjList != NULL; ObjList = ObjList->next) {
      if (IsGlobal(ObjList->type)) {
	 globalnet = ObjList->node;

	 /* Make sure this node is not in the port list already */
	 for (Ob2 = ThisCell->cell; Ob2 != NULL; Ob2 = Ob2->next) {
	    if (Ob2->type != PORT) break;
	    if (Ob2->node == globalnet) break;
	 }
	 if (Ob2 != NULL && IsPort(Ob2->type) && Ob2->node == globalnet)
	    continue;

	 /* Add this node to the cell as a port */
	 NewObj = GetObject();
	 if (NewObj == NULL) return;	/* Memory allocation error */

	 /* Find the last port and add the new net to the end */
	 for (Ob2 = ThisCell->cell; Ob2 != NULL; Ob2 = Ob2->next)
	    if (IsPort(Ob2->type) && (Ob2->next == NULL || !IsPort(Ob2->next->type)))
	       break;

	 if (Ob2 == NULL) {
	    NewObj->next = ThisCell->cell;
	    ThisCell->cell = NewObj;
	 }
	 else {
	    NewObj->next = Ob2->next;
	    Ob2->next = NewObj;
	 }
	 NewObj->type = PORT;
	 NewObj->node = globalnet;
	 NewObj->model = (ObjList->model) ? strsave(ObjList->model) : NULL;
	 NewObj->instance = (ObjList->instance) ? strsave(ObjList->instance) : NULL;
	 NewObj->name = (ObjList->name) ? strsave(ObjList->name) : NULL;

#ifdef HASH_OBJECTS
	 HashPtrInstall(NewObj->name, NewObj, ThisCell->objtab, OBJHASHSIZE);
#endif

	 /* Find all parent cells of this cell.  Find the global node	*/
	 /* if it exists or create it if it doesn't.  Add the node to	*/
	 /* the beginning of the list of pins for this device.		*/

	 ClearDumpedList(); /* keep track of flattened cells */
	 model_to_flatten = strsave(name);
	 RecurseCellFileHashTable(convertglobals, filenum);
	 FREE(model_to_flatten);
      }
   }

   /* Now remove all global nodes from the cell.		*/
   /* Do not remove the hash entry, because we still have a 	*/
   /* node (a pin) of the same name, and have reassigned the	*/
   /* hash table value to it.					*/

   Ob2 = NULL;
   for (ObjList = ThisCell->cell; ObjList != NULL;) {
      if (IsGlobal(ObjList->type)) {
	 if (Ob2 == NULL)
	    ThisCell->cell = ObjList->next;
	 else
	    Ob2->next = ObjList->next;

	 FreeObject(ObjList);	/* not FreeObjectAndHash(), see above */

	 if (Ob2 == NULL)
	    ObjList = ThisCell->cell;
	 else
	    ObjList = Ob2->next;
      }
      else {
         Ob2 = ObjList;
         ObjList = ObjList->next;
      }
   }

   /* Regenerate the node name cache */
   CacheNodeNames(ThisCell);
}

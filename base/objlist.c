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

/* objlist.c  --  manipulating lists of elements */

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#ifdef IBMPC
#include <alloc.h>
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "regexp.h"
#include "dbug.h"
#include "print.h"
#include "netfile.h"

#ifdef TCL_NETGEN
extern Tcl_Interp *netgeninterp;
#endif

struct nlist *CurrentCell = NULL;
#define USE_TAIL_POINTER
struct objlist *CurrentTail = NULL;

/* shortcut pointer to last-placed object in list; used in netgen.c */
struct objlist *LastPlaced = NULL;

/* used to narrow list of cells to those belonging to a specific file */
int TopFile = -1;

/* global variable to enable/disable translation of UNIX wildcards */
int UnixWildcards = 1;
/* define the following to minimize use of regcomp/regexp stuff */
#define OPTIMIZE_WILDCARDS

/***************************************************************************/
/*                                                                         */
/*         NETGEN garbage collection / monitoring stuff                    */
/*                                                                         */
/***************************************************************************/


#define GARBAGESIZE 100
/* list of allocated nodes awaiting garbage collection */
static struct objlist *garbage[GARBAGESIZE]; 
static int nextfree;
static int ObjectsAllocated = 0;
#ifdef DEBUG_GARBAGE
static int StringsAllocated = 0;
#endif

void GarbageCollect()
{
}

void InitGarbageCollection()
{
	int i;
	
	for (i=0; i < GARBAGESIZE; i++)
		garbage[i] = NULL;
	nextfree = 0;
}

void ThrowOutGarbage(int i)
{
	struct objlist *tp, *tpnext;
	
	tp = garbage[i];
	while (tp != NULL) {
		tpnext = tp->next;
		FREE(tp);
		ObjectsAllocated--;
		tp = tpnext;
	}
#ifdef DEBUG_GARBAGE
	Printf("ThrowOutGarbage: objects left = %d\n",ObjectsAllocated);
#endif
	garbage[i] = NULL;
}


void AddToGarbageList(struct objlist *head)
{
	if (garbage[nextfree] != NULL) ThrowOutGarbage(nextfree);
	garbage[nextfree] = head;
	nextfree = (nextfree + 1) % GARBAGESIZE;
}

#ifdef DEBUG_GARBAGE
/* otherwise, inline these functions with macros */

struct keyvalue *NewKeyValue(void)
{
	struct keyvalue *kv;
	
	kv = (struct keyvalue *)CALLOC(1,sizeof(struct keyvalue));
	if (kv == NULL) Fprintf(stderr,"NewKeyValue: Core allocation error\n");
	return (kv);
}

struct keylist *NewProperty(void)
{
	struct keylist *kl;
	
	kl = (struct keylist *)CALLOC(1,sizeof(struct keylist));
	if (kl == NULL) Fprintf(stderr,"NewProperty: Core allocation error\n");
	return (kl);
}

struct valuelist *NewPropValue(void)
{
	struct valuelist *vl;
	
	vl = (struct valuelist *)CALLOC(1,sizeof(struct valuelist));
	if (vl == NULL) Fprintf(stderr,"NewPropValue: Core allocation error\n");
	return (vl);
}

struct objlist *GetObject(void)
{
	struct objlist *tp;
	
#ifdef IBMPC
	Printf("GetObject(): num = %d; mem left = %ld\n",
		++ObjectsAllocated, (long)coreleft());
#endif
	tp = (struct objlist *)CALLOC(1,sizeof(struct objlist));
	if (tp == NULL) Fprintf(stderr,"GetObject: Core allocation error\n");
	return (tp);
}

void FreeString(char *foo)
{
	Printf("Freeing string: %s, number = %d\n", foo, --StringsAllocated);
	FREE(foo);
}

char *strsave(char *s)
{
	char *p;
	
#ifdef DEBUG_GARBAGE
	Printf("\tstrsave(%s), num = %d\n",s,++StringsAllocated);
#endif
	if ((p = (char *)MALLOC(strlen(s)+1)) == NULL)
		Fprintf(stderr,"strsave: core allocation failure\n");
	else strcpy(p, s);
	return (p);
}
#endif /* DEBUG_GARBAGE */

/* Case-sensitive matching */

int match(char *st1, char *st2)
{
	if (0==strcmp(st1,st2)) return(1);
	else return(0);
}

/* Case-insensitive matching */

int matchnocase(char *st1, char *st2)
{
   char *sp1 = st1;
   char *sp2 = st2;

   while (*sp1 != '\0' && *sp2 != '\0') {
      if (tolower(*sp1) != tolower(*sp2)) break;
      sp1++;
      sp2++;
   }
   if ((*sp1 != '\0') || (*sp2 != '\0')) return 0;
   return 1;
}

/* Case-sensitive matching with file matching */

int matchfile(char *st1, char *st2, int f1, int f2)
{
    if (f1 != f2) return 0;
    else if (strcmp(st1,st2)) return(0);
    else return(1);
}

/* Case-insensitive matching with file matching */

int matchfilenocase(char *st1, char *st2, int f1, int f2)
{
   char *sp1 = st1;
   char *sp2 = st2;

   if (f1 != f2) return 0;
   while (*sp1 != '\0' && *sp2 != '\0') {
      if (tolower(*sp1) != tolower(*sp2)) break;
      sp1++;
      sp2++;
   }
   if ((*sp1 != '\0') || (*sp2 != '\0')) return 0;
   return 1;
}

#ifdef HAVE_MALLINFO
void PrintMemoryStats(void)
{
  struct mallinfo minfo;

  /* HPUX 7.0 defines mallinfo(void); use mallinfo(0) for HPUX 6.5 */
  minfo = mallinfo();

  Printf("total space = %d\n", minfo.arena);
  Printf("small blocks    = %5d, space = %7d, free space = %7d\n", 
	 minfo.smblks, minfo.usmblks, minfo.fsmblks);
  Printf("ordinary blocks = %5d, space = %7d, free space = %7d\n", 
	 minfo.ordblks, minfo.uordblks, minfo.fordblks);
  Printf("holding blocks  = %5d, size of header = %d\n", 
	 minfo.hblks, minfo.hblkhd);
}
#endif


/**************************************************************************

  NETGEN cell hash table

**************************************************************************/


#define CELLHASHSIZE 1000
static struct hashlist *cell_hashtab[CELLHASHSIZE]; 

void InitCellHashTable(void)
{
	hashfunc = hash;
	matchfunc = match;
	InitializeHashTable(cell_hashtab, CELLHASHSIZE);
}

struct nlist *LookupCell(char *s)
{
  return((struct nlist *)HashLookup(s, cell_hashtab, CELLHASHSIZE));
}

/* Similar hash lookup to the above, but will check if the matching	*/
/* record has a file equal to "f".  If not, prepends underscores until	*/
/* either a matching record is found, or there is no hash result.	*/

struct nlist *LookupCellFile(char *s, int f)
{
   struct nlist *he;

   if (f == -1) return LookupCell(s);
   return HashIntLookup(s, f, cell_hashtab, CELLHASHSIZE);
}

struct nlist *InstallInCellHashTable(char *name, int fnum)
{
  struct hashlist *ptr;
  struct nlist *p;

  ptr = LookupCellFile(name, fnum);
  if (ptr != NULL)
    return((struct nlist *)(ptr->ptr));

  /* else, it is not present, so add it to list */
  p = (struct nlist *)CALLOC(1, sizeof(struct nlist));
  if (p == NULL) return(NULL);
  if ((p->name = strsave(name)) == NULL) goto fail;
  p->file = fnum;
#ifdef HASH_OBJECTS
  if ((p->objtab = (struct hashlist **)CALLOC(OBJHASHSIZE, 
	    sizeof(struct hashlist *))) == NULL) goto fail;
  if ((p->insttab = (struct hashlist **)CALLOC(OBJHASHSIZE, 
	    sizeof(struct hashlist *))) == NULL) goto fail;
#endif

  // Hash size 0 indicates to hash function that no binning is being done
  p->classhash = (*hashfunc)(name, 0);

  ptr = HashIntPtrInstall(name, fnum, p, cell_hashtab, CELLHASHSIZE);
  if (ptr == NULL) return(NULL);
  return(p);
 fail:
  if (p->name != NULL) FREE(p->name);
#ifdef HASH_OBJECTS
  if (p->objtab != NULL) {
    HashKill(p->objtab, OBJHASHSIZE);
    FREE(p->objtab);
  }
  if (p->insttab != NULL) {
    HashKill(p->insttab, OBJHASHSIZE);
    FREE(p->insttab);
  }
#endif
  FREE(p);
  return(NULL);
}

/* Rename the cell named "name" to "newname".  Find the hash entry for
 * cell "name", remove it from the hash table, generate a new hash entry
 * for "newname", and set its pointer to the original cell.
 */

void CellRehash(char *name, char *newname, int file)
{
  struct nlist *tp;
  struct hashlist *ptr;

  if (file == -1)
     tp = LookupCell(name);
  else
     tp = LookupCellFile(name, file);

  FREE(tp->name);
  tp->name = strsave(newname);

  ptr = HashIntPtrInstall(newname, file, (void *)tp, cell_hashtab, CELLHASHSIZE);
  if (ptr != NULL)
     HashIntDelete(name, file, cell_hashtab, CELLHASHSIZE);
}

struct nlist *OldCell;

int deleteclass(struct hashlist *p, int file)
{
   struct nlist *ptr;
   struct objlist *ob, *lob, *nob;

   ptr = (struct nlist *)(p->ptr);

   if ((file != -1) && (ptr->file != file)) return;

   lob = NULL;
   for (ob = ptr->cell; ob != NULL;) {
      nob = ob->next;
      if ((ob->type == FIRSTPIN) && (ob->model != NULL)) {
	 if ((*matchfunc)(ob->model, OldCell->name)) {
	    HashDelete(ob->instance, ptr->insttab, OBJHASHSIZE);
	    while (1) {
	       FreeObjectAndHash(ob, ptr);
	       ob = nob;
	       if (ob == NULL) break;
	       nob = ob->next;
	       if (ob->type == FIRSTPIN) break;
	    }
	    if (lob == NULL)
	       ptr->cell = ob;
	    else
	       lob->next = ob;
	 }
	 else {
	    lob = ob;
	    ob = nob;
	 }
      }
      else {
         lob = ob;
         ob = nob;
      }
   }
}

/* Remove all instances of class "class" from the database */

void ClassDelete(char *class, int file)
{
   if (file == -1)
      OldCell = LookupCell(class);
   else
      OldCell = LookupCellFile(class, file);

   if (OldCell == NULL) return;
   RecurseCellFileHashTable(deleteclass, file);
}

/* Find all instances of the cell named "name" in the database, and 	*/
/* change their model and instance information to "newname".		*/

char *NewName;

int renameinstances(struct hashlist *p, int file)
{
   struct nlist *ptr;
   struct objlist *ob, *ob2, *obp;

   ptr = (struct nlist *)(p->ptr);

   if ((file != -1) && (ptr->file != file)) return;

   for (ob = ptr->cell; ob != NULL; ob = ob->next) {
      if ((ob->type >= FIRSTPIN) && (ob->model != NULL)) {
	 if ((*matchfunc)(ob->model, OldCell->name)) {
	    FreeString(ob->model);
	    ob->model = strsave(NewName);
	 }
      }
   }
}

void InstanceRename(char *from, char *to, int file)
{
   if (file == -1)
      OldCell = LookupCell(from);
   else
      OldCell = LookupCellFile(from, file);

   if (OldCell == NULL) return;
   NewName = to;

   RecurseCellFileHashTable(renameinstances, file);
}

void CellDelete(char *name, int fnum)
{
  /* delete all the contents of cell 'name', and remove 'name' from
     the cell hash table.  NOTE:  this procedure does not care or check
     if 'name' has been instanced anywhere.  It is assumed that if this
     is the case, the user will (quickly) define a new cell of that name.
  */
  struct objlist *ob, *obnext;
  struct keylist *pr, *prnext;
  struct nlist *tp;

  tp = LookupCellFile(name, fnum);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  HashIntDelete(name, fnum, cell_hashtab, CELLHASHSIZE);
  /* now make sure that we free all the fields of the nlist struct */
  if (tp->name != NULL) FREE(tp->name);
#ifdef HASH_OBJECTS
  if (tp->objtab != NULL) {
    HashKill(tp->objtab, OBJHASHSIZE);
    FREE(tp->objtab);
  }
  if (tp->insttab != NULL) {
    HashKill(tp->insttab, OBJHASHSIZE);
    FREE(tp->insttab);
  }
#endif
  FreeNodeNames(tp);
  ob = tp->cell;
  while (ob != NULL) {
    obnext = ob->next;
    FreeObject (ob);
    ob = obnext;
  }
  pr = tp->proplist;
  while (pr != NULL) {
    prnext = pr->next;
    FreeString(pr->key);
    FREE(pr);
    pr = prnext;
  }
}

static int PrintCellHashTableElement(struct hashlist *p)
{
  struct nlist *ptr;

  ptr = (struct nlist *)(p->ptr);
  if ((TopFile >= 0) && (ptr->file != TopFile)) return 1;

  if (ptr->class != CLASS_SUBCKT) {
    /* only print primitive cells if Debug is enabled */
    if (Debug == 1)  Printf("Cell: %s (instanced %d times); Primitive\n",
		       ptr->name, ptr->number);
  }
  else if (Debug == 2) {	/* list only */
#ifdef TCL_NETGEN
     Tcl_AppendElement(netgeninterp, ptr->name);
#else
     Printf("%s ", ptr->name);
#endif
  }
  else
    Printf("Cell: %s (instanced %d times)\n",ptr->name,ptr->number);
  return(1);
}

/* Print the contents of the cell hash table.	*/
/* if full == 1, print primitive elements also	*/
/* if full == 2, just output the cell names.	*/

void PrintCellHashTable(int full, char *filename)
{
  int total, bins;
  int OldDebug;

  if (filename != NULL) {
     struct nlist *top;
     top = LookupCell(filename);
     TopFile = (top) ? top->file : (File + 1);
  }
  else TopFile = -1;

  bins  = RecurseHashTable(cell_hashtab, CELLHASHSIZE, CountHashTableBinsUsed);
  total = RecurseHashTable(cell_hashtab, CELLHASHSIZE, CountHashTableEntries);
  if (full != 2)
     Printf("Hash table: %d of %d bins used; %d cells total (%.2f per bin)\n",
		bins, CELLHASHSIZE, total, (bins == 0) ? 0 :
		(float)((float)total / (float)bins));
	
  OldDebug = Debug;
  Debug = full;
  RecurseHashTable(cell_hashtab, CELLHASHSIZE, PrintCellHashTableElement);
  Debug = OldDebug;
#ifndef TCL_NETGEN
  if (full == 2) Printf("\n");
#endif
}

struct nlist *FirstCell(void)
{
  return((struct nlist *)HashFirst(cell_hashtab, CELLHASHSIZE));
}

struct nlist *NextCell(void)
{
  return((struct nlist *)HashNext(cell_hashtab, CELLHASHSIZE));
}

static int ClearDumpedElement(struct hashlist *np)
{
	struct nlist *p;

	p = (struct nlist *)(np->ptr);
	p->dumped = 0;
	return(1);
}

void ClearDumpedList(void)
{
	RecurseHashTable(cell_hashtab, CELLHASHSIZE, ClearDumpedElement);
}

int RecurseCellHashTable(int (*foo)(struct hashlist *np))
{
  return RecurseHashTable(cell_hashtab, CELLHASHSIZE, foo);
}

int RecurseCellFileHashTable(int (*foo)(struct hashlist *, int), int value)
{
  return RecurseHashTableValue(cell_hashtab, CELLHASHSIZE, foo, value);
}

/* Yet another version, passing one parameter that is a pointer */

struct nlist *RecurseCellHashTable2(struct nlist *(*foo)(struct hashlist *,
	void *), void *pointer)
{
  return RecurseHashTablePointer(cell_hashtab, CELLHASHSIZE, foo, pointer);
}

/************************** WILD-CARD STUFF *******************************/
		
char *FixTemplate(char *t)
{
	char buffer[200];
	char *rstr;
	int i,j;
	int InsideBrace;
	
	/* if we do not have to translate wildcards, just return input */
	if (!UnixWildcards) return(strsave(t));
	
	buffer[0] = '^';
	InsideBrace=0;
	for (i=0,j=1; i < strlen(t); i++) 
		switch (t[i]) {
			case '{' : buffer[j++]='('; 
				   InsideBrace++;
				   break;
			case '}' : buffer[j++]=')'; 
				   InsideBrace--;
				   break;
			case ',' : if (InsideBrace) {
			              buffer[j++]='|'; 
#if defined(HAVE_REGCMP) || defined(HAVE_RE_COMP)
				      Fprintf(stderr,
	  "Regular expression package unable to handle ',' operator.\n");
				      Fprintf(stderr,"Template = %s\n", t);
#endif
                                   }
				   else buffer[j++]=',';
			           break;
			case '*' : buffer[j++]='.'; 
			           buffer[j++]='*'; break;
			case '?' : buffer[j++]='.'; break;
			/* escape various characters */
			case '^' : 
			case '.' :
			case '(' :
			case ')' :
			case '+' :
			case '|' :
			case '$' :
				   buffer[j++]='\\'; 
			           buffer[j++]=t[i]; break;
			/* escaped characters */
			case '\\' : buffer[j++]='\\'; 
				    buffer[j++] = t[++i]; break;
			/* subranges */
			case '[' : buffer[j++]='['; 
				   if (t[i+1] == '~') {
					   buffer[j++] = '^';
					   i++;
				   }
				   break;
			case ']' : buffer[j++]=']'; break;
			default :  buffer[j++] = t[i]; break;
		}
	buffer[j++] = '$';  /* end of line delimeter */
	buffer[j] = '\0';   /* null-terminated string */

	/* printf ("Translated template = '%s'\n",buffer); */
	rstr = strsave(buffer);
	return(rstr);
}
			
#ifndef TCL_NETGEN

/*
 *-------------------------------------------------------------------
 * returns a list of objects in CurrentCell whose names match the
 * regular expression in the 'list_template'
 *-------------------------------------------------------------------
 */
	
struct objlist *List(char *list_template)
{
  Regexp RegularExpression;
  struct objlist *head, *tail;
  struct objlist *test, *tmp;
  char *template2;
  int itmp;
	
  if (CurrentCell == NULL) {
    Fprintf(stderr,"No current cell in List()\n");
    return (NULL);
  }
  if (QuickSearch)
    test = LastPlaced;
  else
    test = CurrentCell->cell;

  head = NULL;
  tail = NULL;

#ifdef OPTIMIZE_WILDCARDS
  if (strpbrk(list_template,"*?[{") == NULL && UnixWildcards) {

    /* just find element, forget about regular expressions */
    test = LookupObject(list_template, CurrentCell);
    if (test != NULL) {
      /* insert it directly into list (list has only this element) */
      head = GetObject();
      memcpy(head, test, sizeof(struct objlist));
      head->next = NULL;
      AddToGarbageList(head);
      return(head);
    }
  }
  /* otherwise, need to deal with wildcards */

#endif /* OPTIMIZE_WILDCARDS */

  template2 = FixTemplate(list_template);
  DBUG_PRINT("regex",("Compiling regular expression: %s => %s",
		      list_template, template2));
  RegularExpression = RegexpCompile(template2);
  DBUG_PRINT("regex",("   Result = %ld",(long)RegularExpression));
  FreeString(template2);
  for ( ; test != NULL; test = test->next) {
    itmp = RegexpMatch(RegularExpression,test->name);
    DBUG_PRINT("regex",("Testing string %s, result = %d", test->name, itmp));
    if (itmp) {
      tmp = GetObject();
      memcpy(tmp, test, sizeof(struct objlist));
      tmp->next = NULL;

      /* now insert it into list */

      if (head == NULL) {
	head = tmp;
	tail = tmp;
      }
      else {			/* append it to list 'head' */
	tail->next = tmp;
	tail = tmp;
      }
    }
  }

#ifdef REGEXP_FREE_TEMPLATE
  FREE(RegularExpression);
#endif

  AddToGarbageList(head);
  return(head);
}


#else

/*
 *-------------------------------------------------------------------
 * Version of the above (List) with wildcards disabled.
 *
 * Returns (a copy of) the object in CurrentCell whose name
 * matches the 'obj_name'.  For Tcl, we use Tcl's built-in
 * regexp function to do template matching.
 *-------------------------------------------------------------------
 */
	
struct objlist *List(char *obj_name)
{
  struct objlist *head, *test;
	
  if (CurrentCell == NULL) {
    Fprintf(stderr,"No current cell in List()\n");
    return (NULL);
  }
  if (QuickSearch)
    test = LastPlaced;
  else
    test = CurrentCell->cell;

  head = NULL;

  test = LookupObject(obj_name, CurrentCell);
  if (test != NULL) {
    /* insert it directly into list (list has only this element) */
    head = GetObject();
    memcpy(head, test, sizeof(struct objlist));
    head->next = NULL;
  }
  AddToGarbageList(head);
  return(head);
}

#endif	/* TCL_NETGEN */

/****************** LIST MANIPULATION STUFF *****************************/

struct objlist *ListCat(struct objlist *ls1, struct objlist *ls2)
{
	struct objlist *head, *tail, *tmp;
	
	head = NULL;
	tail = NULL;
	if (ls1 == NULL) {
		ls1 = ls2;
		ls2 = NULL;
	}
	while (ls1 != NULL) {
		tmp = GetObject();
#if 1
		memcpy(tmp, ls1, sizeof(struct objlist));
		tmp->next = NULL;
#else
		tmp->name = ls1->name;
		tmp->type = ls1->type;
		tmp->model = ls1->model;
		tmp->instance = ls1->instance;
		tmp->node = ls1->node;
		tmp->next = NULL;
#endif
		/* now insert it into list */
		if (head == NULL) {
			head = tmp;
			tail = head;
		}
		else {  /* append it to list 'head' */
			tail->next = tmp;
			tail = tmp;
		}
		if (ls1->next == NULL) {
			ls1 = ls2;
			ls2 = NULL;
		}
		else ls1 = ls1->next;
	}
	AddToGarbageList(head);
	return(head);
}
	
int ListLen(struct objlist *head)
{
	int n;
	
	n = 0;
	while (head != NULL) {
		head = head->next;
		n++;
	}
	return (n);
}

int ListLength(char *list_template)
{
	struct objlist *head;
	int n;
	
	head = List(list_template);
	n = ListLen(head);
	return (n);
}



struct objlist *CopyObjList(struct objlist *oldlist)
/* copies list pointed to by oldlist, creating
   a list whose head pointer is returned */
{
  struct objlist *head, *tail, *tmp, *newob;

  tmp = oldlist;
  head = NULL;
  tail = NULL;
  while (tmp != NULL) {
    if ((newob = GetObject()) == NULL) {
      Fprintf(stderr,"CopyObjList: core allocation failure\n");
#ifdef HAVE_MALLINFO
      PrintMemoryStats();
#endif
      return(NULL);
    }
    newob->name = (tmp->name) ? strsave(tmp->name) : NULL;
    newob->type = tmp->type;
    if (newob->type == PROPERTY)
       CopyProperties(newob, tmp);
    else {
       if (tmp->model == NULL || IsPort(tmp->type))
          newob->model = NULL;
       else
          newob->model = strsave(tmp->model);
       newob->instance = (tmp->instance) ? strsave(tmp->instance) : NULL;
    }
    newob->node = tmp->node;
    newob->next = NULL;
    if (head == NULL) 
      head = newob;
    else 
      tail->next = newob;
    tail = newob;
    tmp = tmp->next;
  }
  return (head);
}


/********************* LIST SEARCHING STUFF ******************************/

/*--------------------------------------------------------------*/
/* Search for exact match of object 'name' in cell 'WhichCell'	*/
/* This search excludes object properties.			*/
/*--------------------------------------------------------------*/

struct objlist *LookupObject(char *name, struct nlist *WhichCell)
{
#ifdef HASH_OBJECTS
  return((struct objlist *)HashLookup(name, WhichCell->objtab, OBJHASHSIZE));
#else		
	struct objlist *tp;
	
	if (QuickSearch && CurrentCell == WhichCell) {
	  tp = LastPlaced;
	  while (tp != NULL) {
		if (match(name, tp->name)) return (tp);
		tp = tp->next;
	      }
	}
	/* if not QuickSearch, or QuickSearch not successful */
	tp = WhichCell->cell;
	while (tp != NULL) {
		if (match(name, tp->name)) return (tp);
		tp = tp->next;
	}
	return (NULL);
#endif
}

struct objlist *LookupInstance(char *name, struct nlist *WhichCell)
/* searches for exact match of instance 'name' in cell 'WhichCell' */
{
#ifdef HASH_OBJECTS
  return((struct objlist *)HashLookup(name, WhichCell->insttab, OBJHASHSIZE));
#else		
	struct objlist *tp;
	
	if (QuickSearch && CurrentCell == WhichCell) {
	  tp = LastPlaced;
	  while (tp != NULL) {
		if (match(name, tp->instance)) return (tp);
		tp = tp->next;
	      }
	}
	/* if not QuickSearch, or QuickSearch not successful */
	tp = WhichCell->cell;
	while (tp != NULL) {
		if (match(name, tp->instance)) return (tp);
		tp = tp->next;
	}
	return (NULL);
#endif
}


void UpdateNodeNumbers(struct objlist *lst, int from, int to)
{
	while (lst != NULL) {
		if (lst->node == from) 
			lst->node = to;
		lst = lst->next;
	}
}


void AddToCurrentCell(struct objlist *ob)
{
  if (CurrentCell == NULL) {
    Fprintf(stderr,"No current cell for ");
    switch (ob->type) {
    case PORT: Fprintf(stderr,"Port(%s)\n",ob->name); break;
    case PROPERTY: Fprintf(stderr,"Property\n"); break;
    case GLOBAL: Fprintf(stderr,"Global(%s)\n",ob->name); break;
    case UNIQUEGLOBAL: Fprintf(stderr,"UniqueGlobal(%s)\n",ob->name); break;
    default:  Fprintf(stderr,"pin: %s\n",ob->name); break;
    }
    return;
  }

#ifdef USE_TAIL_POINTER
  if (CurrentCell->cell == NULL) CurrentCell->cell = ob;
  else CurrentTail->next = ob;
  CurrentTail = ob;
  ob->next = NULL;
#else
  if (CurrentCell->cell == NULL) CurrentCell->cell = ob;
  else {
    struct objlist *ob2;
      
    ob2 = CurrentCell->cell;
    while (ob2->next != NULL) ob2 = ob2->next;
    ob2->next = ob;
  }
  ob->next = NULL;
#endif

#ifdef HASH_OBJECTS
  /* add to object hash table for this cell */
  HashPtrInstall(ob->name, ob, CurrentCell->objtab, OBJHASHSIZE);
#endif
}

void AddInstanceToCurrentCell(struct objlist *ob)
{
#ifdef HASH_OBJECTS
  /* add to instance hash table for this cell */
  HashPtrInstall(ob->instance, ob, CurrentCell->insttab, OBJHASHSIZE);
#endif
}

void FreeObject(struct objlist *ob)
{
  /* This just frees the object record.  Beware of pointer left	*/
  /* in the objtab hash table.  Hash table records should be	*/
  /* removed first.						*/

  if (ob->name != NULL) FreeString(ob->name);

  if (ob->type == PROPERTY) {
     /* Property records are different in that they use "instance" and	*/
     /* "model" to save pointers to linked lists.			*/
     if (ob->model != NULL) {
	struct keyvalue *kv, *kvnext;
	kv = (struct keyvalue *)ob->model;
	while (kv != NULL) {
	   kvnext = kv->next;
	   FreeString(kv->key);
	   FreeString(kv->value);
	   FREE(kv);
	   kv = kvnext;
	}
     }
     if (ob->instance != NULL) {
	struct valuelist *vl, *vlnext;
	vl = (struct valuelist *)ob->instance;
	while (vl != NULL) {
	   vlnext = vl->next;
	   FREE(vl);
	   vl = vlnext;
	}
     }
  }
  else {
     /* All other records */
     if (ob->model != NULL) FreeString(ob->model);
     if (ob->instance != NULL) FreeString(ob->instance);
  }
  FREE(ob);
}


void FreeObjectAndHash(struct objlist *ob, struct nlist *ptr)
{
#ifdef HASH_OBJECTS
   HashDelete(ob->name, ptr->objtab, OBJHASHSIZE);

#endif	// HASH_OBJECTS

   FreeObject(ob);
}

/***************  GENERAL UTILITIES ****************************/

int NumberOfPorts(char *cellname)
{
  struct nlist *tp;
  struct objlist *ob;
  int ports;

  tp = LookupCell(cellname);
  if (tp == NULL) return(0);
  ports = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (IsPort(ob->type)) ports++;
  return(ports);
}
  
void FreePorts(char *cellname)
{
  struct nlist *tp;
  struct objlist *ob, *obnext, *oblast;

  tp = LookupCell(cellname);
  if (tp == NULL) return;
  ob = tp->cell;
  if (ob == NULL) return;
  tp->cell = NULL;

  while (ob && IsPort(ob->type)) {
    obnext = ob->next;
    if (ob->model != NULL) FreeString(ob->model);
    if (ob->instance != NULL) FreeString(ob->instance);
    FREE(ob);
    ob = obnext;
  }
  tp->cell = ob;

  oblast = ob;
  while (ob) {
    obnext = ob->next;
    if (IsPort(ob->type)) {
       if (ob->model != NULL) FreeString(ob->model);
       if (ob->instance != NULL) FreeString(ob->instance);
       FREE(ob);
       oblast->next = obnext;
    }
    else
       oblast = ob;
    ob = obnext;
  }
}


struct objlist *InstanceNumber(struct nlist *tp, int inst)
{
  struct objlist *ob;
  int count;

  count = 1;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      if (count == inst) return(ob);
      count ++;
    }
  }
  return(NULL);
}



/**********************************************************************/
/*               NodeName cacheing stuff                              */
/**********************************************************************/



static char *OldNodeName(struct nlist *tp, int node)
{
  /* return a legitimate node name for node number 'node' in cell *tp */
  /* Order of preference:
     1) Named ports of cells
     2) Named internal nodes of cells
     3) Unique global ports (these names are descriptive, but long)
     4) Global ports
     5) Pins on instances
     */

  struct objlist *ob;
  struct objlist *firstport;
  struct objlist *firstnode;
  struct objlist *firstuniqueglobal;
  struct objlist *firstglobal;
  struct objlist *firstpin;
  static char StrBuffer[100];

#if 0
  /* make second pass, looking for ports */
  ob = tp->cell;
  while (ob != NULL) {
    if ((node == ob->node) && IsPort(ob->type)) {
      strcpy(StrBuffer, ob->name);
      return(StrBuffer);
    }
    ob = ob->next;
  }

  /* make third pass, looking for named internal nodes */
  ob = tp->cell;
  while (ob != NULL) {
    if ((node == ob->node) && (ob->type == NODE)) {
      strcpy(StrBuffer, ob->name);
      return(StrBuffer);
    }
    ob = ob->next;
  }

  /* make zeroth pass, looking for unique global ports */
  ob = tp->cell;
  while (ob != NULL) {
    if ((node == ob->node) && (ob->type == UNIQUEGLOBAL)) {
      strcpy(StrBuffer, ob->name);
      return(StrBuffer);
    }
    ob = ob->next;
  }

  /* make first pass, looking for global ports */
  ob = tp->cell;
  while (ob != NULL) {
    if ((node == ob->node) && (ob->type == GLOBAL)) {
      strcpy(StrBuffer, ob->name);
      return(StrBuffer);
    }
    ob = ob->next;
  }

  /* make fourth pass, looking for first pin */
  ob = tp->cell;
  while (ob != NULL) {
    if (node == ob->node) {
      strcpy(StrBuffer, ob->name);
      return(StrBuffer);
    }
    ob = ob->next;
  }
#endif

  firstport = NULL;
  firstnode = NULL;
  firstuniqueglobal = NULL;
  firstglobal = NULL;
  firstpin = NULL;
  if (node < 1) {
    sprintf(StrBuffer, "Disconnected(%d)",node);
    return(StrBuffer);
  }
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (node == ob->node) {
      if (ob->type >= FIRSTPIN) firstpin = ob;
      else if (IsPort(ob->type)) {
	firstport = ob;
	strcpy(StrBuffer,ob->name);
	return(StrBuffer);
      }
      else if (ob->type == NODE) firstnode = ob;
      else if (ob->type == UNIQUEGLOBAL) firstuniqueglobal = ob;
      else if (ob->type == GLOBAL) firstglobal = ob;
      else 
	Fprintf(stderr,"??? ob->type = %d on %s\n",ob->type,ob->name);
    }
  }

  if (firstport != NULL) ob = firstport;
  else if (firstnode != NULL) ob = firstnode;
  else if (firstuniqueglobal != NULL) ob = firstuniqueglobal;
  else if (firstglobal != NULL) ob = firstglobal;
  else if (firstpin != NULL) ob = firstpin;
  else {
    /* if we got to here, we have a serious problem */
    Fprintf (stderr, "NodeName(%d) called with bogus parameter\n", node);
    sprintf(StrBuffer, "bogus(%d)",node);
    return(StrBuffer);
  }
  strcpy(StrBuffer,ob->name);
  return(StrBuffer);
}


char *NodeName(struct nlist *tp, int node)
{
  if (node == -1) return("Disconnected");
#ifdef CACHE_NODENAMES
  if (tp->nodename_cache != NULL) {
    if (node > tp->nodename_cache_maxnodenum || 
	tp->nodename_cache[node] == NULL)
      return ("IllegalNode");
    else
      return (tp->nodename_cache[node]->name);
  }
#endif
  return (OldNodeName(tp, node));
}

char *NodeAlias(struct nlist *tp, struct objlist *ob)
/* return the best name for 'ob'; safer than calling NodeName,
   as it correctly handles disconnected nodes */
{
  if (ob == NULL) return("NULL");
  if (ob->node == -1) {
/*    Fprintf(stderr,"Disconnected node in NodeAlias: %s\n",ob->name); */
    return(ob->name);
  }
#ifdef CACHE_NODENAMES
  if ((ob->node >= 0) && (tp->nodename_cache != NULL) &&
		(ob->node <= tp->nodename_cache_maxnodenum))
    return (tp->nodename_cache[ob->node]->name);
#endif
  return (OldNodeName(tp, ob->node));
}

void FreeNodeNames(struct nlist *tp)
{
#ifdef CACHE_NODENAMES
  if (tp == NULL) return;
  if (tp->nodename_cache != NULL)
    FREE(tp->nodename_cache);
  tp->nodename_cache = NULL;
  tp->nodename_cache_maxnodenum = 0;
#endif
}

void CacheNodeNames(struct nlist *tp)
{
#ifdef CACHE_NODENAMES
  int nodes;
  struct objlist *ob;

  if (tp == NULL) return;
  if (tp->nodename_cache != NULL) FreeNodeNames(tp);
  nodes = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->node > nodes) nodes = ob->node;

  if (nodes == 0) return;

  tp->nodename_cache = 
    (struct objlist **)CALLOC(nodes+1, sizeof(*(tp->nodename_cache)));
  if (tp->nodename_cache == NULL) return;
  tp->nodename_cache_maxnodenum = nodes;

  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    int present, new_type;

    if (ob->node < 0) continue;  /* do not cache it */
    if ((tp->nodename_cache)[ob->node] == NULL) 
      (tp->nodename_cache)[ob->node] = ob;

    present = ((tp->nodename_cache)[ob->node])->type;
    new_type = ob->type;

    if (new_type == present) continue;
    if (new_type >= FIRSTPIN && present >= FIRSTPIN) continue;

    switch (new_type) {
    case PORT: 
      (tp->nodename_cache)[ob->node] = ob;
      break;
    case NODE:
      if (present != PORT)
	(tp->nodename_cache)[ob->node] = ob;
      break;
    case UNIQUEGLOBAL:
      if (present != PORT && present != NODE)
	(tp->nodename_cache)[ob->node] = ob;
      break;
    case GLOBAL:
      if (present != PORT && present != NODE && present != UNIQUEGLOBAL)
	(tp->nodename_cache)[ob->node] = ob;
      break;
    default:   /* we have a pin or property, which we can ignore */
      break;
    }
  }
#endif
}


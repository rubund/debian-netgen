/*  objlist.h -- core allocation, list generation by regexps */

#ifndef _OBJLIST_H
#define _OBJLIST_H

#define SEPARATOR "/"
#define INSTANCE_DELIMITER "#"
#define PORT_DELIMITER "."
#define PHYSICALPIN "("
#define ENDPHYSICALPIN ")"

#define PORT (-1)
#define GLOBAL (-2)
#define UNIQUEGLOBAL (-3)
#define PROPERTY (-4)		/* For element properties; e.g., length, width */
#define ALLELEMENTS (-5)	/* for doing searches; e.g., Fanout() */
#define ALLOBJECTS (-6)		/* for doing searches; e.g., Fanout() */
#define NODE 0
#define FIRSTPIN 1
#define IsPort(a) (a == PORT)
#define IsGlobal(a) ((a == GLOBAL) || (a == UNIQUEGLOBAL))

/* list of objects within a cell */
struct objlist {
  char *name;		/* unique name for the port/node/pin/property */
  int type;		/* -1 for port,  0 for internal node,
			   else index of the pin on element */
  char *model;		/* name of element class; nullstr for ports/nodes/properties */
  char *instance;	/* unique name for the instance, or */
			/* (string) value of property for properties */
  int node;		/* the electrical node number of the port/node/pin */
  struct objlist *next;
};

/* Linked list for saving property key:value pairs	*/
/* when reading them from a netlist file.		*/

struct keyvalue {
  char *key;
  char *value;
  struct keyvalue *next;
};

/* Lists of device properties.  Order is defined at the time of	*/
/* the cell definition; values are sorted at the time instances	*/
/* are read.							*/

/* Part 1:  Values (corresponding to the keys, and kept in the instance record) */

struct valuelist {
  union {
     char *string;
     double dval;
     int ival;
  } value;
  struct valuelist *next;	/* link to next property value */
};

/* Part 2a: Define the types of property values */

#define PROP_STRING	0
#define PROP_DOUBLE	1
#define PROP_INTEGER	2

/* Part 2b:  Keys (kept in the cell record) */

struct keylist {
  unsigned char type;		/* string, integer, or double */
  char *key;			/* name of the property */
  union {
     double dval;
     int ival;
  } slop;			/* slop allowance in property */
  struct keylist *next;		/* link to next property key */
};

extern struct objlist *LastPlaced; 

/* Record structure for maintaining lists of cell classes to ignore */

struct IgnoreList {
   char *class;
   int file;
   struct IgnoreList *next;
};

#if defined(IBMPC)  || defined(HASH_OBJECTS)
/* disable these for memory-efficiency reasons on PC, unless -DHASH_OBJECTS */

/* #define HASH_OBJECTS */
/* #define OBJHASHSIZE 101 */
/* #define CACHE_NODENAMES */
#else /* not IBMPC */

/* turn this on to speed up object searches */
#define HASH_OBJECTS
#define OBJHASHSIZE 997 /* the size of the object and instance hash lists */
                        /* prime numbers are good choices as hash sizes */
                        /* 101 is a good number for IBMPC */

/* turn this on to cache NodeName targets into array */
#define CACHE_NODENAMES
#endif /* not IBMPC */

/* cell definition for hash table */
/* NOTE: "file" must come first for the hash matching by name and file */

struct nlist {
  int file;		/* internally ordered file to which cell belongs, or -1 */
  char *name;
  int number;		/* number of instances defined */
  int dumped;		/* instance dumped (usually used as a flag) */
  unsigned char flags;
  unsigned char class;
  unsigned long classhash;	/* randomized hash value for cell class */
  struct keylist *proplist;
  struct objlist *cell;
#ifdef HASH_OBJECTS
  struct hashlist **objtab;  /* hash table of object names */
  struct hashlist **insttab; /* hash table of instance names */
#endif
#ifdef CACHE_NODENAMES
  struct objlist **nodename_cache;
  long nodename_cache_maxnodenum;  /* largest node number in cache */
#endif
  void *embedding;   /* this will be cast to the appropriate data structure */
  struct nlist *next;
};

/* Defined nlist structure flags */

#define CELL_MATCHED	0x1	/* cell matched to another */
#define CELL_NOCASE	0x2	/* cell is case-insensitive (e.g., SPICE) */

extern struct nlist *CurrentCell;
extern struct objlist *CurrentTail;
extern void AddToCurrentCell(struct objlist *ob);
extern void AddInstanceToCurrentCell(struct objlist *ob);
extern void FreeObject(struct objlist *ob);
extern void FreeObjectAndHash(struct objlist *ob, struct nlist *ptr);
extern void FreePorts(char *cellname);
extern struct IgnoreList *ClassIgnore;

extern int NumberOfPorts(char *cellname);
extern struct objlist *InstanceNumber(struct nlist *tp, int inst);

extern struct objlist *List(char *list_template);
extern struct objlist *ListExact(char *list_template);
extern struct objlist *ListCat(struct objlist *ls1, struct objlist *ls2);
extern int ListLen(struct objlist *head);
extern int ListLength(char *list_template);
extern struct nlist *LookupPrematchedClass(struct nlist *, int);
extern struct objlist *LookupObject(char *name, struct nlist *WhichCell);
extern struct objlist *LookupInstance(char *name, struct nlist *WhichCell);
extern struct objlist *CopyObjList(struct objlist *oldlist);
extern void UpdateNodeNumbers(struct objlist *lst, int from, int to);

/* Function pointer to List or ListExact, allowing regular expressions	*/
/* to be enabled/disabled.						*/

extern struct objlist * (*ListPtr)();

extern void PrintCellHashTable(int full, char *filename);
extern struct nlist *LookupCell(char *s);
extern struct nlist *LookupCellFile(char *s, int f);
extern struct nlist *InstallInCellHashTable(char *name, int f);
extern void InitCellHashTable(void);
extern void ClearDumpedList(void);
extern int RecurseCellHashTable(int (*foo)(struct hashlist *np));
extern int RecurseCellFileHashTable(int (*foo)(struct hashlist *, int), int);
extern struct nlist *RecurseCellHashTable2(struct nlist
	 *(*foo)(struct hashlist *, void *), void *);
extern struct nlist *FirstCell(void);
extern struct nlist *NextCell(void);

extern char *NodeName(struct nlist *tp, int node);
extern char *NodeAlias(struct nlist *tp, struct objlist *ob);
extern void FreeNodeNames(struct nlist *tp);
extern void CacheNodeNames(struct nlist *tp);


/* enable the following line to debug the core allocator */
/* #define DEBUG_GARBAGE */
   
#ifdef DEBUG_GARBAGE
extern struct objlist *GetObject(void);
extern struct keyvalue *NewKeyValue(void);
extern struct keylist *NewProperty(void);
extern struct valuelist *NewPropValue(void);
extern void FreeString(char *foo);
extern char *strsave(char *s);
#else /* not DEBUG_GARBAGE */
#define GetObject() ((struct objlist*)CALLOC(1,sizeof(struct objlist)))
#define NewProperty() ((struct keylist*)CALLOC(1,sizeof(struct keylist)))
#define NewPropValue() ((struct valuelist*)CALLOC(1,sizeof(struct valuelist)))
#define NewKeyValue() ((struct keyvalue*)CALLOC(1,sizeof(struct keyvalue)))
#define FreeString(a) (FREE(a))
#define strsave(a) (STRDUP(a))
#endif /* not DEBUG_GARBAGE */

extern int  match(char *, char *);
extern int  matchnocase(char *, char *);
extern int  matchfile(char *, char *, int, int);
extern int  matchfilenocase(char *, char *, int, int);

extern void GarbageCollect(void);
extern void InitGarbageCollection(void);
extern void AddToGarbageList(struct objlist *head);

#ifdef HAVE_MALLINFO
void PrintMemoryStats(void);
#endif

#endif  /* _OBJLIST_H */




#ifndef _QUERY_H
#define _QUERY_H

/* typeahead and prompt routines */
extern void typeahead(char *str);
extern void promptstring(char *prompt, char *buf);
extern FILE *promptstring_infile;


/* various printing routines */
#ifdef TCL_NETGEN
extern void PrintAllElements(char *cell);
extern void ElementNodes(char *cell, char *element, int fnum);
#else
extern void PrintElement(char *cell, char *list_template);
#endif
extern void Fanout(char *cell, char *node, int filter);
extern void PrintNodes(char *name);
extern void PrintCell(char *name);
extern void PrintInstances(char *name);
extern void DescribeInstance(char *name, int file);
extern void PrintPortsInCell(char *cellname);
extern void PrintLeavesInCell(char *cellname);
extern void PrintAllLeaves(void);

extern int ChangeScopeCurrent(char *pattern, int typefrom, int typeto);
extern int ChangeScope(int fnum, char *cellname, char *pattern,
		int typefrom, int typeto);

#endif /* _QUERY_H */

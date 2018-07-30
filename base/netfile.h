#ifndef _NETFILE_H
#define _NETFILE_H

#define NTK_EXTENSION ".ntk"
#define ACTEL_EXTENSION ".adl"
#define XILINX_EXTENSION ".xnf"
#define WOMBAT_EXTENSION ".wom"
#define EXT_EXTENSION ".ext"
#define SIM_EXTENSION ".sim"
#define SPICE_EXTENSION ".spice"
#define SPICE_EXT2 ".spc"
#define SPICE_EXT3 ".fspc"
#define NETGEN_EXTENSION ".ntg"
#define CCODE_EXTENSION ".c.code"
#define ESACAP_EXTENSION ".esa"

#if 0
/* since these are defined in netgen.h, include that instead !!! */

/* output file formats; define these in netgen.h as well */
extern void Ntk(char *name, char *filename);
extern void Actel(char *name, char *filename);
extern void Wombat(char *name, char *filename);
extern void Ext(char *name);
extern void Sim(char *name);
extern void SpiceCell(char *name, int fnum, char *filename);
extern void EsacapCell(char *name, char *filename);
extern void WriteNetgenFile(char *name, char *filename);
extern void Ccode(char *name, char *filename);

/* input file formats */
extern char *ReadNtk (char *fname);
extern char *ReadExtHier(char *fname);
extern char *ReadExtFlat(char *fname);
extern char *ReadSim(char *fname);
extern char *ReadSpice(char *fname);
extern char *ReadNetgenFile (char *fname);
#endif

#define LINELENGTH 80

extern int OpenFile(char *filename, int linelen);
extern void CloseFile(char *filename);
extern int IsPortInPortlist(struct objlist *ob, struct nlist *tp);
extern void FlushString (char *format, ...);
extern char *SetExtension(char *buffer, char *path, char *extension);

extern int File;

/* input routines */

extern char *nexttok;
#define SKIPTO(a) do {SkipTok();} while (!match(nexttok,a))
extern void SkipTok(void);
extern void SkipTokNoNewline(void);
extern void SpiceTokNoNewline(void);	/* handles SPICE "+" continuation line */
extern void SkipNewLine(void);
extern void SpiceSkipNewLine(void);	/* handles SPICE "+" continuation line */
extern void InputParseError(FILE *f);
extern int OpenParseFile(char *name);
extern int EndParseFile(void);
extern int CloseParseFile(void);

#endif /* _NETFILE_H */

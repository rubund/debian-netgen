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

/* tclnetgen.c ---  Tcl interpreter interface for using netgen */

#include <stdio.h>
#include <stdlib.h>	/* for getenv */
#include <string.h>

#include <tcl.h>

#include "config.h"
#include "netgen.h"
#include "objlist.h"
#include "netcmp.h"
#include "dbug.h"
#include "print.h"
#include "query.h"	/* for ElementNodes() */

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*-----------------------*/
/* Tcl 8.4 compatibility */
/*-----------------------*/

#ifndef CONST84
#define CONST84
#endif

int UseTkConsole = TRUE;
Tcl_Interp *netgeninterp;
Tcl_Interp *consoleinterp;
int ColumnBase = 0;
char *LogFileName = NULL;

extern int PropertyErrorDetected;

/* Function prototypes for all Tcl command callbacks */

int _netgen_read(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_lib(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_write(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_flatten(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_nodes(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_elements(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_debug(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_protochip(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_instances(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_contents(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_describe(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_cells(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_ports(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_model(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_leaves(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_quit(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_reinit(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_log(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
#ifdef HAVE_MALLINFO
int _netgen_printmem(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
#endif
int _netgen_help(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_matching(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_compare(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_iterate(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_summary(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_print(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_run(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_verify(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_automorphs(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_equate(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_ignore(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_permute(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_exhaustive(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_restart(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_global(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_convert(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);

typedef struct _Cmd {
   char 	*name;
   int		(*handler)();
   char		*helptext;
} Command;

/*------------------------------------------------------*/
/* All netgen commands under Tcl are defined here	*/ 
/*------------------------------------------------------*/
   
Command netgen_cmds[] = {
	{"readnet",		_netgen_read,
		"[<format>] <file>\n   "
		"read a netlist file (default format=auto)"},
	{"readlib",		_netgen_lib,
		"<format> [<file>]\n   "
		"read a format library"},
	{"writenet", 		_netgen_write,
		"<format> <file>\n   "
		"write a netlist file"},
	{"flatten",		_netgen_flatten,
		"[class] <cell>\n   "
		"flatten a hierarchical cell"},
	{"nodes",		_netgen_nodes,
		"[<element>] <cell> <file>\n   "
		"print nodes of an element or cell"},
	{"elements",		_netgen_elements,
		"[<node>] <cell>\n   "
		"print elements of a node or cell"},
	{"debug",		_netgen_debug,
		"on|off|<command>\n   "
		"turn debugging on or off or debug a command"},
	{"protochip",		_netgen_protochip,
		"\n   "
		"embed protochip structure"},
	{"instances",		_netgen_instances,
		"<cell>\n   "
		"list instances of the cell"},
	{"contents",		_netgen_contents,
		"<cell>\n   "
		"list contents of the cell"},
	{"describe",		_netgen_describe,
		"<cell>\n   "
		"describe the cell"},
	{"cells",		_netgen_cells,
		"[list|all] [filename]\n   "
		"print known cells, optionally from filename only\n   "
		"all:  print all cells, including primitives"},
	{"ports",		_netgen_ports,
		"<cell>\n   "
		"print ports of the cell"},
	{"model",		_netgen_model,
		"<name> <class>\n   "
		"equate a model name with a device class"},
	{"leaves",		_netgen_leaves,
		"[<cell>]\n   "
		"print leaves of the cell"},
	{"quit",		_netgen_quit,
		"\n   "
		"exit netgen and Tcl"},
	{"reinitialize",	_netgen_reinit,
		"\n   "
		"reintialize netgen data structures"},
	{"log",			_netgen_log,
		"[file <name>|start|end|reset|suspend|resume|echo]\n   "
		"enable or disable output log to file"},
#ifdef HAVE_MALLINFO
	{"memory",		_netgen_printmem,
		"\n   "
		"print memory statistics"},
#endif
	{"help",		_netgen_help,
		"\n   "
		"print this help information"},
	NULL
};

Command netcmp_cmds[] = {
	{"compare",		_netcmp_compare,
		"<cell1> <cell2>\n   "
		"declare two cells for netcomp netlist comparison"},
	{"global",		_netcmp_global,
		"<cell> <nodename>\n	"
		"declare a node (with possible wildcards) in the\n	"
		"hierarchy of <cell> to be of global scope"},
	{"convert",		_netcmp_convert,
		"<cell>\n	"
		"convert global nodes to local nodes and pins\n		"
		"in cell <cell>"},
	{"iterate",		_netcmp_iterate,
		"\n   "
		"do one netcomp iteration"},
	{"summary",		_netcmp_summary,
		"[elements|nodes]\n   "
		"summarize netcomp internal data structure"},
	{"print",		_netcmp_print,
		"\n   "
		"print netcomp internal data structure"},
	{"run",			_netcmp_run,
		"[converge|resolve]\n   "
		"converge: run netcomp to completion (convergence)\n   "
		"resolve: run to completion and resolve automorphisms"},
	{"verify",		_netcmp_verify,
		"[elements|nodes|only|equivalent|unique]\n   "
		"verify results"},
	{"automorphisms",	_netcmp_automorphs,
		"\n   "
		"print automorphisms"},
	{"equate",		_netcmp_equate,
		"[elements|nodes|classes|pins] <name1> [<file1>] <name2> [<file2>]\n   "
		"elements: equate two elements\n   "
		"nodes: equate two nodes\n  "
		"classes: equate two device classes\n  "
		"pins: match pins between two cells"},

	{"ignore",		_netcmp_ignore,
		"class <name>\n   "
		"class: ignore any instances of class named <name>"},
		
	{"permute",		_netcmp_permute,
		"[transistors|resistors|capacitors|<model>]\n   "
		"<model>: permute named pins on device model\n   "
		"resistor: enable resistor permutations\n   "
		"capacitor: enable capacitor permutations\n   "
		"transistor: enable transistor permutations\n   "
		"(none): enable transistor and resistor permutations"},
	{"exhaustive",		_netcmp_exhaustive,
		"\n   "
		"toggle exhaustive subdivision"},
	{"restart",		_netcmp_restart,
		"\n   "
		"start over (reset data structures)"},
	{"matching",		_netcmp_matching,
		"[element|node] <name1>\n   "
		"return the corresponding node or element name\n   "
		"in the compared cell"},
	NULL
};
 
/*------------------------------------------------------*/
/*------------------------------------------------------*/

/*------------------------------------------------------*/
/* The following code breaks up the Query() command	*/
/* from query.c into individual functions w/arguments	*/
/*------------------------------------------------------*/

/*------------------------------------------------------*/
/* Function name: _netgen_read				*/
/* Syntax: netgen::readnet [format] <filename>		*/
/* Formerly: read r, K, Z, G, and S			*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_read(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *formats[] = {
      "automatic", "ext", "extflat", "sim", "ntk", "spice",
      "netgen", "actel", "xilinx", NULL
   };
   enum FormatIdx {
      AUTO_IDX, EXT_IDX, EXTFLAT_IDX, SIM_IDX, NTK_IDX,
      SPICE_IDX, NETGEN_IDX, ACTEL_IDX, XILINX_IDX
   };
   struct nlist *tc;
   int result, index, filenum;
   char *retstr = NULL, *savstr = NULL;

   if (objc == 1 || objc > 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "?format? file");
      return TCL_ERROR;
   }
   else if (objc > 1) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)formats,
		"format", 0, &index) != TCL_OK) {
	 if (objc == 3)

	    return TCL_ERROR;
	 else {
	    Tcl_ResetResult(interp);
	    index = AUTO_IDX;
	 }
      }
   }

   switch (index) {
      case ACTEL_IDX:
      case XILINX_IDX:
	 if (objc != 2) {
	    Fprintf(stderr, "Warning: argument \"%s\" ignored.  Reading %s library.\n",
		Tcl_GetString(objv[2]), formats[index]);
	 }
	 break;

      case AUTO_IDX:
	 if (objc != 2) {
            Tcl_WrongNumArgs(interp, 1, objv, "file");
            return TCL_ERROR;
	 }
         retstr = Tcl_GetString(objv[1]);
	 break;

      default:
	 if (objc != 3) {
            Tcl_WrongNumArgs(interp, 1, objv, "format file");
            return TCL_ERROR;
	 }
         retstr = Tcl_GetString(objv[2]);
	 break;
   }

   if (retstr) savstr = STRDUP(retstr);

   // Check if the file is already loaded
   tc = LookupCell(savstr);
   if (tc != NULL) {
      filenum = tc->file;
   }
   else {

      filenum = -1;
      switch(index) {
         case AUTO_IDX:
            retstr = ReadNetlist(savstr, &filenum);
            break;
         case EXT_IDX:
            retstr = ReadExtHier(savstr, &filenum);
            break;
         case EXTFLAT_IDX:
            retstr = ReadExtFlat(savstr, &filenum);
            break;
         case SIM_IDX:
            retstr = ReadSim(savstr, &filenum);
            break;
         case NTK_IDX:
            retstr = ReadNtk(savstr, &filenum);
            break;
         case SPICE_IDX:
            retstr = ReadSpice(savstr, &filenum);
            break;
         case NETGEN_IDX:
            retstr = ReadNetgenFile(savstr, &filenum);
            break;
         case ACTEL_IDX:
	    ActelLib();
	    retstr = formats[index];
	    break;
         case XILINX_IDX:
	    XilinxLib();
	    retstr = formats[index];
	    break;
      }
   }

   /* Return the file number to the interpreter */
   Tcl_SetObjResult(interp, Tcl_NewIntObj(filenum));

   if (savstr) FREE(savstr);
   return (retstr == NULL) ? TCL_ERROR : TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_lib				*/
/* Syntax: netgen::readlib <format> [<filename>]	*/
/* Formerly: read X, A					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_lib(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *formats[] = {
      "actel", "spice", "xilinx", NULL
   };
   enum FormatIdx {
      ACTEL_IDX, SPICE_IDX, XILINX_IDX
   };
   int result, index, fnum = -1;
   char *repstr;

   if (objc == 1 || objc > 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "format ?file?");
      return TCL_ERROR;
   }
   if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)formats,
	"format", 0, &index) != TCL_OK) {
      return TCL_ERROR;
   }
   switch(index) {
      case ACTEL_IDX:
      case XILINX_IDX:
	 if (objc == 3) {
	    Tcl_WrongNumArgs(interp, 1, objv, "actel | xilinx");
	    return TCL_ERROR;
	 }
	 break;
      case SPICE_IDX:
	 if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 1, objv, "spice file");
	    return TCL_ERROR;
	 }
	 break;
   }

   switch(index) {
      case ACTEL_IDX:
         ActelLib();
         break;
      case SPICE_IDX:
	 repstr = Tcl_GetString(objv[2]);
         ReadSpice(repstr, &fnum);
         break;
      case XILINX_IDX:
         XilinxLib();
         break;
   }

   Tcl_SetObjResult(interp, Tcl_NewIntObj(fnum));
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_write				*/
/* Syntax: netgen::write format cellname [filenum]	*/
/* Formerly: k, x, z, w, o, g, s, E, and C		*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_write(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *formats[] = {
      "ext", "sim", "ntk", "actel",
      "spice", "wombat", "esacap", "netgen", "ccode", "xilinx", NULL
   };
   enum FormatIdx {
      EXT_IDX, SIM_IDX, NTK_IDX, ACTEL_IDX,
      SPICE_IDX, WOMBAT_IDX, ESACAP_IDX, NETGEN_IDX, CCODE_IDX, XILINX_IDX
   };
   int result, index, filenum;
   char *repstr;

   if (objc != 3 && objc != 4) {
      Tcl_WrongNumArgs(interp, 1, objv, "format file");
      return TCL_ERROR;
   }
   if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)formats,
		"format", 0, &index) != TCL_OK) {
      return TCL_ERROR;
   }
   repstr = Tcl_GetString(objv[2]);

   if (objc == 4) {
      result = Tcl_GetIntFromObj(interp, objv[3], &filenum);
      if (result != TCL_OK) return result;
   }
   else filenum = -1;

   switch(index) {
      case EXT_IDX:
         Ext(repstr, filenum);
         break;
      case SIM_IDX:
         Sim(repstr, filenum);
         break;
      case NTK_IDX:
         Ntk(repstr,"");
         break;
      case ACTEL_IDX:
	 if (ActelLibPresent() == 0) {
	    Fprintf(stderr, "Warning:  Actel library was not loaded.\n");
	    Fprintf(stderr, "Try \"readlib actel\" before reading the netlist.\n");
	 }
         Actel(repstr,"");
         break;
      case SPICE_IDX:
         SpiceCell(repstr, filenum, "");
         break;
      case WOMBAT_IDX:
         Wombat(repstr,NULL);
         break;
      case ESACAP_IDX:
         EsacapCell(repstr,"");
         break;
      case NETGEN_IDX:
         WriteNetgenFile(repstr,"");
         break;
      case CCODE_IDX:
         Ccode(repstr,"");
         break;
      case XILINX_IDX:
	 if (XilinxLibPresent() == 0) {
	    Fprintf(stderr, "Warning:  Xilinx library was not loaded.\n");
	    Fprintf(stderr, "Try \"readlib xilinx\" before reading the netlist.\n");
	 }
         Xilinx(repstr,"");
         break;
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_flatten			*/
/* Syntax: netgen::flatten mode				*/
/* Formerly: f and F					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_flatten(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr, *file;
   int result, llen, filenum;
   Tcl_Obj *tobj;
   struct nlist *tp;

   if ((objc < 2) || (objc > 4)) {
      Tcl_WrongNumArgs(interp, 1, objv, "?class? name [filenum]");
      return TCL_ERROR;
   }

   result = Tcl_GetIntFromObj(interp, objv[objc - 1], &filenum);
   if (result != TCL_OK) {
      Tcl_ResetResult(interp);
      filenum = -1;
   }
   else objc--;

   result = Tcl_ListObjLength(interp, objv[objc - 1], &llen);
   if (result != TCL_OK) return TCL_ERROR;

   // Alternatively to supplying "filenum", allow the use of a
   // 2-item list containing a source filename and a class name,
   // in which case the search for the class name will be limited
   // to cells in the specified file.

   if (llen == 2 && filenum == -1) {
      result = Tcl_ListObjIndex(interp, objv[objc - 1], 0, &tobj);
      if (result != TCL_OK) return TCL_ERROR;
      file = Tcl_GetString(tobj);
      result = Tcl_ListObjIndex(interp, objv[objc - 1], 1, &tobj);
      if (result != TCL_OK) return TCL_ERROR;
      repstr = Tcl_GetString(tobj);
      tp = LookupCell(file);
      if (tp != NULL) {
	 tp = LookupCellFile(repstr, tp->file);
	 repstr = tp->name;
      } else {
	 Tcl_SetResult(interp, "No such file.\n", NULL);
	 return TCL_ERROR;
      }
   } else {
      repstr = Tcl_GetString(objv[objc - 1]);
   }

   if (objc == 3 || objc == 4) {
      char *argv = Tcl_GetString(objv[1]);
      if (!strcmp(argv, "class"))
         FlattenInstancesOf(repstr, filenum);
      else {
	 Tcl_WrongNumArgs(interp, 1, objv, "class name [file]");
	 return TCL_ERROR;
      }
   }
   else
      Flatten(repstr, filenum);
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Function name: _netgen_nodes					*/
/* Syntax: netgen::nodes [-list <element>] [<cell> <file>]	*/
/* Formerly: n and N						*/
/* Results:							*/
/* Side Effects:						*/
/*--------------------------------------------------------------*/

int
_netgen_nodes(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *estr = NULL, *istr = NULL, *cstr, *fstr;
   char *optstart;
   int dolist = 0;
   int fnum, result;
   struct nlist *np;

   if (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 objv++;
	 objc--;
      }
   }

   if ((objc < 1 || objc > 4) || (objc == 2)) {
      Tcl_WrongNumArgs(interp, 1, objv, "?element? ?cell file?");
      return TCL_ERROR;
   }

   if (objc == 1) {
      if (CurrentCell == NULL) {
	 Tcl_WrongNumArgs(interp, 1, objv, "(cell name required)");
	 return TCL_ERROR;
      }
      cstr = CurrentCell->name;
      fnum = CurrentCell->file;
   }
   else {
      cstr = Tcl_GetString(objv[objc - 2]);
      if (objc == 4)
	 estr = Tcl_GetString(objv[1]);

      // If last argument is an integer, it is the file number.  Otherwise,
      // it is a filename and can be used to pick up the file number.

      result = Tcl_GetIntFromObj(interp, objv[objc - 1], &fnum);
      if (result != TCL_OK) {
	 Tcl_ResetResult(interp);
	 fstr = Tcl_GetString(objv[objc - 1]);
	 np = LookupCell(fstr);
	 if (np == NULL) {
	    Tcl_SetResult(interp, "No such file.", NULL);
	    return TCL_ERROR;
	 }
	 fnum = np->file;
      }
   }

   if (estr) {
      if (*estr != '/') {
	 istr = (char *)Tcl_Alloc(strlen(estr) + 2);
	 sprintf(istr, "/%s", estr);
	 estr = istr;
      }
   }

   if (estr) {
      if (dolist) {
	 struct objlist *ob, *nob;
	 Tcl_Obj *lobj, *pobj;
	 int ckto;

	 np = LookupCellFile(cstr, fnum);

	 if (np == NULL) {
	    Tcl_SetResult(interp, "No such cell.", NULL);
	    if (istr) Tcl_Free(istr);
	    return TCL_ERROR;
	 }

	 ckto = strlen(estr);
	 for (ob = np->cell; ob != NULL; ob = ob->next) {
	    if (!strncmp(estr, ob->name, ckto)) {
	       if (*(ob->name + ckto) == '/' || *(ob->name + ckto) == '\0')
		  break;
	    }
	 }
	 if (ob == NULL) {
	    Tcl_SetResult(interp, "No such element.", NULL);
	    if (istr) Tcl_Free(istr);
	    return TCL_ERROR;
	 }
	 lobj = Tcl_NewListObj(0, NULL);
	 for (; ob != NULL; ob = ob->next) {
	    if (!strncmp(estr, ob->name, ckto)) {
	       if (*(ob->name + ckto) != '/' && *(ob->name + ckto) != '\0')
		  continue;

	       pobj = Tcl_NewListObj(0, NULL);
               Tcl_ListObjAppendElement(interp, pobj,
			Tcl_NewStringObj(ob->name + ckto + 1, -1));

	       for (nob = np->cell; nob != NULL; nob = nob->next) {
		  if (nob->node == ob->node) {
		     if (nob->type < FIRSTPIN) {
                        Tcl_ListObjAppendElement(interp, pobj,
				Tcl_NewStringObj(nob->name, -1));
		        break;
		     }
		  }
	       }
               Tcl_ListObjAppendElement(interp, lobj, pobj);
	    }
	 }
	 Tcl_SetObjResult(interp, lobj);
      }
      else
         ElementNodes(cstr, estr, fnum);
   }
   else
      PrintNodes(cstr);
  
   if (istr) Tcl_Free(istr);
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_elements			*/
/* Syntax: netgen::elements [-list <node>] [<cell>]	*/
/* Formerly: e						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_elements(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *nstr = NULL, *cstr;
   struct objlist * (*ListSave)();
   char *optstart;
   int dolist = 0;

   if (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 objv++;
	 objc--;
      }
   }
    
   if (objc < 1 || objc > 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "?node? ?cell?");
      return TCL_ERROR;
   }

   if (objc == 1) {
      if (CurrentCell == NULL) {
	 Tcl_WrongNumArgs(interp, 1, objv, "(cell name required)");
	 return TCL_ERROR;
      }
      cstr = CurrentCell->name;
   }
   else {
      cstr = Tcl_GetString(objv[objc - 1]);
      if (objc == 3)
	 nstr = Tcl_GetString(objv[1]);
   }

   if (nstr) {
      if (dolist) {
	 struct objlist *ob;
	 struct nlist *np;
	 Tcl_Obj *lobj;
	 int nodenum;

	 np = LookupCell(cstr);

	 if (np == NULL) {
	    Tcl_SetResult(interp, "No such cell.", NULL);
	    return TCL_ERROR;
	 }

	 for (ob = np->cell; ob != NULL; ob = ob->next) {
	    if (match(nstr, ob->name)) {
	       nodenum = ob->node;
	       break;
	    }
	 }
	 if (ob == NULL) {
	    Tcl_SetResult(interp, "No such node.", NULL);
	    return TCL_ERROR;
	 }
	 lobj = Tcl_NewListObj(0, NULL);
	 for (ob = np->cell; ob != NULL; ob = ob->next) {
	    if (ob->node == nodenum && ob->type >= FIRSTPIN) {
	       char *obname = ob->name;
	       if (*obname == '/') obname++;
               Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewStringObj(obname, -1));
	    }
	 }
	 Tcl_SetObjResult(interp, lobj);
      }
      else
         Fanout(cstr, nstr, ALLELEMENTS);
   }
   else {
      PrintAllElements(cstr);
   }

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_debug				*/
/* Syntax: netgen::debug [on|off] or debug command	*/
/* Formerly: D						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_debug(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *yesno[] = {
      "on", "off", NULL
   };
   enum OptionIdx {
      YES_IDX, NO_IDX, CMD_IDX
   };
   int result, index;
   char *command;

   if (objc == 1)
      index = YES_IDX;
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)yesno,
		"option", 0, &index) != TCL_OK) {
         index = CMD_IDX;
      }
   }

   switch(index) {
      case YES_IDX:
	 Debug = TRUE;
	 break;
      case NO_IDX:
	 Debug = FALSE;
	 break;
      case CMD_IDX:
	 /* Need to redefine DBUG_PUSH! */
	 command = Tcl_GetString(objv[1]);
	 DBUG_PUSH(command);
   }

   if (index != CMD_IDX)
      Printf("Debug mode is %s\n", Debug?"ON":"OFF");

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_protochip			*/
/* Syntax: netgen::protochip				*/
/* Formerly: P						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_protochip(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   PROTOCHIP();
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_instances			*/
/* Syntax: netgen::instances cell			*/
/* Formerly: i						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_instances(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr;

   if (objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "cell");
      return TCL_ERROR;
   }
   repstr = Tcl_GetString(objv[1]);
   PrintInstances(repstr);
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_contents			*/
/* Syntax: netgen::contents cell			*/
/* Formerly: c						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_contents(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr;

   if (objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "cell");
      return TCL_ERROR;
   }
   repstr = Tcl_GetString(objv[1]);
   PrintCell(repstr);
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_describe			*/
/* Syntax: netgen::describe cell			*/
/* Formerly: d						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_describe(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr;
   int file = -1;
   int result;

   if (objc != 2 && objc != 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "cell [file]");
      return TCL_ERROR;
   }
   repstr = Tcl_GetString(objv[1]);
   if (objc == 3) {
      result = Tcl_GetIntFromObj(interp, objv[2], &file);
      if (result != TCL_OK) return result;
   }
   DescribeInstance(repstr, file);
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_cells				*/
/* Syntax: netgen::cells [list|all] [filename]		*/
/* Formerly: h and H					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_cells(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr, *filename = NULL;

   if (objc < 1 || objc > 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "?list|all? ?filename?");
      return TCL_ERROR;
   }
   if (objc == 1) 
      PrintCellHashTable(0, NULL);
   else {
      if (objc == 3) {
	 filename = Tcl_GetString(objv[2]);
      }
      if (!strncmp(Tcl_GetString(objv[1]), "list", 4)) {
	 PrintCellHashTable(2, filename);
      }
      else {
         repstr = Tcl_GetString(objv[1]);
         if (strncmp(repstr, "all", 3)) {
	    Tcl_WrongNumArgs(interp, 1, objv, "all [filename]");
	    return TCL_ERROR;
         }
         PrintCellHashTable(1, filename);
      }
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_model				*/
/* Syntax: netgen::model name class			*/
/* Formerly: (nothing)					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_model(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   struct nlist *cell;
   char *model, *retclass;
   unsigned char class;

   char *modelclasses[] = {
      "undefined", "nmos", "pmos", "pnp", "npn",
      "resistor", "capacitor", "diode",
      "inductor", "module", "blackbox", "xline",
      "moscap", "mosfet", "bjt", "subcircuit", NULL
   };
   enum OptionIdx {
      UNDEF_IDX,  NMOS_IDX, PMOS_IDX, PNP_IDX, NPN_IDX,
      RES_IDX, CAP_IDX, DIODE_IDX, INDUCT_IDX,
      MODULE_IDX, BLACKBOX_IDX, XLINE_IDX, MOSCAP_IDX,
      MOSFET_IDX, BJT_IDX, SUBCKT_IDX
   };
   int result, index, nports;

   if (objc != 3 && objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "name [class]");
      return TCL_ERROR;
   }

   model = Tcl_GetString(objv[1]);
   cell = LookupCell(model);

   if (cell == NULL) {
      Tcl_SetResult(interp, "No such cell.", NULL);
      return TCL_ERROR;
   }
   nports = NumberOfPorts(model);

   if (objc == 3) {
      if (Tcl_GetIndexFromObj(interp, objv[2], (CONST84 char **)modelclasses,
		"class", 0, &index) != TCL_OK) {
	 return TCL_ERROR;
      }
      switch (index) {
	 case UNDEF_IDX:
	    class = CLASS_UNDEF;
	    break;
	 case NMOS_IDX:
	    if (nports != 4 && nports != 3) goto wrongNumPorts;
	    class = (nports == 4) ? CLASS_NMOS4 : CLASS_NMOS;
	    break;
	 case PMOS_IDX:
	    if (nports != 4 && nports != 3) goto wrongNumPorts;
	    class = (nports == 4) ? CLASS_PMOS4 : CLASS_PMOS;
	    break;
	 case PNP_IDX:
	    if (nports != 3) goto wrongNumPorts;
	    class = CLASS_PNP;
	    break;
	 case NPN_IDX:
	    if (nports != 3) goto wrongNumPorts;
	    class = CLASS_NPN;
	    break;
	 case RES_IDX:
	    if (nports != 2 && nports != 3) goto wrongNumPorts;
	    class = (nports == 2) ? CLASS_RES : CLASS_RES3;
	    break;
	 case CAP_IDX:
	    if (nports != 2 && nports != 3) goto wrongNumPorts;
	    class = (nports == 2) ? CLASS_CAP : CLASS_CAP3;
	    break;
	 case DIODE_IDX:
	    if (nports != 2) goto wrongNumPorts;
	    class = CLASS_DIODE;
	    break;
	 case INDUCT_IDX:
	    if (nports != 2) goto wrongNumPorts;
	    class = CLASS_INDUCTOR;
	    break;
	 case XLINE_IDX:
	    if (nports != 4) goto wrongNumPorts;
	    class = CLASS_XLINE;
	    break;
	 case BJT_IDX:
	    if (nports != 3) goto wrongNumPorts;
	    class = CLASS_BJT;
	    break;
	 case MOSFET_IDX:
	    if (nports != 4 && nports != 3) goto wrongNumPorts;
	    class = (nports == 4) ? CLASS_FET4 : CLASS_FET;
	    break;
	 case MOSCAP_IDX:
	    if (nports != 3) goto wrongNumPorts;
	    class = CLASS_ECAP;
	    break;
	 case MODULE_IDX:
	 case BLACKBOX_IDX:
	    class = CLASS_MODULE;
	    break;
	 case SUBCKT_IDX:
	    class = CLASS_SUBCKT;
	    break;
      }
      cell->class = class;
   }
   else {
      class = cell->class;

      switch (class) {
	 case CLASS_NMOS: case CLASS_NMOS4:
	    retclass = modelclasses[NMOS_IDX];
	    break;

	 case CLASS_PMOS: case CLASS_PMOS4:
	    retclass = modelclasses[PMOS_IDX];
	    break;

	 case CLASS_FET3: case CLASS_FET4: case CLASS_FET:
	    retclass = "mosfet";
	    break;

	 case CLASS_BJT:
	    retclass = "bipolar";
	    break;

	 case CLASS_NPN:
	    retclass = modelclasses[NPN_IDX];
	    break;

	 case CLASS_PNP:
	    retclass = modelclasses[PNP_IDX];
	    break;

	 case CLASS_RES: case CLASS_RES3:
	    retclass = modelclasses[RES_IDX];
	    break;

	 case CLASS_CAP: case CLASS_ECAP: case CLASS_CAP3:
	    retclass = modelclasses[CAP_IDX];
	    break;

	 case CLASS_SUBCKT:
	    retclass = modelclasses[SUBCKT_IDX];
	    break;

	 default: /* (includes case CLASS_UNDEF) */
	    retclass = modelclasses[UNDEF_IDX];
	    break;
      }
      Tcl_SetResult(interp, retclass, NULL);
   }
   return TCL_OK;

wrongNumPorts:
   Tcl_SetResult(interp, "Wrong number of ports for device", NULL);
   return TCL_ERROR;
}

/*------------------------------------------------------*/
/* Function name: _netgen_ports				*/
/* Syntax: netgen::ports cell				*/
/* Formerly: p						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_ports(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr;

   if (objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "cell");
      return TCL_ERROR;
   }
   repstr = Tcl_GetString(objv[1]);
   PrintPortsInCell(repstr);
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_leaves			*/
/* Syntax: netgen::leaves [cell]			*/
/* Formerly: l and L					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_leaves(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr;

   if (objc != 1 && objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "?cell?");
      return TCL_ERROR;
   }
   if (objc == 1) {
      Printf("List of all leaf cells:\n");
      PrintAllLeaves();
   }
   else {
      repstr = Tcl_GetString(objv[1]);
      ClearDumpedList();
      PrintLeavesInCell(repstr);
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_quit				*/
/* Syntax: netgen::quit					*/
/* Formerly: q and Q					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_quit(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   exit(0);	/* In Tcl, this should remove the	*/
		/* netgen module, not exit abrubtly!	*/

   /* return TCL_OK; */
}

/*------------------------------------------------------*/
/* Function name: _netgen_reinit			*/
/* Syntax: netgen::reinitialize				*/
/* Formerly: I						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_reinit(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   Initialize();
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_log				*/
/* Syntax: netgen::log [option...]			*/
/* Formerly: (xnetgen command only)			*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_log(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *yesno[] = {
      "start", "end", "reset", "suspend", "resume", "file", "echo", "put", NULL
   };
   enum OptionIdx {
      START_IDX, END_IDX, RESET_IDX, SUSPEND_IDX, RESUME_IDX, FILE_IDX,
	ECHO_IDX, PUT_IDX
   };
   int result, index, i;
   char *tmpstr;
   FILE *file;

   if (objc == 1) {
      index = (LoggingFile) ? RESUME_IDX : START_IDX;
   }
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)yesno,
		"option", 0, &index) != TCL_OK) {
	 return TCL_ERROR;
      }
   }

   switch(index) {
      case START_IDX:
      case RESUME_IDX:
	 if (LoggingFile) {
	    Tcl_SetResult(interp, "Already logging output.", NULL);
	    return TCL_ERROR;
	 }
	 break;
      case END_IDX:
      case RESET_IDX:
      case SUSPEND_IDX:
	 if (!LoggingFile) {
	    Tcl_SetResult(interp, "Not logging data.", NULL);
	    return TCL_ERROR;
	 }
	 /* Don't leave echo off if we're stopping the log */
	 if (NoOutput) NoOutput = FALSE;
	 break;
   }

   switch(index) {
      case START_IDX:
      case RESUME_IDX:
      case RESET_IDX:
	 if (LogFileName == NULL) {
	    Tcl_SetResult(interp, "No log file declared.  "
			"Use \"log file <name>\"", NULL);
	    return TCL_ERROR;
	 }
	 break;
   }

   switch(index) {
      case START_IDX:
	 LoggingFile = fopen(LogFileName, "w");
	 break;
      case RESUME_IDX:
	 LoggingFile = fopen(LogFileName, "a");
	 break;
      case END_IDX:
	 fclose(LoggingFile);
	 LoggingFile = FALSE;
	 break;
      case RESET_IDX:
	 fclose(LoggingFile);
	 LoggingFile = fopen(LogFileName, "w");
	 break;
      case SUSPEND_IDX:
	 fclose(LoggingFile);
	 LoggingFile = FALSE;
	 break;
      case FILE_IDX:
	 if (objc == 2)
	    Tcl_SetResult(interp, LogFileName, NULL);
	 else {
	    if (LoggingFile) {
	       fclose(LoggingFile);
	       LoggingFile = FALSE;
	       Printf("Closed old log file \"%s\".\n", LogFileName);
	    }
	    tmpstr = Tcl_GetString(objv[2]);
	    if (LogFileName) Tcl_Free(LogFileName);
	    LogFileName = (char *)Tcl_Alloc(1 + strlen(tmpstr));
	    strcpy(LogFileName, tmpstr);
	 }
	 break;
      case PUT_IDX:
	 // All arguments after "log put" get sent to stdout through Tcl,
	 // and also to the logfile, if the logfile is enabled.
	 for (i = 2; i < objc; i++) {
	    Fprintf(stdout, Tcl_GetString(objv[i]));
	 }
	 if (!NoOutput) Printf("\n");
	 return TCL_OK;
      case ECHO_IDX:
	 if (objc == 2) {
	    Tcl_SetResult(interp, (NoOutput) ? "off" : "on", NULL);
	 }
	 else {
	    int bval;
	    result = Tcl_GetBooleanFromObj(interp, objv[2], &bval);
	    if (result == TCL_OK)
	       NoOutput = (bval) ? FALSE : TRUE;
	    else
	       return result;
	 }
	 if (Debug)
            Printf("Echoing log file \"%s\" output to console %s\n",
			LogFileName, (NoOutput) ? "disabled" : "enabled");
	 return TCL_OK;
   }
   if ((index != FILE_IDX) && (index != ECHO_IDX))
      Printf("Logging to file \"%s\" %s\n", LogFileName,
		(LoggingFile) ? "enabled" : "disabled");

   return TCL_OK;
}

#ifdef HAVE_MALLINFO
/*------------------------------------------------------*/
/* Function name: _netgen_printmem			*/
/* Syntax: netgen::memory				*/
/* Formerly: m						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_printmem(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   PrintMemoryStats();
   return TCL_OK;
}
#endif

/*------------------------------------------------------*/
/* The following code breaks up the NETCOMP() command	*/
/* from netcmp.c into individual functions w/arguments	*/
/*------------------------------------------------------*/

/*------------------------------------------------------*/
/* Function name: _netcmp_compare			*/
/* Syntax: netgen::compare cell1 cell2			*/
/* or (preferably)					*/
/*	   netgen::compare {file1 cell1} {file2 cell2}	*/
/* Formerly: c						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_compare(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *name1, *name2, *file1, *file2;
   int fnum1, fnum2;
   int dohierarchy = FALSE;
   int argstart = 1, qresult, llen, result;
   struct Correspond *nextcomp;
   struct nlist *tp;
   Tcl_Obj *tobj;

   if (objc > 1) {
      if (!strncmp(Tcl_GetString(objv[argstart]), "hier", 4)) {
	 dohierarchy = TRUE;
 	 argstart++;
      }
   }

   fnum1 = -1;
   fnum2 = -1;

   if (((objc - argstart) == 2) || ((dohierarchy && ((objc - argstart) == 0)))) {

      if (dohierarchy && ((objc - argstart) == 0)) {

         qresult = GetCompareQueueTop(&name1, &fnum1, &name2, &fnum2);
         if (qresult == -1) {
	    Tcl_Obj *lobj;

	    // When queue is empty, return a null list 
	    lobj = Tcl_NewListObj(0, NULL);
	    Tcl_SetObjResult(interp, lobj);
	    return TCL_OK;
         }
      }
      else if ((objc - argstart) == 2) {

         result = Tcl_ListObjLength(interp, objv[argstart], &llen);
         if (result != TCL_OK) return TCL_ERROR;
         if (llen == 2) {
	    result = Tcl_ListObjIndex(interp, objv[argstart], 0, &tobj);
	    if (result != TCL_OK) return TCL_ERROR;
	    result = Tcl_GetIntFromObj(interp, tobj, &fnum1);
	    if (result != TCL_OK) {
		Tcl_ResetResult(interp);
		fnum1 = -1;
		file1 = Tcl_GetString(tobj);
	    }
	    else file1 = NULL;
	    result = Tcl_ListObjIndex(interp, objv[argstart], 1, &tobj);
	    if (result != TCL_OK) return TCL_ERROR;
            name1 = Tcl_GetString(tobj);

	    if (fnum1 == -1) {
		tp = LookupCell(file1);
		if (tp != NULL) {
		   fnum1 = tp->file;
		   tp = LookupCellFile(name1, fnum1);
		   if (tp != NULL)
		      name1 = tp->name;
		}
            }
         } else {
            file1 = Tcl_GetString(objv[argstart]);
	    name1 = file1;
         }

         result = Tcl_ListObjLength(interp, objv[argstart + 1], &llen);
         if (result != TCL_OK) return TCL_ERROR;
         if (llen == 2) {
	    result = Tcl_ListObjIndex(interp, objv[argstart + 1], 0, &tobj);
	    if (result != TCL_OK) return TCL_ERROR;
	    result = Tcl_GetIntFromObj(interp, tobj, &fnum2);
	    if (result != TCL_OK) {
		Tcl_ResetResult(interp);
		fnum2 = -1;
		file2 = Tcl_GetString(tobj);
	    }
	    else file2 = NULL;
	    result = Tcl_ListObjIndex(interp, objv[argstart + 1], 1, &tobj);
	    if (result != TCL_OK) return TCL_ERROR;
            name2 = Tcl_GetString(tobj);

	    if (fnum2 == -1) {
        	tp = LookupCell(file1);
        	if (tp != NULL) {
	           fnum2 = tp->file;
	           tp = LookupCellFile(name2, fnum2);
	           if (tp != NULL)
	              name2 = tp->name;
		}
            }
         } else {
            file2 = Tcl_GetString(objv[argstart + 1]);
	    name2 = file2;
         }

         if (dohierarchy) {
	    RemoveCompareQueue();
	    qresult = CreateCompareQueue(name1, fnum1, name2, fnum2);
	    if (qresult != 0) {
	       Tcl_AppendResult(interp, "No such cell ",
			(qresult == 1) ? name1 : name2, NULL);
	       return TCL_ERROR;
	    }
	    GetCompareQueueTop(&name1, &fnum1, &name2, &fnum2);
         }
      }
   }
   else {
      Tcl_WrongNumArgs(interp, 1, objv, "[hierarchical] cell1 cell2");
      return TCL_ERROR;
   }

   // Resolve global nodes into local nodes and ports
   if (dohierarchy) {
      ConvertGlobals(name1, fnum1);
      ConvertGlobals(name2, fnum2);
   }

   CreateTwoLists(name1, fnum1, name2, fnum2);

   // Return the names of the two cells being compared, if doing "compare
   // hierarchical"

   if (dohierarchy) {
      Tcl_Obj *lobj;

      lobj = Tcl_NewListObj(0, NULL);
      Tcl_ListObjAppendElement(interp, lobj, Tcl_NewStringObj(name1, -1));
      Tcl_ListObjAppendElement(interp, lobj, Tcl_NewStringObj(name2, -1));
      Tcl_SetObjResult(interp, lobj);
   }

#ifdef DEBUG_ALLOC
   PrintCoreStats();
#endif

   Permute();		/* Apply permutations */
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_iterate			*/
/* Syntax: netgen::iterate				*/
/* Formerly: i						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_iterate(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   if (!Iterate())
      Printf("Please iterate again.\n");
   else
      Printf("No fractures made: we're done.\n");

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_summary			*/
/* Syntax: netgen::summary [elements|nodes]		*/
/* Formerly: s						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_summary(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "nodes", "elements", NULL
   };
   enum OptionIdx {
      NODE_IDX, ELEM_IDX
   };
   int result, index = -1;

   if (objc != 1 && objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "?nodes|elements?");
      return TCL_ERROR;
   }
   if (objc == 2) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
   }

   if (objc == 1 || index == ELEM_IDX)
      SummarizeElementClasses(ElementClasses);

   if (objc == 1 || index == NODE_IDX)
      SummarizeNodeClasses(NodeClasses);

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_print				*/
/* Syntax: netgen::print [elements|nodes|queue]		*/
/*		[legal|illegal]				*/
/* Formerly: P						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_print(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "nodes", "elements", "queue", NULL
   };
   enum OptionIdx {
      NODE_IDX, ELEM_IDX, QUEUE_IDX
   };

   /* Note:  The order is such that the type passed to PrintElementClasses()
    * or PrintNodeClasses() is -1 for all, 0 for legal, and 1 for illegal
    */
   char *classes[] = {
      "legal", "illegal", NULL
   };
   enum ClassIdx {
      LEGAL_IDX, ILLEGAL_IDX
   };

   int result, index = -1, class = -1, dolist = 0;
   int fnum1, fnum2;
   char *optstart;

   if (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 objv++;
	 objc--;
      }
   }

   if (objc < 1 || objc > 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "?nodes|elements|queue? ?legal|illegal?");
      return TCL_ERROR;
   }
   if (objc >= 2) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
	 if ((objc == 2) && (Tcl_GetIndexFromObj(interp, objv[1],
		(CONST84 char **)classes, "class", 0, &class) != TCL_OK)) {
            return TCL_ERROR;
	 }
      }
   }
   if (objc == 3 && index != QUEUE_IDX) {
      if (Tcl_GetIndexFromObj(interp, objv[2], (CONST84 char **)classes,
		"class", 0, &class) != TCL_OK) {
         return TCL_ERROR;
      }
   }
   else if (objc == 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "queue [no arguments]");
      return TCL_ERROR;
   }

   enable_interrupt();
   if (objc == 1 || index == ELEM_IDX)
      PrintElementClasses(ElementClasses, class, dolist);

   if (objc == 1 || index == NODE_IDX)
      PrintNodeClasses(NodeClasses, class, dolist);

   if (objc == 2 && index == QUEUE_IDX) {
      char *name1, *name2;
      Tcl_Obj *lobj;
      result = PeekCompareQueueTop(&name1, &fnum1, &name2, &fnum2);
      lobj = Tcl_NewListObj(0, NULL);
      if (result != -1) {
         Tcl_ListObjAppendElement(interp, lobj, Tcl_NewStringObj(name1, -1));
         Tcl_ListObjAppendElement(interp, lobj, Tcl_NewStringObj(name2, -1));
      }
      Tcl_SetObjResult(interp, lobj);
   }

   disable_interrupt();

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_run				*/
/* Syntax: netgen::run [converge|resolve]		*/
/* Formerly: r and R					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_run(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "converge", "resolve", NULL
   };
   enum OptionIdx {
      CONVERGE_IDX, RESOLVE_IDX
   };
   int result, index;
   int automorphisms;

   if (objc == 1)
      index = RESOLVE_IDX;
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
   }


   switch(index) {
      case CONVERGE_IDX:
	 if (ElementClasses == NULL || NodeClasses == NULL) {
	    return TCL_OK;
	 }
	 else {
	    enable_interrupt();
	    while (!Iterate() && !InterruptPending);
	    _netcmp_verify(clientData, interp, 1, NULL);
	    disable_interrupt();
	 }
	 break;
      case RESOLVE_IDX:
	 if (ElementClasses == NULL || NodeClasses == NULL) {
	    // Printf("Must initialize data structures first.\n");
	    // return TCL_ERROR;
	    return TCL_OK;
	 }
	 else {
	    enable_interrupt();
	    while (!Iterate() && !InterruptPending);
	    automorphisms = VerifyMatching();
	    if (automorphisms == -1)
	       Fprintf(stdout, "Graphs do not match.\n");
	    else if (automorphisms == 0)
	       Fprintf(stdout, "Graphs match uniquely.\n");
	    else {
	       Fprintf(stdout, "Graphs match with %d automorphisms.\n",
			automorphisms);
	       PermuteAutomorphisms();
	       while ((automorphisms = ResolveAutomorphisms()) > 0);
	       if (automorphisms == -1) Fprintf(stdout, "Graphs do not match.\n");
		  else Fprintf(stdout, "Circuits match correctly.\n");
	    }
	    if (PropertyErrorDetected) {
	       Fprintf(stdout, "There were property errors.\n");
	       PrintPropertyResults();
	    }
	    disable_interrupt();
         }
	 break;
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_verify			*/
/* Syntax: netgen::verify [option]			*/
/* 	options: nodes, elements, only, all,		*/
/*		 equivalent, or unique.			*/
/* Formerly: v						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_verify(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "nodes", "elements", "properties", "only", "all", "equivalent", "unique", NULL
   };
   enum OptionIdx {
      NODE_IDX, ELEM_IDX, PROP_IDX, ONLY_IDX, ALL_IDX, EQUIV_IDX, UNIQUE_IDX
   };
   int result, index = -1;
   int automorphisms;

   if (objc != 1 && objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv,	
		"?nodes|elements|only|all|equivalent|unique?");
      return TCL_ERROR;
   }
   if (objc == 2) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
   }

   if (ElementClasses == NULL || NodeClasses == NULL) {
      Printf("Cell has no elements and/or nodes.\n");
      Tcl_SetObjResult(interp, Tcl_NewIntObj(-1));
      // return TCL_ERROR;
      return TCL_OK;
   }
   else {
      automorphisms = VerifyMatching();
      if (automorphisms == -1) {
	 enable_interrupt();
	 if (objc == 1 || index == ELEM_IDX || index == ALL_IDX) {
	     if (Debug == TRUE)
	        PrintIllegalElementClasses();	// Old style
	     else
	        FormatIllegalElementClasses();	// Side-by-side
	 }
	 if (objc == 1 || index == NODE_IDX || index == ALL_IDX) {
	     if (Debug == TRUE)
	        PrintIllegalNodeClasses();	// Old style
	     else
	        FormatIllegalNodeClasses();	// Side-by-side
	 }
	 disable_interrupt();
	 if (index == EQUIV_IDX || index == UNIQUE_IDX)
	     Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 else
	     Fprintf(stdout, "Graphs do not match.\n");
      }
      else {
	 if (automorphisms) {
	    if (index == EQUIV_IDX)
	        Tcl_SetObjResult(interp, Tcl_NewIntObj((int)automorphisms));
	    else if (index == UNIQUE_IDX)
	        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    else
	        Fprintf(stdout, "Circuits match with %d automorphism%s.\n",
			automorphisms, (automorphisms == 1) ? "" : "s");
	 }
	 else {
	    if ((index == EQUIV_IDX) || (index == UNIQUE_IDX)) {
		if (PropertyErrorDetected == 0)
	           Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	        else
	           Tcl_SetObjResult(interp, Tcl_NewIntObj(2));
	    }
	    else {
	        Fprintf(stdout, "Circuits match uniquely.\n");
		if (PropertyErrorDetected != 0)
		   Fprintf(stdout, "Property errors were found.\n");
	    }
	 }
         if ((index == PROP_IDX) && (PropertyErrorDetected != 0)) {
	    PrintPropertyResults();
	 }
      }
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_automorphs			*/
/* Syntax: netgen::automorphisms			*/
/* Formerly: a						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_automorphs(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   PrintAutomorphisms();
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_convert			*/
/* Syntax: netgen::convert <cell> [<filenum>]		*/
/* Formerly: nonexistant function			*/
/* Results: none					*/
/* Side Effects:  one or more global nodes changed to	*/
/* 	local scope and ports.				*/
/*------------------------------------------------------*/

int
_netcmp_convert(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    char *cellname;
    int filenum, result;

    if (objc != 2 && objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "<cell>");
	return TCL_ERROR;
    }
    cellname = Tcl_GetString(objv[1]);   

    if (objc == 3) {
	result = Tcl_GetIntFromObj(interp, objv[2], &filenum);
	if (result != TCL_OK) return result;
    }
    else filenum = -1;

    ConvertGlobals(cellname, filenum);
    return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_global			*/
/* Syntax: netgen::global <cell> <name>			*/
/* Formerly: nonexistant function			*/
/* Results: returns number of matching nets found	*/
/* Side Effects:  one or more nodes changed to global	*/
/* 	scope.						*/
/*------------------------------------------------------*/

int
_netcmp_global(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   Tcl_Obj *tobj;
   char *filename, *cellname, *pattern;
   int numchanged = 0, nextp, p, fnum, llen, result;
   struct nlist *tp;

   if (objc < 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "<cell>|<filenum> <pattern> [...]");
      return TCL_ERROR;
   }

   /* Check if first argument is a file number */

   result = Tcl_GetIntFromObj(interp, objv[1], &fnum);
   if (result != TCL_OK) {
      Tcl_ResetResult(interp);
      fnum = -1;
 
      /* Check if second argument is a file number */

      result = Tcl_GetIntFromObj(interp, objv[2], &fnum);
      if (result != TCL_OK) {
         Tcl_ResetResult(interp);
         fnum = -1;
         nextp = 2;

         // Alternatively to supplying "filenum", allow the use of a
         // 2-item list containing a source filename and a class name,
         // in which case the search for the class name will be limited
         // to cells in the specified file.

         result = Tcl_ListObjLength(interp, objv[1], &llen);
         if (result != TCL_OK) return TCL_ERROR;

         if (llen == 2 && fnum == -1) {
            result = Tcl_ListObjIndex(interp, objv[1], 0, &tobj);
            if (result != TCL_OK) return TCL_ERROR;
            filename = Tcl_GetString(tobj);
            result = Tcl_ListObjIndex(interp, objv[1], 1, &tobj);
            if (result != TCL_OK) return TCL_ERROR;
            cellname = Tcl_GetString(tobj);
            tp = LookupCell(filename);
            if (tp != NULL) {
	       tp = LookupCellFile(cellname, tp->file);
	       cellname = tp->name;
            } else {
	       Tcl_SetResult(interp, "No such file.\n", NULL);
	       return TCL_ERROR;
            }
         } else {
            filename = Tcl_GetString(objv[1]);
	    cellname = NULL;
         }
      }
      else {
	 nextp = 3;
	 // 1st argument must be the cellname (only)
         cellname = Tcl_GetString(objv[1]);
      }
   }
   else {	// Only file number given
      nextp = 2;
      cellname = NULL;
   }

   for (p = nextp; p < objc; p++) {
      pattern = Tcl_GetString(objv[p]);
      numchanged += ChangeScope(fnum, cellname, pattern, NODE, GLOBAL);
   }
   
   Tcl_SetObjResult(interp, Tcl_NewIntObj(numchanged));
   return TCL_OK;
}


/*------------------------------------------------------*/
/* Function name: _netcmp_ignore			*/
/* Syntax: netgen::ignore [class <name> [<file>]]	*/
/* Formerly: no such command				*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_ignore(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "class", NULL
   };
   enum OptionIdx {
      CLASS_IDX
   };
   int result, index;
   int file = -1;
   char *name = NULL, *name2 = NULL;

   if (objc >= 3) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
      name = Tcl_GetString(objv[2]);

      if (objc == 4) {
	 result = Tcl_GetIntFromObj(interp, objv[3], &file);
	 if (result != TCL_OK) return result;
      }
   }
   else {
      Tcl_WrongNumArgs(interp, 1, objv, "?class? name");
      return TCL_ERROR;
   }

   switch(index) {
      case CLASS_IDX:
	 IgnoreClass(name, file);
	 break;
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: GetNamesAndFiles			*/
/* Helper function for _netcmp_equate, to parse the	*/
/* input for cell and filename pairs, either as		*/
/* separate arguments or as a list of two items.	*/
/*------------------------------------------------------*/

int
GetNamesAndFiles(Tcl_Interp *interp, Tcl_Obj *CONST objv[], int objc,
	char **name1, char **name2, int *file1, int *file2)
{
    int result, llen;
    int fnum1, fnum2;
    char *fname1, *fname2;
    int argnext;
    struct nlist *tp;
    Tcl_Obj *tobj;

    /* Is 1st argument a list */
    argnext = 0;

    result = Tcl_ListObjLength(interp, objv[argnext], &llen);
    if (result != TCL_OK) return result;

    if (llen == 1) {
	*name1 = Tcl_GetString(objv[argnext]);
	argnext++;

	/* Is 2nd argment a filename or number? */
	result = Tcl_GetIntFromObj(interp, objv[argnext], &fnum1);
	if (result != TCL_OK) {
	    Tcl_ResetResult(interp);
	    fnum1 = -1;
	    fname1 = Tcl_GetString(objv[argnext]);
	    /* Input filenames are unique, so don't need LookupCellFile */
	    tp = LookupCell(fname1);
	    if (tp == NULL) {
		Printf("File %s not loaded.\n", fname1);
		return TCL_ERROR;
	    }
	    else fnum1 = tp->file;
	}
    }
    else if (llen == 2) {
	// List of 2 arguments, either name and file number or
	// name and filename.

	result = Tcl_ListObjIndex(interp, objv[argnext], 0, &tobj);
	if (result != TCL_OK) return result;
	*name1 = Tcl_GetString(tobj);

	result = Tcl_ListObjIndex(interp, objv[argnext], 1, &tobj);
	if (result != TCL_OK) return result;
	result = Tcl_GetIntFromObj(interp, tobj, &fnum1);
	if (result != TCL_OK) {
	    Tcl_ResetResult(interp);
	    fnum1 = -1;
	    fname1 = Tcl_GetString(tobj);
	    /* Input filenames are unique, so don't need LookupCellFile */
	    tp = LookupCell(fname1);
	    if (tp == NULL) {
		Printf("File %s not loaded.\n", fname1);
		return TCL_ERROR;
	    }
	    else fnum1 = tp->file;
	}
    }
    else {
	Tcl_WrongNumArgs(interp, 0, objv, "?name file?");
	return TCL_ERROR;
    }

    // Move on to second set of names/files
    argnext++;

    result = Tcl_ListObjLength(interp, objv[argnext], &llen);
    if (result != TCL_OK) return result;

    if (llen == 1) {
	*name2 = Tcl_GetString(objv[argnext]);
	argnext++;

	/* Is 2nd argment a filename or number? */
	result = Tcl_GetIntFromObj(interp, objv[argnext], &fnum2);
	if (result != TCL_OK) {
	    Tcl_ResetResult(interp);
	    fnum2 = -1;
	    fname2 = Tcl_GetString(objv[argnext]);
	    /* Input filenames are unique, so don't need LookupCellFile */
	    tp = LookupCell(fname2);
	    if (tp == NULL) {
		Printf("File %s not loaded.\n", fname2);
		return TCL_ERROR;
	    }
	    else fnum2 = tp->file;
	}
    }
    else if (llen == 2) {
	// List of 2 arguments, either name and file number or
	// name and filename.

	result = Tcl_ListObjIndex(interp, objv[argnext], 0, &tobj);
	if (result != TCL_OK) return result;
	*name2 = Tcl_GetString(tobj);

	result = Tcl_ListObjIndex(interp, objv[argnext], 1, &tobj);
	if (result != TCL_OK) return result;
	result = Tcl_GetIntFromObj(interp, tobj, &fnum1);
	if (result != TCL_OK) {
	    Tcl_ResetResult(interp);
	    fnum2 = -1;
	    fname2 = Tcl_GetString(tobj);
	    /* Input filenames are unique, so don't need LookupCellFile */
	    tp = LookupCell(fname2);
	    if (tp == NULL) {
		Printf("File %s not loaded.\n", fname2);
		return TCL_ERROR;
	    }
	    else fnum2 = tp->file;
	}
    }

    *file1 = fnum1;
    *file2 = fnum2;

    return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_equate			*/
/* Syntax: netgen::equate [elements|nodes|classes|pins]	*/
/* Formerly: e and n					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

// FIXME:  MatchPins() requires that Circuit1, Circuit2 have
// been compared, so it is not really possible to give the
// routine two cells to match pins for.  Currently any cell
// names passed to "equate pins" are ignored.

int
_netcmp_equate(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "nodes", "elements", "classes", "pins", NULL
   };
   enum OptionIdx {
      NODE_IDX, ELEM_IDX, CLASS_IDX, PINS_IDX
   };
   int result, index;
   char *name1 = NULL, *name2 = NULL;

   if (objc == 3) {
      index = NODE_IDX;
      name1 = Tcl_GetString(objv[1]);
      name2 = Tcl_GetString(objv[2]);
   }
   else if (objc == 2 || objc == 4) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
      if (objc == 4) {
	 name1 = Tcl_GetString(objv[2]);
	 name2 = Tcl_GetString(objv[3]);
      }
      else if (index != PINS_IDX) {
         Tcl_WrongNumArgs(interp, 1, objv, "?nodes|elements|classes|pins? name1 name2");
         return TCL_ERROR;
      }
   }
   else {
      Tcl_WrongNumArgs(interp, 1, objv, "?nodes|elements|classes|pins? name1 name2");
      return TCL_ERROR;
   }

   switch(index) {
      case NODE_IDX:
	 if (NodeClasses == NULL) {
	    Printf("Cell has no nodes.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    return TCL_OK;
	 }
	 if (EquivalenceNodes(name1, name2)) {
	    Printf("Nodes %s and %s are equivalent.\n", name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	 }
	 else {
	    Printf("Unable to equate nodes %s and %s.\n",name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 }
	 break;
      case ELEM_IDX:
	 if (ElementClasses == NULL) {
	    Printf("Cell has no elements.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    return TCL_OK;
	 }
	 if (EquivalenceElements(name1, name2)) {
	    Printf("Elements %s and %s are equivalent.\n", name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	 }
	 else {
	    Printf("Unable to equate elements %s and %s.\n",name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 }
	 break;
      case CLASS_IDX:
	 if (EquivalenceClasses(name1, -1, name2, -1)) {
	    Printf("Device classes %s and %s are equivalent.\n", name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	 }
	 else {
	    Printf("Unable to equate device classes %s and %s.\n",name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 }
	 break;
      case PINS_IDX:
	 if (ElementClasses == NULL) {
	    Printf("Cell has no elements.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    return TCL_OK;
	 }
	 if (MatchPins()) {
	    Printf("Cell pin lists are equivalent.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	 }
	 else {
	    Printf("Cell pin lists cannot be made equivalent.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 }
	 break;
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_permute			*/
/* Syntax: netgen::permute				*/
/*	   netgen::permute permute_class		*/
/*	   netgen::permute cell pin1 pin2		*/
/* Formerly: t						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_permute(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *model, *pin1, *pin2;
   char *permuteclass[] = {
      "transistors", "resistors", "capacitors", "default", "pins",
		"forget", NULL
   };
   enum OptionIdx {
      TRANS_IDX, RES_IDX, CAP_IDX, DEFLT_IDX, PINS_IDX, FORGET_IDX
   };
   int result, index;
   struct nlist *tp;

   if (objc != 1 && objc != 2 && objc != 4 && objc != 5) {
      Tcl_WrongNumArgs(interp, 1, objv, "?cell pin1 pin2?");
      return TCL_ERROR;
   }
   if (objc == 1) {
      index = DEFLT_IDX;
   }
   else if (objc == 2) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)permuteclass,
		"permute class", 0, &index) != TCL_OK)
         return TCL_ERROR;
   }
   if (objc == 1 || objc == 2) {
      tp = FirstCell();
      while (tp != NULL) {
	 switch (tp->class) {
	    case CLASS_NMOS: case CLASS_PMOS: case CLASS_FET3:
	    case CLASS_NMOS4: case CLASS_PMOS4: case CLASS_FET4:
	    case CLASS_FET:
	       if (index == TRANS_IDX || index == DEFLT_IDX)
	          PermuteSetup(tp->name, "source", "drain");
	       break;
	    case CLASS_RES: case CLASS_RES3:
	       if (index == RES_IDX || index == DEFLT_IDX)
	          PermuteSetup(tp->name, "end_a", "end_b");
	       break;
	    case CLASS_CAP: case CLASS_ECAP: case CLASS_CAP3:
	       if (index == CAP_IDX)
	          PermuteSetup(tp->name, "top", "bottom");
	       break;
	 }
	 tp = NextCell();
      }
   }
   else {
      /* equivalence two pins on a given class of element */

      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)permuteclass,
		"permute class", 0, &index) != TCL_OK) {
	 Tcl_ResetResult(interp);

	 if (objc == 4) {
	    model = Tcl_GetString(objv[1]);
	    pin1 = Tcl_GetString(objv[2]);
	    pin2 = Tcl_GetString(objv[3]);
	 }
	 else {
	    Tcl_WrongNumArgs(interp, 1, objv, "?cell pin1 pin2?");
	    return TCL_ERROR;
	 }
      }
      else if (index == PINS_IDX) {
	 if (objc == 5) {
	    model = Tcl_GetString(objv[2]);
	    pin1 = Tcl_GetString(objv[3]);
	    pin2 = Tcl_GetString(objv[4]);
	 }
	 else {
	    Tcl_WrongNumArgs(interp, 2, objv, "?cell pin1 pin2?");
	    return TCL_ERROR;
	 }
      }
      else if (index == FORGET_IDX) {
	 if (PermuteForget(model, pin1, pin2))
	    Printf("%s != %s\n", pin1, pin2);
	 else
	    Printf("Unable to reset pin permutation %s, %s.\n", pin1, pin2);
	 return TCL_OK;
      }

      if (PermuteSetup(model, pin1, pin2))
	 Printf("%s == %s\n", pin1, pin2);
      else
	 Printf("Unable to permute pins %s, %s.\n", pin1, pin2);
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_exhaustive			*/
/* Syntax: netgen::exhaustive [on|off]			*/
/* Formerly: x						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_exhaustive(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *yesno[] = {
      "on", "off", NULL
   };
   enum OptionIdx {
      YES_IDX, NO_IDX
   };
   int result, index;

   if (objc == 1)
      index = YES_IDX;
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)yesno,
		"option", 0, &index) != TCL_OK)
         return TCL_ERROR;
   }

   switch(index) {
      case YES_IDX:
	 ExhaustiveSubdivision = TRUE;
	 break;
      case NO_IDX:
	 ExhaustiveSubdivision = FALSE;
	 break;
   }
   Printf("Exhaustive subdivision %s.\n", 
	     ExhaustiveSubdivision ? "ENABLED" : "DISABLED");

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_restart			*/
/* Syntax: netgen::restart				*/
/* Formerly: o						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_restart(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   RegroupDataStructures();
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_help				*/
/* Syntax: netgen::help					*/
/* Formerly: [any invalid command]			*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_help(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int n;

   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }

   for (n = 0; netgen_cmds[n].name != NULL; n++) {
      Printf("netgen::%s", netgen_cmds[n].name);
      Printf(" %s\n", netgen_cmds[n].helptext);
   }
   for (n = 0; netcmp_cmds[n].name != NULL; n++) {
      Printf("netgen::%s", netcmp_cmds[n].name);
      Printf(" %s\n", netcmp_cmds[n].helptext);
   }

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_matching			*/
/* Syntax: netgen::matching [element|node] <name>	*/
/* Formerly: [no such function]				*/
/* Results: 						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_matching(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "nodes", "elements", NULL
   };
   enum OptionIdx {
      NODE_IDX, ELEM_IDX
   };
   int result, index;
   struct objlist *obj;
   char *name;

   if (objc != 2 &&  objc != 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "?node|element? name");
      return TCL_ERROR;
   }

   if (objc == 2) {
      index = NODE_IDX;
      name = Tcl_GetString(objv[1]);
   }
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
      name = Tcl_GetString(objv[2]);
   }

   switch(index) {
      case NODE_IDX:
	 result = EquivalentNode(name, NULL, &obj);
	 if (result > 0)
	    Tcl_SetResult(interp, obj->name, NULL);
	 else {
	    if (result < 0)
	       Tcl_SetResult(interp, "No such node.", NULL);
	    else
	       Tcl_SetResult(interp, "No matching node.", NULL);
	    return TCL_ERROR;
	 }
	 break;
      case ELEM_IDX:
	 result = EquivalentElement(name, NULL, &obj);
	 if (result > 0)
	    Tcl_SetResult(interp, obj->name, NULL);
	 else {
	    if (result < 0)
	       Tcl_SetResult(interp, "No such element.", NULL);
	    else
	       Tcl_SetResult(interp, "No matching element.", NULL);
	    return TCL_ERROR;
	 }
	 break;
   }

   if (obj == NULL) {
      Tcl_SetResult(interp, "Cannot find equivalent node", NULL);
      return TCL_ERROR;
   }
   return TCL_OK;
}


/*------------------------------------------------------*/
/* Define a calloc() function for Tcl			*/
/*------------------------------------------------------*/

char *tcl_calloc(size_t asize, size_t nbytes)
{
   size_t tsize = asize * nbytes;
   char *cp = Tcl_Alloc((int)tsize);
   bzero((void *)cp, tsize);
   return cp;
}

/*------------------------------------------------------*/
/* Redefine the printf() functions for use with tkcon	*/
/*------------------------------------------------------*/

void tcl_vprintf(FILE *f, const char *fmt, va_list args_in)
{
    va_list args;
    static char outstr[128] = "puts -nonewline std";
    char *outptr, *bigstr = NULL, *finalstr = NULL;
    int i, nchars, result, escapes = 0, limit;
    Tcl_Interp *printinterp = (UseTkConsole) ? consoleinterp : netgeninterp;

    strcpy (outstr + 19, (f == stderr) ? "err \"" : "out \"");
    outptr = outstr;

    va_copy(args, args_in);
    nchars = vsnprintf(outptr + 24, 102, fmt, args);
    va_end(args);

    if (nchars >= 102)
    {
	va_copy(args, args_in);
	bigstr = Tcl_Alloc(nchars + 26);
	strncpy(bigstr, outptr, 24);
	outptr = bigstr;
	vsnprintf(outptr + 24, nchars + 2, fmt, args);
	va_end(args);
    }
    else if (nchars == -1) nchars = 126;

    for (i = 24; *(outptr + i) != '\0'; i++) {
	if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
	    	*(outptr + i) == ']' || *(outptr + i) == '\\' ||
		*(outptr + i) == '$')
	    escapes++;
	if (*(outptr + i) == '\n')
	    ColumnBase = 0;
	else
	    ColumnBase++;
    }

    if (escapes > 0)
    {
	finalstr = Tcl_Alloc(nchars + escapes + 26);
	strncpy(finalstr, outptr, 24);
	escapes = 0;
	for (i = 24; *(outptr + i) != '\0'; i++)
	{
	    if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
	    		*(outptr + i) == ']' || *(outptr + i) == '\\' ||
			*(outptr + i) == '$')
	    {
	        *(finalstr + i + escapes) = '\\';
		escapes++;
	    }
	    *(finalstr + i + escapes) = *(outptr + i);
	}
        outptr = finalstr;
    }

    *(outptr + 24 + nchars + escapes) = '\"';
    *(outptr + 25 + nchars + escapes) = '\0';

    result = Tcl_Eval(printinterp, outptr);

    if (bigstr != NULL) Tcl_Free(bigstr);
    if (finalstr != NULL) Tcl_Free(finalstr);
}
    
/*------------------------------------------------------*/
/* Console output flushing which goes along with the	*/
/* routine tcl_vprintf() above.				*/
/*------------------------------------------------------*/

void tcl_stdflush(FILE *f)
{   
   Tcl_SavedResult state;
   static char stdstr[] = "::flush stdxxx";
   char *stdptr = stdstr + 11;
    
   Tcl_SaveResult(netgeninterp, &state);
   strcpy(stdptr, (f == stderr) ? "err" : "out");
   Tcl_Eval(netgeninterp, stdstr);
   Tcl_RestoreResult(netgeninterp, &state);
}

/*------------------------------------------------------*/
/* Define a version of strdup() that uses Tcl_Alloc	*/
/* to match the use of Tcl_Free() for calls to FREE()	*/
/* Note objlist.h and config.h definitions for		*/
/* strsave() and STRDUP().				*/
/*------------------------------------------------------*/

char *Tcl_Strdup(const char *s)
{
   char *snew;
   int slen;

   slen = 1 + strlen(s);
   snew = Tcl_Alloc(slen);
   if (snew != NULL)
      memcpy(snew, s, slen);

   return snew;
}

/*------------------------------------------------------*/
/* Experimental---generate an interrupt condition	*/
/* from a Control-C in the console window.		*/
/* The console script binds this procedure to Ctrl-C.	*/
/*------------------------------------------------------*/

int _tkcon_interrupt(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   InterruptPending = 1;
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Allow Tcl to periodically do (Tk) window events.  This	*/
/* will not cause problems because netgen is not inherently	*/
/* window based and only the console defines window commands.	*/
/* This also works with the terminal-based method although	*/
/* in that case, Tcl_DoOneEvent() should always return 0.	*/
/*--------------------------------------------------------------*/

int check_interrupt() {
   Tcl_DoOneEvent(TCL_WINDOW_EVENTS | TCL_DONT_WAIT);
   if (InterruptPending) {
      Fprintf(stderr, "Interrupt!\n");
      return 1;
   }
   return 0;
}

/*------------------------------------------------------*/
/* Tcl package initialization function			*/
/*------------------------------------------------------*/

int Tclnetgen_Init(Tcl_Interp *interp)
{
   int n;
   char keyword[128];
   char *cadroot;

   /* Sanity checks! */
   if (interp == NULL) return TCL_ERROR;

   /* Remember the interpreter */
   netgeninterp = interp;

   if (Tcl_InitStubs(interp, "8.1", 0) == NULL) return TCL_ERROR;
  
   for (n = 0; netgen_cmds[n].name != NULL; n++) {
      sprintf(keyword, "netgen::%s", netgen_cmds[n].name);
      Tcl_CreateObjCommand(interp, keyword, netgen_cmds[n].handler,
		(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
   }
   for (n = 0; netcmp_cmds[n].name != NULL; n++) {
      sprintf(keyword, "netgen::%s", netcmp_cmds[n].name);
      Tcl_CreateObjCommand(interp, keyword, netcmp_cmds[n].handler,
		(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
   }

   Tcl_Eval(interp, "namespace eval netgen namespace export *");

   /* Set $CAD_ROOT as a Tcl variable */

   cadroot = getenv("CAD_ROOT");
   if (cadroot == NULL) cadroot = CAD_DIR;
   Tcl_SetVar(interp, "CAD_ROOT", cadroot, TCL_GLOBAL_ONLY);

   Tcl_PkgProvide(interp, "Tclnetgen", "1.1");

   if ((consoleinterp = Tcl_GetMaster(interp)) == NULL)
      consoleinterp = interp;

   Tcl_CreateObjCommand(consoleinterp, "netgen::interrupt", _tkcon_interrupt,
		(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

   InitializeCommandLine(0, NULL);
   sprintf(keyword, "Netgen %s.%s compiled on %s\n", NETGEN_VERSION,
		NETGEN_REVISION, NETGEN_DATE);
   Printf(keyword);

   return TCL_OK;	/* Drop back to interpreter for input */
}

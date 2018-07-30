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

/* ntk2adl.c  -- a simple wrapper to translate .ntk to ACTEL format */

#include <stdio.h>
#include "netgen.h"

#ifndef HAVE_X11
/* the following two procedures permit linking
 * with netgen.a even if HAVE_X11 has been disabled
 */

void X_display_line(char *buf)
{
  printf("%s", buf);
}

void X_display_refresh(void)
{
  fflush(stdout);
}
#endif


void STRCPY(char *dest, char *source)
{
  while ((*dest++ = *source++) != '\0') ;
}

int main(int argc, char *argv[])
{
  char cellname[200];

  Debug = 0;
  if (argc < 2 || argc > 3) {
    printf ("usage: ntk2adl <netlist file name> [<top level cell name>]\n");
    return (-1);
  }
  Initialize();
  ActelLib();

  STRCPY(cellname, ReadNetlist(argv[1]));
  if (argc == 3) STRCPY(cellname, argv[2]);

  Actel(cellname, NULL);
  return(0);
}

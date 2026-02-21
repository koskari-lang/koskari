/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


#ifndef KOSKARI_DEBUG_H
#define KOSKARI_DEBUG_H


#ifndef KOS_STACK_TRACE
#define KOS_STACK_TRACE 0
#endif


#ifndef VERBOSE_DEBUG
#define VERBOSE_DEBUG 0
#endif


#if VERBOSE_DEBUG
#define SAY(m)                                  \
  PySys_WriteStdout(m "\n")
#define SAYF(m, a...)                           \
  PySys_FormatStdout(m "\n", a);

#else
#define SAY(m) {}
#define SAYF(m, a...) {}

#endif


/* Debug helper functions */
const char *kos_dynnode_kind_name(int kind);


#endif

// The end.

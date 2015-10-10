# Copyright (c) MariaDB Corporation. All rights reserved.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
# Add library plus some additional MySQL specific stuff
# Usage (same as for standard CMake's ADD_LIBRARY)
#
#add_library(<name> [STATIC | SHARED | MODULE]
#            [EXCLUDE_FROM_ALL]
#            source1 [source2 ...])
#
# Dependend on PGO_MODE the library will be compiled in either
# instrumentation or optimization mode
FUNCTION (MYSQL_ADD_LIBRARY)
  ADD_LIBRARY(${ARGN})
  LIST(GET ARGN 0 target)
  HANDLE_PGO(${target})
ENDFUNCTION()

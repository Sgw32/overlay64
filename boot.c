/*
overlay64 -- video overlay module
Copyright (C) 2016 Henning Bekel

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "usb.h"
#include "protocol.h"

int boot() {
  printf("Entering bootloader...\n");  
  usb_send("/dev/overlay64", OVERLAY64_BOOT, 0, (uint8_t*)NULL, 0);
  return true;
}


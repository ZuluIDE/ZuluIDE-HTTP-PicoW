/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
 *
 * ZuluIDE™ firmware is licensed under the GPL version 3 or any later version.
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 **/

#include "url_decode.h"

void urldecode(char *str) {
   if (!str) return;
   int len = strlen(str);
   int write = 0;
   for (int read = 0; read < len; read++) {
      switch (str[read]) {
         case '+': {
            str[write++] = ' ';
            break;
         }

         case '%': {
            // If there are 2 more chars and they're hex digits
            if (
                (read + 2) < len
                && isxdigit(str[read + 1])
                && isxdigit(str[read + 2])
            ) {
                // Skip the %
                read++;
                sscanf(str + read++, "%2hhx", str + write++);
            }
            // Pass the %
            else {
                str[write++] = '%';
            }
            break;
         }

         default: {
            str[write++] = str[read];
            break;
         }
      }
   }

   memset(str + write, 0, len - write);
}

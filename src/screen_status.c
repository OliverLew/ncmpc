/* ncmpc (Ncurses MPD Client)
 * (c) 2004-2010 The Music Player Daemon Project
 * Project homepage: http://musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "screen_status.h"
#include "screen.h"

#include <stdarg.h>

void
screen_status_clear_message(void)
{
	status_bar_clear_message(&screen.status_bar);
}

void
screen_status_message(const char *msg)
{
	status_bar_message(&screen.status_bar, msg);
}

void
screen_status_printf(const char *format, ...)
{
	char *msg;
	va_list ap;

	va_start(ap,format);
	msg = g_strdup_vprintf(format,ap);
	va_end(ap);
	screen_status_message(msg);
	g_free(msg);
}

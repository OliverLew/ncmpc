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

#include "screen_text.h"
#include "screen_find.h"
#include "charset.h"

#include <assert.h>
#include <string.h>

void
screen_text_clear(struct screen_text *text)
{
	list_window_reset(text->lw);

	for (guint i = 0; i < text->lines->len; ++i)
		g_free(g_ptr_array_index(text->lines, i));

	g_ptr_array_set_size(text->lines, 0);
	list_window_set_length(text->lw, 0);
}

void
screen_text_append(struct screen_text *text, const char *str)
{
	assert(str != NULL);

	const char *eol;
	while ((eol = strchr(str, '\n')) != NULL) {
		char *line;

		const char *next = eol + 1;

		/* strip whitespace at end */

		while (eol > str && (unsigned char)eol[-1] <= 0x20)
			--eol;

		/* create copy and append it to text->lines */

		line = g_malloc(eol - str + 1);
		memcpy(line, str, eol - str);
		line[eol - str] = 0;

		g_ptr_array_add(text->lines, line);

		/* reset control characters */

		for (eol = line + (eol - str); line < eol; ++line)
			if ((unsigned char)*line < 0x20)
				*line = ' ';

		str = next;
	}

	if (*str != 0)
		g_ptr_array_add(text->lines, g_strdup(str));

	list_window_set_length(text->lw, text->lines->len);
}

const char *
screen_text_list_callback(unsigned idx, void *data)
{
	const struct screen_text *text = data;

	assert(idx < text->lines->len);

	char *value = utf8_to_locale(g_ptr_array_index(text->lines, idx));

	static char buffer[256];
	g_strlcpy(buffer, value, sizeof(buffer));
	g_free(value);

	return buffer;
}

bool
screen_text_cmd(struct screen_text *text,
		gcc_unused struct mpdclient *c, command_t cmd)
{
	if (list_window_scroll_cmd(text->lw, cmd)) {
		screen_text_repaint(text);
		return true;
	}

	list_window_set_cursor(text->lw, text->lw->start);
	if (screen_find(text->lw, cmd, screen_text_list_callback, text)) {
		/* center the row */
		list_window_center(text->lw, text->lw->selected);
		screen_text_repaint(text);
		return true;
	}

	return false;
}

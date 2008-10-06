#ifndef WREADLN_H
#define WREADLN_H

#include <glib.h>
#include <ncurses.h>

/* completion callback data */
extern void *wrln_completion_callback_data;

/* called after TAB is pressed but before g_completion_complete */
typedef void (*wrln_gcmp_pre_cb_t) (GCompletion *gcmp, gchar *buf, void *data);
extern wrln_gcmp_pre_cb_t wrln_pre_completion_callback;

/* post completion callback */
typedef void (*wrln_gcmp_post_cb_t) (GCompletion *gcmp, gchar *s, GList *l,
                                     void *data);
extern wrln_gcmp_post_cb_t wrln_post_completion_callback;

/* Note, wreadln calls curs_set() and noecho(), to enable cursor and
 * disable echo. wreadln will not restore these settings when exiting! */
gchar *wreadln(WINDOW *w,            /* the curses window to use */
	       const gchar *prompt, /* the prompt string or NULL */
	       const gchar *initial_value, /* initial value or NULL for a empty line
					    * (char *) -1 = get value from history */
	       unsigned x1,              /* the maximum x position or 0 */
	       GList **history,     /* a pointer to a history list or NULL */
	       GCompletion *gcmp    /* a GCompletion structure or NULL */
	       );

gchar *
wreadln_masked(WINDOW *w,
	       const gchar *prompt,
	       const gchar *initial_value,
	       unsigned x1,
	       GList **history,
	       GCompletion *gcmp);

#endif

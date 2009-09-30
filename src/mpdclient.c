/* ncmpc (Ncurses MPD Client)
 * (c) 2004-2009 The Music Player Daemon Project
 * Project homepage: http://musicpd.org

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "mpdclient.h"
#include "filelist.h"
#include "screen_client.h"
#include "config.h"
#include "options.h"
#include "strfsong.h"
#include "utils.h"

#include <mpd/client.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#undef  ENABLE_FANCY_PLAYLIST_MANAGMENT_CMD_ADD /* broken with song id's */
#define ENABLE_FANCY_PLAYLIST_MANAGMENT_CMD_DELETE
#define ENABLE_FANCY_PLAYLIST_MANAGMENT_CMD_MOVE

#define BUFSIZE 1024

static bool
MPD_ERROR(const struct mpdclient *client)
{
	return client->connection == NULL ||
		mpd_connection_get_error(client->connection) != MPD_ERROR_SUCCESS;
}

/* filelist sorting functions */
static gint
compare_filelistentry(gconstpointer filelist_entry1,
			  gconstpointer filelist_entry2)
{
	const struct mpd_entity *e1, *e2;
	int n = 0;

	e1 = ((const struct filelist_entry *)filelist_entry1)->entity;
	e2 = ((const struct filelist_entry *)filelist_entry2)->entity;

	if (e1 != NULL && e2 != NULL &&
	    mpd_entity_get_type(e1) == mpd_entity_get_type(e2)) {
		switch (mpd_entity_get_type(e1)) {
		case MPD_ENTITY_TYPE_UNKNOWN:
			break;
		case MPD_ENTITY_TYPE_DIRECTORY:
			n = g_utf8_collate(mpd_directory_get_path(mpd_entity_get_directory(e1)),
					   mpd_directory_get_path(mpd_entity_get_directory(e2)));
			break;
		case MPD_ENTITY_TYPE_SONG:
			break;
		case MPD_ENTITY_TYPE_PLAYLIST:
			n = g_utf8_collate(mpd_playlist_get_path(mpd_entity_get_playlist(e1)),
					   mpd_playlist_get_path(mpd_entity_get_playlist(e2)));
		}
	}
	return n;
}

/* sort by list-format */
gint
compare_filelistentry_format(gconstpointer filelist_entry1,
			     gconstpointer filelist_entry2)
{
	const struct mpd_entity *e1, *e2;
	char key1[BUFSIZE], key2[BUFSIZE];
	int n = 0;

	e1 = ((const struct filelist_entry *)filelist_entry1)->entity;
	e2 = ((const struct filelist_entry *)filelist_entry2)->entity;

	if (e1 && e2 &&
	    mpd_entity_get_type(e1) == MPD_ENTITY_TYPE_SONG &&
	    mpd_entity_get_type(e2) == MPD_ENTITY_TYPE_SONG) {
		strfsong(key1, BUFSIZE, options.list_format, mpd_entity_get_song(e1));
		strfsong(key2, BUFSIZE, options.list_format, mpd_entity_get_song(e2));
		n = strcmp(key1,key2);
	}

	return n;
}


/****************************************************************************/
/*** mpdclient functions ****************************************************/
/****************************************************************************/

gint
mpdclient_handle_error(struct mpdclient *c)
{
	enum mpd_error error = mpd_connection_get_error(c->connection);

	assert(error != MPD_ERROR_SUCCESS);

	if (error == MPD_ERROR_SERVER &&
	    mpd_connection_get_server_error(c->connection) == MPD_SERVER_ERROR_PERMISSION &&
	    screen_auth(c))
		return 0;

	if (error == MPD_ERROR_SERVER)
		error = error | (mpd_connection_get_server_error(c->connection) << 8);

	mpdclient_ui_error(mpd_connection_get_error_message(c->connection));

	if (!mpd_connection_clear_error(c->connection))
		mpdclient_disconnect(c);

	return error;
}

static gint
mpdclient_finish_command(struct mpdclient *c)
{
	return mpd_response_finish(c->connection)
		? 0 : mpdclient_handle_error(c);
}

struct mpdclient *
mpdclient_new(void)
{
	struct mpdclient *c;

	c = g_new0(struct mpdclient, 1);
	playlist_init(&c->playlist);
	c->volume = -1;
	c->events = 0;

	return c;
}

void
mpdclient_free(struct mpdclient *c)
{
	mpdclient_disconnect(c);

	mpdclient_playlist_free(&c->playlist);

	g_free(c);
}

void
mpdclient_disconnect(struct mpdclient *c)
{
	if (c->connection)
		mpd_connection_free(c->connection);
	c->connection = NULL;

	if (c->status)
		mpd_status_free(c->status);
	c->status = NULL;

	playlist_clear(&c->playlist);

	if (c->song)
		c->song = NULL;
}

bool
mpdclient_connect(struct mpdclient *c,
		  const gchar *host,
		  gint port,
		  gfloat _timeout,
		  const gchar *password)
{
	/* close any open connection */
	if( c->connection )
		mpdclient_disconnect(c);

	/* connect to MPD */
	c->connection = mpd_connection_new(host, port, _timeout * 1000);
	if (c->connection == NULL)
		g_error("Out of memory");

	if (mpd_connection_get_error(c->connection) != MPD_ERROR_SUCCESS) {
		mpdclient_handle_error(c);
		mpdclient_disconnect(c);
		return false;
	}

	/* send password */
	if (password != NULL && !mpd_run_password(c->connection, password)) {
		mpdclient_handle_error(c);
		mpdclient_disconnect(c);
		return false;
	}

	return true;
}

bool
mpdclient_update(struct mpdclient *c)
{
	bool retval;

	c->volume = -1;

	if (MPD_ERROR(c))
		return false;

	/* always announce these options as long as we don't have real
	   "idle" support */
	c->events |= MPD_IDLE_PLAYER|MPD_IDLE_OPTIONS;

	/* free the old status */
	if (c->status)
		mpd_status_free(c->status);

	/* retrieve new status */
	c->status = mpd_run_status(c->connection);
	if (c->status == NULL)
		return mpdclient_handle_error(c) == 0;

	if (c->update_id != mpd_status_get_update_id(c->status)) {
		c->events |= MPD_IDLE_UPDATE;

		if (c->update_id > 0)
			c->events |= MPD_IDLE_DATABASE;
	}

	c->update_id = mpd_status_get_update_id(c->status);

	if (c->volume != mpd_status_get_volume(c->status))
		c->events |= MPD_IDLE_MIXER;

	c->volume = mpd_status_get_volume(c->status);

	/* check if the playlist needs an update */
	if (c->playlist.id != mpd_status_get_queue_version(c->status)) {
		c->events |= MPD_IDLE_PLAYLIST;

		if (!playlist_is_empty(&c->playlist))
			retval = mpdclient_playlist_update_changes(c);
		else
			retval = mpdclient_playlist_update(c);
	} else
		retval = true;

	/* update the current song */
	if (!c->song || mpd_status_get_song_id(c->status)) {
		c->song = playlist_get_song(&c->playlist,
					    mpd_status_get_song_pos(c->status));
	}

	return retval;
}


/****************************************************************************/
/*** MPD Commands  **********************************************************/
/****************************************************************************/

gint
mpdclient_cmd_play(struct mpdclient *c, gint idx)
{
	const struct mpd_song *song = playlist_get_song(&c->playlist, idx);

	if (MPD_ERROR(c))
		return -1;

	if (song)
		mpd_send_play_id(c->connection, mpd_song_get_id(song));
	else
		mpd_send_play(c->connection);

	return mpdclient_finish_command(c);
}

gint
mpdclient_cmd_crop(struct mpdclient *c)
{
	struct mpd_status *status;
	bool playing;
	int length, current;

	if (MPD_ERROR(c))
		return -1;

	status = mpd_run_status(c->connection);
	if (status == NULL)
		return mpdclient_handle_error(c);

	playing = mpd_status_get_state(status) == MPD_STATE_PLAY ||
		mpd_status_get_state(status) == MPD_STATE_PAUSE;
	length = mpd_status_get_queue_length(status);
	current = mpd_status_get_song_pos(status);

	mpd_status_free(status);

	if (!playing || length < 2)
		return 0;

	mpd_command_list_begin(c->connection, false);

	while (--length >= 0)
		if (length != current)
			mpd_send_delete(c->connection, length);

	mpd_command_list_end(c->connection);

	return mpdclient_finish_command(c);
}

gint
mpdclient_cmd_shuffle_range(struct mpdclient *c, guint start, guint end)
{
	mpd_send_shuffle_range(c->connection, start, end);
	return mpdclient_finish_command(c);
}

gint
mpdclient_cmd_clear(struct mpdclient *c)
{
	gint retval = 0;

	if (MPD_ERROR(c))
		return -1;

	mpd_send_clear(c->connection);
	retval = mpdclient_finish_command(c);

	if (retval)
		c->events |= MPD_IDLE_PLAYLIST;

	return retval;
}

gint
mpdclient_cmd_volume(struct mpdclient *c, gint value)
{
	if (MPD_ERROR(c))
		return -1;

	mpd_send_set_volume(c->connection, value);
	return mpdclient_finish_command(c);
}

gint mpdclient_cmd_volume_up(struct mpdclient *c)
{
	if (MPD_ERROR(c))
		return -1;

	if (c->status == NULL ||
	    mpd_status_get_volume(c->status) == -1)
		return 0;

	if (c->volume < 0)
		c->volume = mpd_status_get_volume(c->status);

	if (c->volume >= 100)
		return 0;

	return mpdclient_cmd_volume(c, ++c->volume);
}

gint mpdclient_cmd_volume_down(struct mpdclient *c)
{
	if (MPD_ERROR(c))
		return -1;

	if (c->status == NULL || mpd_status_get_volume(c->status) < 0)
		return 0;

	if (c->volume < 0)
		c->volume = mpd_status_get_volume(c->status);

	if (c->volume <= 0)
		return 0;

	return mpdclient_cmd_volume(c, --c->volume);
}

gint
mpdclient_cmd_add_path(struct mpdclient *c, const gchar *path_utf8)
{
	if (MPD_ERROR(c))
		return -1;

	mpd_send_add(c->connection, path_utf8);
	return mpdclient_finish_command(c);
}

gint
mpdclient_cmd_add(struct mpdclient *c, const struct mpd_song *song)
{
	gint retval = 0;

	if (MPD_ERROR(c))
		return -1;

	if (song == NULL)
		return -1;

	/* send the add command to mpd */
	mpd_send_add(c->connection, mpd_song_get_uri(song));
	if( (retval=mpdclient_finish_command(c)) )
		return retval;

#ifdef ENABLE_FANCY_PLAYLIST_MANAGMENT_CMD_ADD
	/* add the song to playlist */
	playlist_append(&c->playlist, song);

	/* increment the playlist id, so we don't retrieve a new playlist */
	c->playlist.id++;

	c->events |= MPD_IDLE_PLAYLIST;
#endif

	return 0;
}

gint
mpdclient_cmd_delete(struct mpdclient *c, gint idx)
{
	gint retval = 0;
	struct mpd_song *song;

	if (MPD_ERROR(c))
		return -1;

	if (idx < 0 || (guint)idx >= playlist_length(&c->playlist))
		return -1;

	song = playlist_get(&c->playlist, idx);

	/* send the delete command to mpd */
	mpd_send_delete_id(c->connection, mpd_song_get_id(song));
	if( (retval=mpdclient_finish_command(c)) )
		return retval;

#ifdef ENABLE_FANCY_PLAYLIST_MANAGMENT_CMD_DELETE
	/* increment the playlist id, so we don't retrieve a new playlist */
	c->playlist.id++;

	/* remove the song from the playlist */
	playlist_remove_reuse(&c->playlist, idx);

	c->events |= MPD_IDLE_PLAYLIST;

	/* remove references to the song */
	if (c->song == song)
		c->song = NULL;

	mpd_song_free(song);
#endif

	return 0;
}

gint
mpdclient_cmd_move(struct mpdclient *c, gint old_index, gint new_index)
{
	gint n;
	struct mpd_song *song1, *song2;

	if (MPD_ERROR(c))
		return -1;

	if (old_index == new_index || new_index < 0 ||
	    (guint)new_index >= c->playlist.list->len)
		return -1;

	song1 = playlist_get(&c->playlist, old_index);
	song2 = playlist_get(&c->playlist, new_index);

	/* send the move command to mpd */
	mpd_send_swap_id(c->connection,
			 mpd_song_get_id(song1), mpd_song_get_id(song2));
	if( (n=mpdclient_finish_command(c)) )
		return n;

#ifdef ENABLE_FANCY_PLAYLIST_MANAGMENT_CMD_MOVE
	/* update the playlist */
	playlist_swap(&c->playlist, old_index, new_index);

	/* increment the playlist id, so we don't retrieve a new playlist */
	c->playlist.id++;
#endif

	c->events |= MPD_IDLE_PLAYLIST;

	return 0;
}

gint
mpdclient_cmd_save_playlist(struct mpdclient *c, const gchar *filename_utf8)
{
	gint retval = 0;

	if (MPD_ERROR(c))
		return -1;

	mpd_send_save(c->connection, filename_utf8);
	if ((retval = mpdclient_finish_command(c)) == 0) {
		c->events |= MPD_IDLE_STORED_PLAYLIST;
	}

	return retval;
}

gint
mpdclient_cmd_load_playlist(struct mpdclient *c, const gchar *filename_utf8)
{
	if (MPD_ERROR(c))
		return -1;

	mpd_send_load(c->connection, filename_utf8);
	return mpdclient_finish_command(c);
}

gint
mpdclient_cmd_delete_playlist(struct mpdclient *c, const gchar *filename_utf8)
{
	gint retval = 0;

	if (MPD_ERROR(c))
		return -1;

	mpd_send_rm(c->connection, filename_utf8);
	if ((retval = mpdclient_finish_command(c)) == 0)
		c->events |= MPD_IDLE_STORED_PLAYLIST;

	return retval;
}


/****************************************************************************/
/*** Playlist management functions ******************************************/
/****************************************************************************/

/* update playlist */
bool
mpdclient_playlist_update(struct mpdclient *c)
{
	struct mpd_entity *entity;

	if (MPD_ERROR(c))
		return false;

	playlist_clear(&c->playlist);

	mpd_send_list_queue_meta(c->connection);
	while ((entity = mpd_recv_entity(c->connection))) {
		if (mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG)
			playlist_append(&c->playlist, mpd_entity_get_song(entity));

		mpd_entity_free(entity);
	}

	c->playlist.id = mpd_status_get_queue_version(c->status);
	c->song = NULL;

	return mpdclient_finish_command(c) == 0;
}

/* update playlist (plchanges) */
bool
mpdclient_playlist_update_changes(struct mpdclient *c)
{
	struct mpd_song *song;
	guint length;

	if (MPD_ERROR(c))
		return false;

	mpd_send_queue_changes_meta(c->connection, c->playlist.id);

	while ((song = mpd_recv_song(c->connection)) != NULL) {
		int pos = mpd_song_get_pos(song);

		if (pos >= 0 && (guint)pos < c->playlist.list->len) {
			/* update song */
			playlist_replace(&c->playlist, pos, song);
		} else {
			/* add a new song */
			playlist_append(&c->playlist, song);
		}

		mpd_song_free(song);
	}

	/* remove trailing songs */

	length = mpd_status_get_queue_length(c->status);
	while (length < c->playlist.list->len) {
		guint pos = c->playlist.list->len - 1;

		/* Remove the last playlist entry */
		playlist_remove(&c->playlist, pos);
	}

	c->song = NULL;
	c->playlist.id = mpd_status_get_queue_version(c->status);

	return mpdclient_finish_command(c) == 0;
}


/****************************************************************************/
/*** Filelist functions *****************************************************/
/****************************************************************************/

struct filelist *
mpdclient_filelist_get(struct mpdclient *c, const gchar *path)
{
	struct filelist *filelist;
	struct mpd_entity *entity;

	if (MPD_ERROR(c))
		return NULL;

	mpd_send_list_meta(c->connection, path);
	filelist = filelist_new();

	while ((entity = mpd_recv_entity(c->connection)) != NULL)
		filelist_append(filelist, entity);

	if (mpdclient_finish_command(c)) {
		filelist_free(filelist);
		return NULL;
	}

	filelist_sort_dir_play(filelist, compare_filelistentry);

	return filelist;
}

static struct filelist *
mpdclient_recv_filelist_response(struct mpdclient *c)
{
	struct filelist *filelist;
	struct mpd_entity *entity;

	filelist = filelist_new();

	while ((entity = mpd_recv_entity(c->connection)) != NULL)
		filelist_append(filelist, entity);

	if (mpdclient_finish_command(c)) {
		filelist_free(filelist);
		return NULL;
	}

	return filelist;
}

struct filelist *
mpdclient_filelist_search(struct mpdclient *c,
			  int exact_match,
			  enum mpd_tag_type tag,
			  gchar *filter_utf8)
{
	if (MPD_ERROR(c))
		return NULL;

	mpd_search_db_songs(c->connection, exact_match);
	mpd_search_add_tag_constraint(c->connection, MPD_OPERATOR_DEFAULT,
				      tag, filter_utf8);
	mpd_search_commit(c->connection);

	return mpdclient_recv_filelist_response(c);
}

int
mpdclient_filelist_add_all(struct mpdclient *c, struct filelist *fl)
{
	guint i;

	if (MPD_ERROR(c))
		return -1;

	if (filelist_is_empty(fl))
		return 0;

	mpd_command_list_begin(c->connection, false);

	for (i = 0; i < filelist_length(fl); ++i) {
		struct filelist_entry *entry = filelist_get(fl, i);
		struct mpd_entity *entity  = entry->entity;

		if (entity != NULL &&
		    mpd_entity_get_type(entity) == MPD_ENTITY_TYPE_SONG) {
			const struct mpd_song *song =
				mpd_entity_get_song(entity);
			const char *uri = mpd_song_get_uri(song);

			if (uri != NULL)
				mpd_send_add(c->connection, uri);
		}
	}

	mpd_command_list_end(c->connection);
	return mpdclient_finish_command(c);
}

GList *
mpdclient_get_artists(struct mpdclient *c)
{
	GList *list = NULL;
	struct mpd_pair *pair;

	if (MPD_ERROR(c))
               return NULL;

	mpd_search_db_tags(c->connection, MPD_TAG_ARTIST);
	mpd_search_commit(c->connection);

	while ((pair = mpd_recv_pair_tag(c->connection,
					 MPD_TAG_ARTIST)) != NULL) {
		list = g_list_append(list, g_strdup(pair->value));
		mpd_return_pair(c->connection, pair);
	}

	if (mpdclient_finish_command(c))
		return string_list_free(list);

	return list;
}

GList *
mpdclient_get_albums(struct mpdclient *c, const gchar *artist_utf8)
{
	GList *list = NULL;
	struct mpd_pair *pair;

	if (MPD_ERROR(c))
               return NULL;

	mpd_search_db_tags(c->connection, MPD_TAG_ALBUM);
	if (artist_utf8 != NULL)
		mpd_search_add_tag_constraint(c->connection,
					      MPD_OPERATOR_DEFAULT,
					      MPD_TAG_ARTIST, artist_utf8);
	mpd_search_commit(c->connection);

	while ((pair = mpd_recv_pair_tag(c->connection,
					 MPD_TAG_ALBUM)) != NULL) {
		list = g_list_append(list, g_strdup(pair->value));
		mpd_return_pair(c->connection, pair);
	}

	if (mpdclient_finish_command(c))
		return string_list_free(list);

	return list;
}

/*
 * Copyright (c) 2012 Alexander Schrijver
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <string.h>

#include <gtk/gtk.h>

#include <sqlite3.h>

#include "abbott.h"

struct gm_driver_conn {
	GIOChannel	*channel;
	size_t		 length;
};

struct gm_generic_conn {
	struct gm_driver_conn	conn;
};

struct gm_state {
	struct gm_generic_conn	*conns[100];
	size_t			nconns;

	sqlite3			*sqlite3_handle;
	GtkTreeModel		*measurements;
};

static gboolean gm_abbott_in(GIOChannel *gio, GIOCondition condition, gpointer data);
static gboolean gm_abbott_out(GIOChannel *gio, GIOCondition condition, gpointer data);
static gboolean gm_abbott_error(GIOChannel *gio, GIOCondition condition, gpointer data);
struct gm_abbott_conn * gm_abbott_conn_init(char *dev);
int gm_abbott_device_init(char *dev);
int gm_process_config(struct gm_state *state);
void meas_change_cb(GtkTreeModel *model, GtkTreePath  *path, GtkTreeIter  *iter, gpointer user_data);
void meas_insert_cb(GtkTreeModel *model, GtkTreePath  *path, GtkTreeIter  *iter, gpointer user_data);
int meas_orm(void *user, int ncolumns, char **columns, char **column_names);

#define GM_MEAS_COL_GLUCOSE 0
#define GM_MEAS_COL_DATE 1
#define GM_MEAS_COL_DEVICE 2
#define GM_MEAS_NUM_COLS 3

void
meas_change_cb(GtkTreeModel *model, GtkTreePath  *path,
	GtkTreeIter  *iter, gpointer user_data)
{
	gchar *date;

	printf("changed!\n");

	gtk_tree_model_get(model, iter, GM_MEAS_COL_DATE, &date, -1);

	printf("date: %s\n", date);

	g_free(date);
}

void
meas_insert_cb(GtkTreeModel *model, GtkTreePath  *path,
	GtkTreeIter  *iter, gpointer user_data)
{
	gchar		*date = NULL, *device = NULL;
	gint		 glucose;
	sqlite3_stmt	*stmt;
	int r;

	stmt = g_object_get_data(G_OBJECT(model), "insert_stmt");
	if (stmt == NULL)
		return;

	gtk_tree_model_get(model, iter, GM_MEAS_COL_GLUCOSE, &glucose, -1);

	r = sqlite3_bind_int(stmt, 1, glucose);
	if (r != SQLITE_OK)
		goto fail;

	gtk_tree_model_get(model, iter, GM_MEAS_COL_DATE, &date, -1);

	r = sqlite3_bind_text(stmt, 2, date, -1, NULL);
	if (r != SQLITE_OK)
		goto fail;

	gtk_tree_model_get(model, iter, GM_MEAS_COL_DEVICE, &device, -1);

	r = sqlite3_bind_text(stmt, 2, device, -1, NULL);
	if (r != SQLITE_OK)
		goto fail;

	r = sqlite3_step(stmt);
	if (r != SQLITE_DONE)
		goto fail;

fail:
	g_free(date);
}

int
meas_orm(void *user, int ncolumns, char **columns, char **column_names)
{
	GtkTreeIter	 iter;
	GtkListStore	*store = user;
	int i;

	gtk_list_store_append(store, &iter);

	for (i = 0; i < ncolumns; i++) {
		if (strcmp(column_names[i], "glucose") == 0) 
			/* XXX: replace atoi with strtonum or use a better
			 * method to receive the sqlite columns */
			gtk_list_store_set(store, &iter, GM_MEAS_COL_GLUCOSE, atoi(columns[i]), -1);
		if (strcmp(column_names[i], "date") == 0)
			gtk_list_store_set(store, &iter, GM_MEAS_COL_DATE, columns[i], -1);
		if (strcmp(column_names[i], "device") == 0)
			gtk_list_store_set(store, &iter, GM_MEAS_COL_DEVICE, columns[i], -1);
	}

	return 0;
}

static GtkTreeModel *
meas_model(struct gm_state *state)
{
	GtkListStore	*store;
	GtkTreeIter	 iter;
	int		 r;
	char		*errmsg;
	sqlite3_stmt	*insert_stmt, *change_stmt, *delete_stmt;
	const char	*sql_tail;

	r = sqlite3_exec(state->sqlite3_handle, "CREATE TABLE IF NOT EXISTS measurements " \
		" (id INTEGER PRIMARY KEY, glucose INTEGER, date DATETIME, device VARCHAR(255), " \
		" UNIQUE (glucose, date, device))", NULL, NULL, &errmsg);
	if (r != SQLITE_OK) {
		return NULL;
	}

	store = gtk_list_store_new(GM_MEAS_NUM_COLS, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);

	/* Fill the measurement store with entries from the database */
	r = sqlite3_exec(state->sqlite3_handle, "SELECT * from measurements",
		meas_orm, store, &errmsg);
	if (r != SQLITE_OK) {
		return NULL;
	}

	r = sqlite3_prepare_v2(state->sqlite3_handle, "INSERT INTO measurements VALUES " \
		" (?, ?, ?);", -1, &insert_stmt, &sql_tail);
	if (r != SQLITE_OK) {
		return NULL;
	}

	g_object_set_data_full(G_OBJECT(store), "insert_stmt", insert_stmt, g_object_unref);

	g_object_set_data_full(G_OBJECT(store), "change_stmt", change_stmt, g_object_unref);
	g_object_set_data_full(G_OBJECT(store), "delete_stmt", delete_stmt, g_object_unref);

	g_signal_connect(store, "row-changed", G_CALLBACK(meas_insert_cb), state);
	g_signal_connect(store, "row-inserted", G_CALLBACK(meas_change_cb), state);

	return GTK_TREE_MODEL(store);
}

static GtkWidget *
glucose_listview(GtkTreeModel *model)
{
	GtkWidget *view;
	GtkCellRenderer *renderer;

	view = gtk_tree_view_new();

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1,
		"Date", renderer, "text", GM_MEAS_COL_DATE, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1,
		"Glucose", renderer, "text", GM_MEAS_COL_GLUCOSE, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1,
		"Device", renderer, "text", GM_MEAS_COL_DEVICE, NULL);

	gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);

	g_object_unref(model);

	return view;
}

#define GM_MENU_COL_NAME 0
#define GM_MENU_NUM_COLS 2

static GtkTreeModel *
menu_listmodel(void)
{
	GtkListStore	*store;
	GtkTreeIter	iter;
  
	store = gtk_list_store_new(GM_MENU_NUM_COLS, G_TYPE_STRING, G_TYPE_UINT);

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, GM_MENU_COL_NAME, "bla", -1);

	return GTK_TREE_MODEL(store);
}


static gboolean
menu_listview_select(GtkTreeSelection *selection, GtkTreeModel *model,
	GtkTreePath *path, gboolean path_currently_selected, gpointer userdata)
{
	GtkTreeIter	 iter;
	gchar		*name;

	if(!gtk_tree_model_get_iter(model, &iter, path)) {
		return TRUE;
	}

	gtk_tree_model_get(model, &iter, GM_MENU_COL_NAME, &name, -1);

	if(!path_currently_selected)
		g_print("%s is going to be selected.\n", name);
	else
		g_print("%s is going to be unselected.\n", name);

	g_free(name);

	return TRUE;
}

static GtkWidget *
menu_listview(void)
{
	GtkWidget *view;
	GtkCellRenderer *renderer;
	GtkTreeModel	*model;
	GtkTreeSelection  *selection;

	view = gtk_tree_view_new();

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
	gtk_tree_selection_set_select_function(selection, menu_listview_select, NULL, NULL);

	renderer = gtk_cell_renderer_text_new();

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1,
		"Item", renderer, "text", GM_MENU_COL_NAME, NULL);

	model = menu_listmodel();
	gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);

	g_object_unref(model);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

	return view;
}

struct gm_dummy_conn {
	struct gm_driver_conn	conn;
};

gboolean
gm_dummy_cb(gpointer user)
{
	struct gm_state *state = user;
	GtkTreeModel *store = state->measurements;
	GtkTreeIter	 iter;

	gtk_list_store_append(store, &iter);

	gtk_list_store_set(store, &iter, GM_MEAS_COL_GLUCOSE, 100, -1);
	gtk_list_store_set(store, &iter, GM_MEAS_COL_DATE, "date", -1);
	gtk_list_store_set(store, &iter, GM_MEAS_COL_DEVICE, "device", -1);

	return FALSE;
}

struct gm_dummy_conn *
gm_dummy_conn_init(struct gm_state *state)
{
	struct gm_dummy_conn *conn;

	conn = calloc(1, sizeof(*conn));
	if (conn == NULL) {
		return NULL;
	}

	g_timeout_add_seconds(10, gm_dummy_cb, state);

	return conn;
}

struct gm_abbott_conn {
	struct gm_driver_conn	conn;
	/*
	 * Send MEM
	 * Device type
	 * Software revision
	 * CurrentDateTime
	 * Number of results
	 * Resultline
	 * END
	 * EMPTY
	 *
	 */
	enum abbott_protocol_state {
		ABBOTT_SEND_MEM,
		ABBOTT_DEVICE_TYPE,
		ABBOTT_SOFTWARE_REVISION,
		ABBOTT_CURRENTDATETIME,
		ABBOTT_NUMBEROFRESULTS,
		ABBOTT_RESULTLINE,
		ABBOTT_END,
		ABBOTT_EMPTY,
	} protocol_state;
	uint16_t checksum;
};

int
gm_abbott_device_init(char *dev)
{
	int fd;
        struct termios ts;

	fd = open(dev, O_RDWR | O_NONBLOCK | O_NOCTTY);
	if (fd < 0) {
		perror("open");
		return fd;
	}

        tcgetattr(fd, &ts);
        tcflush(fd, TCIFLUSH);

	int r;
        r = tcflush(fd, TCOFLUSH);
	if (r == -1) {
		perror("tcflush");
	}

        bzero(&ts, sizeof(ts));

        ts.c_lflag = 0;
        ts.c_cflag = B19200 | CS8 |CREAD | CLOCAL | CRTSCTS;
	ts.c_cc[VTIME] = 5;
	ts.c_cc[VMIN] = 5;

        cfsetospeed(&ts, B19200);
        tcsetattr(fd, TCSANOW, &ts);

	return fd;
}

struct gm_abbott_conn *
gm_abbott_conn_init(char *dev)
{
	int		 fd;
	guint		 r;
	GIOChannel	*channel;
	struct gm_abbott_conn *conn;

	conn = calloc(1, sizeof(*conn));
	if (conn == NULL) {
		return NULL;
	}

	fd = gm_abbott_device_init(dev);
	if (fd < 0) {
		fprintf(stderr, "Cannnot open device\n");

		free(conn);
		return NULL;
	}

	channel = g_io_channel_unix_new(fd);
	if (channel == NULL) {
		g_error("Cannnot create GIOChannel");

		free(conn);
		return NULL;
	}

	r = g_io_add_watch(channel, G_IO_IN | G_IO_HUP, gm_abbott_in, conn);
	if (!r) {
		g_error("Cannnot watch GIOChannel");

		goto fail;	
	}

	r = g_io_add_watch(channel, G_IO_OUT | G_IO_HUP, gm_abbott_out, conn);
	if (!r) {
		g_error("Cannnot watch GIOChannel");
		goto fail;
	}

	r = g_io_add_watch(channel, G_IO_ERR | G_IO_HUP, gm_abbott_error, conn);
	if (!r) {
		g_error("Cannnot watch GIOChannel");
		goto fail;
	}

	return conn;

fail:
	/* XXX: free channel and destroy watch */
	free(conn);
	return NULL;
	
}

static gboolean
gm_abbott_in(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	GIOStatus	 status;
	gchar		*line;
	gsize		 line_length, terminator_pos;
	GError		*error = NULL;
	guint		 r;
	struct gm_abbott_conn *abbott_state = data;

	printf("in\n");

	status = g_io_channel_read_line(gio, &line, &line_length, &terminator_pos, &error);

	if (status == G_IO_STATUS_ERROR) {
		printf("error occured\n");
	}
	if (status == G_IO_STATUS_AGAIN) {
		printf("resource temp unavail.\n");
	}

	/* skip emtpy lines */
	if (terminator_pos == 0)
		goto skip;

	if (status == G_IO_STATUS_NORMAL) {

		switch(abbott_state->protocol_state) {
			case ABBOTT_DEVICE_TYPE: {
				enum abbott_devicetype device_type;
				printf("device_type");

				device_type = abbott_devicetype(line);
				printf("device_type: %d\n", device_type);

				abbott_state->protocol_state++;
			}
				break;
			case ABBOTT_SOFTWARE_REVISION:
				printf("software_revision");
				abbott_state->protocol_state++;
				break;
			case ABBOTT_CURRENTDATETIME:
				printf("currentdatetime");
				abbott_state->protocol_state++;
				break;
			case ABBOTT_NUMBEROFRESULTS:
				printf("numberofresults");
				abbott_state->protocol_state++;
				break;
			case ABBOTT_RESULTLINE:
				break;
			case ABBOTT_END:
				break;
			case ABBOTT_EMPTY:
				break;

			default:
				/* XXX: this shouldn't happen */
				break;
		}

		printf("normal; line: \"%s\"\n", line);
	}

skip:
	if (status != G_IO_STATUS_EOF) {
		r = g_io_add_watch(gio, G_IO_IN | G_IO_HUP, gm_abbott_in, abbott_state);
		if (!r)
			g_error("Cannnot watch GIOChannel");
	}


	return TRUE;
}

static gboolean
gm_abbott_out(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	gchar		 buf[] = "mem";
	gsize		 wrote_len;
	GError		*error = NULL;
	GIOStatus	 status;
	struct gm_abbott_conn *abbott_state = data;

	if (abbott_state->protocol_state != ABBOTT_SEND_MEM) {
		return FALSE;
	}

	printf("out\n");

	status = g_io_channel_write_chars(gio, buf, -1, &wrote_len, &error);

	g_io_channel_flush(gio, NULL);

	printf("bytes written: %zu status: %d\n", wrote_len, status);

	abbott_state->protocol_state++;

	return TRUE;
}

static gboolean
gm_abbott_error(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	printf("error\n");

	return TRUE;
}

#if 0
int
progress_dialog_new(void)
{
	GtkWidget *dialog, *progressbar;

	progressbar = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction((gpointer)progressbar, 0.5);

	dialog = gtk_dialog_new();

	gtk_window_set_title(GTK_WINDOW(dialog), "Error");

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), progressbar,
		TRUE, TRUE, 0);

	gtk_widget_show(progressbar);

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}
#endif

/*
 * Method which should process the configuration files. But this code hasn't
 * been written yet.
 */
int
gm_process_config(struct gm_state *state)
{
	struct gm_abbott_conn	*abbott_conn;
	struct gm_dummy_conn	*dummy_conn;

#if 0
	abbott_conn = gm_abbott_conn_init("/dev/ttyU0");
	if (abbott_conn == NULL) {
		return -1;
	}

	state->conns[state->nconns++] = (struct gm_generic_conn *)abbott_conn;
#else
	dummy_conn = gm_dummy_conn_init(state);
	if (dummy_conn == NULL) {
		return -1;
	}

	state->conns[state->nconns++] = (struct gm_generic_conn *)dummy_conn;
#endif

	return 0;
}

static gboolean
gm_delete_cb(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	return TRUE;
}

static void
gm_destroy_cb(GtkWidget *widget, gpointer data)
{
	gtk_main_quit();
}

int
main(int argc, char *argv[])
{
	GtkWidget	*window, *view, *menu, *hpaned;
	GMainLoop	*loop;
	GtkTreeModel	*glucose_model;
	struct gm_state  state;
	int r;

	bzero(&state, sizeof(state));

	r = sqlite3_open("database.sqlite3", &state.sqlite3_handle);
	if (r != SQLITE_OK) {
		// XXX: free handle;
		return -1;
	}

	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(window, "delete-event", G_CALLBACK(gm_delete_cb), NULL);
	g_signal_connect(window, "destroy", G_CALLBACK(gm_destroy_cb), NULL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);
	gtk_widget_set_size_request(GTK_WIDGET(window), 450, 400);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);

	glucose_model = meas_model(&state);

	view = glucose_listview(glucose_model);

	menu = menu_listview();

	hpaned = gtk_hpaned_new();

	gtk_paned_add1(GTK_PANED(hpaned), menu);
	gtk_paned_add2(GTK_PANED(hpaned), view);

	gtk_container_add(GTK_CONTAINER(window), hpaned);

	gtk_widget_show_all(window);

	loop = g_main_loop_new(NULL, TRUE);

	if (gm_process_config(&state) == -1)
		exit(EXIT_FAILURE);

	g_main_loop_run(loop);

	return 0;
}

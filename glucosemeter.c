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

#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sqlite3.h>
#include <termios.h>
#include <unistd.h>

#include <sys/queue.h>

#include <gtk/gtk.h>


#include "abbott.h"

#ifdef DEBUG
#define DPRINTF(x) do { printf x; } while(0)
#else
#define DPRINTF(x)
#endif

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

struct gm_dummy_conn {
	struct gm_driver_conn	conn;
};

struct gm_abbott_entry {
	struct abbott_entry	abbott_entry;

	SLIST_ENTRY(gm_abbott_entry) next;
};


struct gm_abbott_conn {
	struct gm_driver_conn	 conn;
	struct gm_state		*gm_state;
	enum abbott_protocol_state {
		ABBOTT_SEND_MEM,
		ABBOTT_DEVICE_TYPE,
		ABBOTT_SOFTWARE_REVISION,
		ABBOTT_CURRENTDATETIME,
		ABBOTT_NUMBEROFRESULTS,
		ABBOTT_RESULTLINE,
		ABBOTT_END,
		ABBOTT_EMPTY,
		ABBOTT_FAIL,
	} protocol_state;
	uint16_t checksum;
	int nresults;
	int results_processed;
	SLIST_HEAD(, gm_abbott_entry) entries;
};

gboolean gm_dummy_cb(gpointer user);
struct gm_dummy_conn *gm_dummy_conn_init(struct gm_state *state);
int gm_process_config(struct gm_state *state);
void gm_refresh(GtkToolButton *button, gpointer user);

int meas_insert(struct gm_state *state, int glucose, char *date, char *device);
static GtkTreeModel *meas_model(struct gm_state *state);
int meas_model_fill(struct gm_state *state, GtkListStore *store);

static gboolean gm_abbott_in(GIOChannel *gio, GIOCondition condition, gpointer data);
static gboolean gm_abbott_out(GIOChannel *gio, GIOCondition condition, gpointer data);
static gboolean gm_abbott_error(GIOChannel *gio, GIOCondition condition, gpointer data);
static struct gm_abbott_conn * gm_abbott_conn_init(struct gm_state *state, char *dev);
static int gm_abbott_device_init(char *dev);
static void gm_abbott_parsedev(struct gm_abbott_conn *conn, char *line);
static void gm_abbott_parsesoft(struct gm_abbott_conn *conn, char *line);
static void gm_abbott_parsedate(struct gm_abbott_conn *conn, char *line);
static void gm_abbott_parsenresults(struct gm_abbott_conn *conn, char *line);
static void gm_abbott_parseresult(struct gm_abbott_conn *conn, char *line);
static void gm_abbott_parseend(struct gm_abbott_conn *conn, char *line);
static void gm_abbott_parseempty(struct gm_abbott_conn *conn, char *line);
static void gm_abbott_parseline(struct gm_abbott_conn *conn, char *line);

#define GM_MEAS_COL_GLUCOSE 0
#define GM_MEAS_COL_DATE 1
#define GM_MEAS_COL_DEVICE 2
#define GM_MEAS_NUM_COLS 3

int
meas_insert(struct gm_state *state, int glucose, char *date, char *device)
{
	int		 r;
	sqlite3_stmt    *stmt;
	const char      *sql_tail;

	r = sqlite3_prepare_v2(state->sqlite3_handle, "INSERT OR IGNORE INTO measurements VALUES " \
		" (?, ?, ?);", -1, &stmt, &sql_tail);
	if (r != SQLITE_OK) {
		return -1;
	}

	r = sqlite3_bind_int(stmt, 1, glucose);
	if (r != SQLITE_OK)
		goto fail;

	r = sqlite3_bind_text(stmt, 2, date, -1, NULL);
	if (r != SQLITE_OK)
		goto fail;

	r = sqlite3_bind_text(stmt, 3, device, -1, NULL);
	if (r != SQLITE_OK)
		goto fail;

	r = sqlite3_step(stmt);
	if (r != SQLITE_DONE)
		goto fail;

	sqlite3_finalize(stmt);

	meas_model_fill(state, GTK_LIST_STORE(state->measurements));

	return 0;
fail:
	sqlite3_finalize(stmt);

	return -1;
}

int
meas_model_fill(struct gm_state *state, GtkListStore *store)
{
	GtkTreeIter	 iter;
	int		 r;
	sqlite3_stmt	*stmt;
	const char	*sql_tail;

	gtk_list_store_clear(store);

	/* Fill the measurement store with entries from the database */
	r = sqlite3_prepare_v2(state->sqlite3_handle,
			"SELECT glucose, date, device from measurements", -1, &stmt, &sql_tail);
	if (r != SQLITE_OK)
		goto fail;

	while((r = sqlite3_step(stmt)) == SQLITE_ROW) {
		int glucose;
		const unsigned char *date, *device;

		gtk_list_store_append(store, &iter);

		glucose = sqlite3_column_int(stmt, 0);
		gtk_list_store_set(store, &iter, GM_MEAS_COL_GLUCOSE, glucose, -1);

		date = sqlite3_column_text(stmt, 1);
		gtk_list_store_set(store, &iter, GM_MEAS_COL_DATE, date, -1);

		device = sqlite3_column_text(stmt, 2);
		gtk_list_store_set(store, &iter, GM_MEAS_COL_DEVICE, device, -1);

	}

	if (r != SQLITE_DONE)
		goto fail;

	return 0;
fail:
	return -1;
}

static GtkTreeModel *
meas_model(struct gm_state *state)
{
	GtkListStore	*store;
	int		 r;
	char		*errmsg;

	r = sqlite3_exec(state->sqlite3_handle, "CREATE TABLE IF NOT EXISTS measurements " \
		" (glucose INTEGER, date DATETIME, device VARCHAR(255), " \
		" UNIQUE (glucose, date, device))", NULL, NULL, &errmsg);
	if (r != SQLITE_OK) {
		return NULL;
	}

	store = gtk_list_store_new(GM_MEAS_NUM_COLS, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);

	r = meas_model_fill(state, store);
	if (r == -1)
		goto fail;

	return GTK_TREE_MODEL(store);
fail:
	g_object_unref(store);

	return NULL;
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

gboolean
gm_dummy_cb(gpointer user)
{
	struct gm_state *state = user;

	meas_insert(state, 100, "2012-11-17 13:13", "dummy");

	return TRUE;
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

static int
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

static struct gm_abbott_conn *
gm_abbott_conn_init(struct gm_state *state, char *dev)
{
	int		 fd;
	guint		 r;
	GIOChannel	*channel;
	struct gm_abbott_conn *conn;

	conn = calloc(1, sizeof(*conn));
	if (conn == NULL) {
		return NULL;
	}

	SLIST_INIT(&conn->entries);
	conn->gm_state = state;

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

static void
gm_abbott_parseline(struct gm_abbott_conn *conn, char *line)
{
	int old_state = conn->protocol_state;

	switch(conn->protocol_state) {
		case ABBOTT_DEVICE_TYPE:
			gm_abbott_parsedev(conn, line);
			break;
		case ABBOTT_SOFTWARE_REVISION:
			gm_abbott_parsesoft(conn, line);
			break;
		case ABBOTT_CURRENTDATETIME:
			gm_abbott_parsedate(conn, line);
			break;
		case ABBOTT_NUMBEROFRESULTS:
			gm_abbott_parsenresults(conn, line);
			break;
		case ABBOTT_RESULTLINE:
			gm_abbott_parseresult(conn, line);
			break;
		case ABBOTT_END:
			gm_abbott_parseend(conn, line);
			break;
		case ABBOTT_EMPTY:
			gm_abbott_parseempty(conn, line);
			break;
		default:
			/* XXX: this shouldn't happen */
			break;
	}

	DPRINTF(("%s: state: %d -> %d\n", __func__, old_state, conn->protocol_state));
}

static void
gm_abbott_parsedev(struct gm_abbott_conn *conn, char *line)
{
	enum abbott_devicetype device_type;

	device_type = abbott_devicetype(line);
	DPRINTF(("%s: device_type: %d\n", __func__, device_type));

	/* Don't continue parsing if the device type isn't known. */
	if (device_type == ABBOTT_DEV_UNKNOWN)
		conn->protocol_state = ABBOTT_FAIL;
	else
		conn->protocol_state++;
}

static void
gm_abbott_parsesoft(struct gm_abbott_conn *conn, char *line)
{
	enum abbott_softwarerevision softrev;

	softrev = abbott_softrev(line);
	DPRINTF(("%s: softrev: %d\n", __func__, softrev));

	/* Don't continue parsing if the software revision isn't known. */
	if (softrev == ABBOTT_SOFT_UNKNOWN)
		conn->protocol_state = ABBOTT_FAIL;
	else
		conn->protocol_state++;
}

static void
gm_abbott_parsedate(struct gm_abbott_conn *conn, char *line)
{
	struct tm device_tm;	
	int r;

	r = abbott_parsetime(line, &device_tm);
	if (r == -1) {
		conn->protocol_state = ABBOTT_FAIL;
		return;
	}

	DPRINTF(("%s: currentdatetime\n", __func__));
	conn->protocol_state++;

	return;
}

static void
gm_abbott_parsenresults(struct gm_abbott_conn *conn, char *line)
{
	int nresults;

	nresults = abbott_nentries(line);
	if (nresults == -1)
		conn->protocol_state = ABBOTT_FAIL;
	else
		conn->protocol_state++;

	conn->nresults = nresults;

	DPRINTF(("%s: numberofresults\n", __func__));

	return;
}

static void
gm_abbott_parseresult(struct gm_abbott_conn *conn, char *line)
{
	int r;
	struct gm_abbott_entry *entry;

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		/* XXX: change to a fail state */
		return;
	}

	r = abbott_parse_entry(line, &entry->abbott_entry);

	/* We can't insert the entry into the database at this point because
	 * the checksum is calculated over all the messages thus we aren't sure
	 * yet if this entry is correct. Instead put all the entries in a
	 * linked list and insert them when the checksum can been verified. */
	SLIST_INSERT_HEAD(&conn->entries, entry, next);

	DPRINTF(("%s: glucose: %d\n", __func__, entry->abbott_entry.bloodglucose));
	DPRINTF(("%s: month: %d\n", __func__, entry->abbott_entry.ptm.tm_mon));
	DPRINTF(("%s: day: %d\n", __func__, entry->abbott_entry.ptm.tm_mday));
	DPRINTF(("%s: year: %d\n", __func__, entry->abbott_entry.ptm.tm_year));
	DPRINTF(("%s: hour: %d\n", __func__, entry->abbott_entry.ptm.tm_hour));
	DPRINTF(("%s: min: %d\n", __func__, entry->abbott_entry.ptm.tm_min));

	conn->results_processed++;
	if (conn->results_processed >= conn->nresults)
		conn->protocol_state = ABBOTT_END;

	DPRINTF(("%s: result\n", __func__));
}

static void
gm_abbott_parseend(struct gm_abbott_conn *conn, char *line)
{
	uint16_t checksum;
	int r;

	r = abbott_parse_checksum(line, &checksum);
	if (r == -1)
		return;

	if (conn->checksum == checksum) {
		struct gm_abbott_entry *e;

		/* We are as sure as we can get that the entries are correct.
		 * Insert them into the database */
		while (!SLIST_EMPTY(&conn->entries)) {
			e = SLIST_FIRST(&conn->entries);
			SLIST_REMOVE_HEAD(&conn->entries, next);

			meas_insert(conn->gm_state,
				e->abbott_entry.bloodglucose,
				asctime(&e->abbott_entry.ptm), "abbott");

			free(e);
		}

		DPRINTF(("%s: checksum verified!\n", __func__));
	}
}

static void
gm_abbott_parseempty(struct gm_abbott_conn *conn, char *line)
{
}

static gboolean
gm_abbott_in(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	GIOStatus	 status;
	gsize		 terminator_pos;
	GError		*error = NULL;
	struct gm_abbott_conn *abbott_state = data;
	GString		*line;

	if (abbott_state->protocol_state == ABBOTT_FAIL) {
		/* XXX: stop processing */
	}

	line = g_string_sized_new(100);

	status = g_io_channel_read_line_string(gio, line, &terminator_pos, &error);

	if (status == G_IO_STATUS_ERROR) {
		DPRINTF(("%s: error occured\n", __func__));
		/* XXX: stop processing */
	} else if (status == G_IO_STATUS_AGAIN) {
		DPRINTF(("%s: resource temp unavail.\n", __func__));
	} else if (status == G_IO_STATUS_NORMAL) {
		/* Calculate the checksum before the newline terminators are cut off */
		if (abbott_state->protocol_state != ABBOTT_END)
			abbott_state->checksum += abbott_calc_checksum(line->str);

		/* Cut off the newline terminators */
		line = g_string_truncate(line, terminator_pos);

		DPRINTF(("%s: line(%zu): \"%s\"\n", __func__, line->len, line->str));

		if (line->len > 0)
			gm_abbott_parseline(abbott_state, line->str);
	}

	g_string_free(line, TRUE);

	if (status == G_IO_STATUS_EOF)
		return FALSE;

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

	status = g_io_channel_write_chars(gio, buf, -1, &wrote_len, &error);

	g_io_channel_flush(gio, NULL);

	DPRINTF(("%s: bytes written: %zu status: %d\n", __func__, wrote_len, status));

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

#if 1
	struct gm_abbott_conn	*abbott_conn;

	abbott_conn = gm_abbott_conn_init(state, "/dev/ttyU0");
	if (abbott_conn == NULL) {
		return -1;
	}

	state->conns[state->nconns++] = (struct gm_generic_conn *)abbott_conn;
#else
	struct gm_dummy_conn	*dummy_conn;

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

void
gm_refresh(GtkToolButton *button, gpointer user)  
{
	struct gm_state *state = user;

	meas_model_fill(state, GTK_LIST_STORE(state->measurements));

	printf("refresh\n");
}

int
main(int argc, char *argv[])
{
	GtkWidget	*window, *view, *toolbar, *vpaned, *scrollview;
	GtkToolItem	*refresh;
	GMainLoop	*loop;
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

	state.measurements = meas_model(&state);

	view = glucose_listview(state.measurements);

	scrollview = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(scrollview, GTK_POLICY_AUTOMATIC,
		GTK_POLICY_AUTOMATIC);
	gtk_container_add(scrollview, view);

	toolbar = gtk_toolbar_new();

	refresh = gtk_tool_button_new_from_stock(GTK_STOCK_REFRESH);
	g_signal_connect(refresh, "clicked", G_CALLBACK(gm_refresh), &state); 
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), refresh, -1);

	vpaned = gtk_vpaned_new();

	gtk_paned_add1(GTK_PANED(vpaned), toolbar);
	gtk_paned_add2(GTK_PANED(vpaned), scrollview);

	gtk_container_add(GTK_CONTAINER(window), vpaned);

	gtk_widget_show_all(window);

	loop = g_main_loop_new(NULL, TRUE);

	if (gm_process_config(&state) == -1)
		exit(EXIT_FAILURE);

	g_main_loop_run(loop);

	return 0;
}

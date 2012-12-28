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

#include "glucosemeter.h"

void			 gm_refresh(GtkToolButton *button, gpointer user);

#define GM_MEAS_COL_GLUCOSE 0
#define GM_MEAS_COL_DATE 1
#define GM_MEAS_COL_DEVICE 2
#define GM_MEAS_NUM_COLS 3

int
meas_insert(struct gm_conf *conf, int glucose, char *date, char *device)
{
	int		 r;
	sqlite3_stmt    *stmt;
	const char      *sql_tail;

	r = sqlite3_prepare_v2(conf->sqlite3_handle, "INSERT OR IGNORE INTO measurements VALUES " \
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

	meas_model_fill(conf, GTK_LIST_STORE(conf->measurements));

	return 0;
fail:
	sqlite3_finalize(stmt);

	return -1;
}

int
meas_model_fill(struct gm_conf *conf, GtkListStore *store)
{
	GtkTreeIter	 iter;
	int		 r;
	sqlite3_stmt	*stmt;
	const char	*sql_tail;

	gtk_list_store_clear(store);

	/* Fill the measurement store with entries from the database */
	r = sqlite3_prepare_v2(conf->sqlite3_handle,
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

GtkTreeModel *
meas_model(struct gm_conf *conf)
{
	GtkListStore	*store;
	int		 r;
	char		*errmsg;

	r = sqlite3_exec(conf->sqlite3_handle, "CREATE TABLE IF NOT EXISTS measurements " \
		" (glucose INTEGER, date DATETIME, device VARCHAR(255), " \
		" UNIQUE (glucose, date, device))", NULL, NULL, &errmsg);
	if (r != SQLITE_OK) {
		return NULL;
	}

	store = gtk_list_store_new(GM_MEAS_NUM_COLS, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);

	r = meas_model_fill(conf, store);
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
	struct gm_conf *conf = user;

	meas_model_fill(conf, GTK_LIST_STORE(conf->measurements));

	printf("refresh\n");
}

int
main(int argc, char *argv[])
{
	GtkWidget	*window, *view, *toolbar, *vpaned, *scrollview;
	GtkToolItem	*refresh;
	GMainLoop	*loop;
	struct gm_conf	 conf;
	int r;

	struct abfr_dev *dev;

	devicemgmt_init(&conf);
	gtk_init(&argc, &argv);

	if ((dev = calloc(1, sizeof(*dev))) == NULL) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}
	dev->file = strdup("/dev/ttyU0");
	dev->device.driver = &abfr_driver;
	dev->device.conf = &conf;

	TAILQ_INSERT_TAIL(&conf.devices, (struct device *)dev, entry);

	devicemgmt_start(&conf);

	r = sqlite3_open("database.sqlite3", &conf.sqlite3_handle);
	if (r != SQLITE_OK) {
		// XXX: free handle;
		return -1;
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(window, "delete-event", G_CALLBACK(gm_delete_cb), NULL);
	g_signal_connect(window, "destroy", G_CALLBACK(gm_destroy_cb), NULL);

	conf.measurements = meas_model(&conf);

	view = glucose_listview(conf.measurements);

	scrollview = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(scrollview, GTK_POLICY_AUTOMATIC,
		GTK_POLICY_AUTOMATIC);
	gtk_container_add(scrollview, view);

	toolbar = gtk_toolbar_new();

	refresh = gtk_tool_button_new_from_stock(GTK_STOCK_REFRESH);
	g_signal_connect(refresh, "clicked", G_CALLBACK(gm_refresh), &conf); 
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), refresh, -1);

	vpaned = gtk_vpaned_new();

	gtk_paned_add1(GTK_PANED(vpaned), toolbar);
	gtk_paned_add2(GTK_PANED(vpaned), scrollview);

	gtk_container_add(GTK_CONTAINER(window), vpaned);

	gtk_widget_show_all(window);

	loop = g_main_loop_new(NULL, TRUE);

	g_main_loop_run(loop);

	return 0;
}

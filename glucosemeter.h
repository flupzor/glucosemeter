/*
 * Copyright (c) 2009, 2012 Alexander Schrijver
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

#include <sqlite3.h>

#include <sys/queue.h>

#include <gtk/gtk.h>

/* glucosemeter.c */

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

int meas_insert(struct gm_state *state, int glucose, char *date, char *device);
static GtkTreeModel *meas_model(struct gm_state *state);
int meas_model_fill(struct gm_state *state, GtkListStore *store);


/* abfr.c */

#define ABFR_MAX_ENTRIES	450

// XXX: do these include the NULL terminator?
#define ABFR_ENTRYLEN	31
#define ABFR_TIMELEN	16

enum abfr_devicetype {
	ABFR_DEV_UNKNOWN,
	ABFR_DEV_CDMK311_B0764, // FreeStyle Freedom Lite
	ABFR_DEV_DAMH359_63524, // FreeStyle Mini
	ABFR_DEV_DBMN169_C4824  // FreeStyle Lite
};

enum abfr_softwarerevision {
	ABFR_SOFT_UNKNOWN,
	ABFR_SOFT_4_0100_P,
	ABFR_SOFT_0_31_P1_B0764,
	ABFR_SOFT_0_31_P, /* XXX: */
	ABFR_SOFT_1_43_P,
};

struct abfr_entry {
	int		bloodglucose;
	struct tm	ptm;
	int		plasmatype;
};

struct gm_abfr_entry {
	struct abfr_entry	abfr_entry;

	SLIST_ENTRY(gm_abfr_entry) next;
};


struct gm_abfr_conn {
	struct gm_driver_conn	 conn;
	struct gm_state		*gm_state;
	enum abfr_protocol_state {
		ABFR_SEND_MEM,
		ABFR_DEVICE_TYPE,
		ABFR_SOFTWARE_REVISION,
		ABFR_CURRENTDATETIME,
		ABFR_NUMBEROFRESULTS,
		ABFR_RESULTLINE,
		ABFR_END,
		ABFR_EMPTY,
		ABFR_FAIL,
	} protocol_state;
	uint16_t checksum;
	int nresults;
	int results_processed;
	SLIST_HEAD(, gm_abfr_entry) entries;
};

int	 abfr_parsetime(char *p, struct tm *r);
enum abfr_devicetype abfr_parsedev(char *type);
enum abfr_softwarerevision abfr_parsesoft(char *rev);
int abfr_nentries(char *);
int abfr_parse_entry(char *p, struct abfr_entry *entry);
uint16_t abfr_calc_checksum(char *line);
int abfr_parse_checksum(char *line, uint16_t *checksum);

gboolean gm_abfr_in(GIOChannel *gio, GIOCondition condition, gpointer data);
gboolean gm_abfr_out(GIOChannel *gio, GIOCondition condition, gpointer data);
gboolean gm_abfr_error(GIOChannel *gio, GIOCondition condition, gpointer data);
struct gm_abfr_conn * gm_abfr_conn_init(struct gm_state *state, char *dev);

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
struct device;
struct gm_conf {
	TAILQ_HEAD(, device)	 devices;
	int			 devicemgmt_status;
	sqlite3			*sqlite3_handle;
	GtkTreeModel		*measurements;
};

int		 meas_insert(struct gm_conf *conf, int glucose, char *date, char *device);
GtkTreeModel	*meas_model(struct gm_conf *conf);
int		 meas_model_fill(struct gm_conf *conf, GtkListStore *store);

/* devicemgmt.c */
struct driver;
struct device {
	struct driver	*driver;
	GIOChannel	*channel;
	struct gm_conf	*conf;
	size_t		 length;
	TAILQ_ENTRY(device)	 entry;
};

struct driver {
	char *driver_name;
	int (*driver_start_fn)(struct device *);
	int (*driver_stop_fn)(struct device *);

	int (*driver_input)(struct device *, GIOChannel *gio);
	int (*driver_output)(struct device *, GIOChannel *gio);
	int (*driver_error)(struct device *, GIOChannel *gio);
};

void devicemgmt_init(struct gm_conf *);
void devicemgmt_start(struct gm_conf *);
void devicemgmt_stop(struct gm_conf *);
int devicemgmt_status(struct gm_conf *);

gboolean devicemgmt_input(GIOChannel *gio, GIOCondition condition, gpointer data);
gboolean devicemgmt_output(GIOChannel *gio, GIOCondition condition, gpointer data);
gboolean devicemgmt_error(GIOChannel *gio, GIOCondition condition, gpointer data);

/* abfr.c */
#define ABFR_MAX_ENTRIES	450

// XXX: do these include the NULL terminator?
#define ABFR_ENTRYLEN	31
#define ABFR_TIMELEN	16

extern struct driver abfr_driver;

enum abfr_devtype {
	ABFR_DEV_UNKNOWN,
	ABFR_DEV_CDMK311_B0764, // FreeStyle Freedom Lite
	ABFR_DEV_DAMH359_63524, // FreeStyle Mini
	ABFR_DEV_DBMN169_C4824  // FreeStyle Lite
};

enum abfr_softrev {
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

	SLIST_ENTRY(abfr_entry) next;
};

struct abfr_dev {
	struct device			 device;
	char				*file;
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
		ABFR_DONE,
	}				 protocol_state;
	uint16_t			 checksum;
	int				 nresults;
	int				 results_processed;
	SLIST_HEAD(, abfr_entry)	 entries;
};

struct abfr_dev *abfr_init(char *);

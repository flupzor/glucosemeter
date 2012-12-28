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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>

#include <sys/queue.h>

#include <gtk/gtk.h>

#include "glucosemeter.h"

#define DEBUG

#ifdef DEBUG
#define DPRINTF(x) do { printf x; } while(0)
#else
#define DPRINTF(x)
#endif

static void abfr_line_dev(struct abfr_dev *dev, char *line);
static void abfr_line_soft(struct abfr_dev *dev, char *line);
static void abfr_line_date(struct abfr_dev *dev, char *line);
static void abfr_line_nresults(struct abfr_dev *dev, char *line);
static void abfr_line_result(struct abfr_dev *dev, char *line);
static void abfr_line_end(struct abfr_dev *dev, char *line);
static void abfr_line_empty(struct abfr_dev *dev, char *line);
static void abfr_parseline(struct abfr_dev *dev, char *line);

static int			abfr_parsetime(char *p, struct tm *r);
static enum abfr_devtype	abfr_parsedev(char *type);
static enum abfr_softrev	abfr_parsesoft(char *rev);
static int			abfr_nentries(char *);
static int			abfr_parse_entry(char *p, struct abfr_entry *entry);
static uint16_t			abfr_calc_checksum(char *line);
static int			abfr_parse_checksum(char *line, uint16_t *checksum);

static int dev_cmp(const void *k, const void *e);
static int rev_cmp(const void *k, const void *e);
static int month_cmp(const void *k, const void *e);

static gboolean abfr_in(struct device *dev, GIOChannel *gio);
static gboolean abfr_out(struct device *dev, GIOChannel *gio);
static gboolean abfr_error(struct device *dev, GIOChannel *gio);

int abfr_start(struct device *);
int abfr_stop(struct device *);

struct driver abfr_driver = {
	"abfr",
	abfr_start,
	abfr_stop,
	abfr_in,
	abfr_out,
	abfr_error,
};

struct devlist {
	char			*devicename;
	enum abfr_devtype	 devicetype;
};

struct softlist {
	char				*softwarename;
	enum abfr_softrev	 softwaretype;
};

struct monthlist {
	char	*month;
	int 	 number;
};

struct abfr_dev *
abfr_init(char *device_file)
{
	struct abfr_dev *dev;

	dev = calloc(1, sizeof(*dev));
	if (dev == NULL) {
		return NULL;
	}

	return dev;
}

static int
abfr_open(char *dev)
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

int
abfr_start(struct device *dev)
{
	struct abfr_dev		*abfr_dev = (struct abfr_dev *)dev;
	int			 fd;
	guint		 	 r;

	fd = abfr_open(abfr_dev->file);
	if (fd < 0) {
		goto fail;
	}

	dev->channel = g_io_channel_unix_new(fd);
	if (dev->channel == NULL) {
		g_error("Cannnot create GIOChannel");

		goto fail;
	}

	r = g_io_add_watch(dev->channel, G_IO_IN | G_IO_HUP, devicemgmt_input, dev);
	if (!r) {
		g_error("Cannnot watch GIOChannel");

		goto fail;	
	}

	r = g_io_add_watch(dev->channel, G_IO_OUT | G_IO_HUP, devicemgmt_output, dev);
	if (!r) {
		g_error("Cannnot watch GIOChannel");
		goto fail;
	}

	r = g_io_add_watch(dev->channel, G_IO_ERR | G_IO_HUP, devicemgmt_error, dev);
	if (!r) {
		g_error("Cannnot watch GIOChannel");
		goto fail;
	}

	return 1;
fail:
	/* XXX: cleanup fd/channels */
	return (-1);
}

int
abfr_stop(struct device *dev)
{
	/* XXX: close fd/channels */

	return 1;
}

static int
dev_cmp(const void *k, const void *e)
{
        return (strcmp(k, ((const struct devlist *)e)->devicename));
}

static enum abfr_devtype
abfr_parsedev(char *type)
{
	const struct devlist   *p;

	/* keep sorted */
	struct devlist abfr_typelist[] = {
		{ "CDMK311-B0764", ABFR_DEV_CDMK311_B0764 }, // FreeStyle Freedom Lite
		{ "DAMH359-63524", ABFR_DEV_DAMH359_63524 }, // FreeStyle Mini
		{ "DBMN169-C4824", ABFR_DEV_DBMN169_C4824 }  // FreeStyle Lite
	};

	p = bsearch(type, abfr_typelist, sizeof(abfr_typelist)/sizeof(abfr_typelist[0]),
		sizeof(abfr_typelist[0]), dev_cmp);

	if (p)
		return p->devicetype;

	return ABFR_DEV_UNKNOWN;
}

static int
rev_cmp(const void *k, const void *e)
{
        return (strcmp(k, ((const struct softlist *)e)->softwarename));
}

static enum abfr_softrev
abfr_parsesoft(char *rev)
{
	const struct softlist   *p;

	/* keep sorted */
	struct softlist abfr_revlist[] = {
		{ "0.31-P",ABFR_SOFT_0_31_P}, // On my FreeStyle Freedom Lite (The missing spaces isn't a mistake)
		{ "0.31-P1-B0764", ABFR_SOFT_0_31_P1_B0764 },
		{ "1.43       -P", ABFR_SOFT_1_43_P}, // On my FreeStyle Lite
		{ "4.0100     -P", ABFR_SOFT_4_0100_P}, // On my FreeStyle Mini
	};


	p = bsearch(rev, abfr_revlist, sizeof(abfr_revlist)/sizeof(abfr_revlist[0]),
		sizeof(abfr_revlist[0]), rev_cmp);

	if (p)
		return p->softwaretype;

	return ABFR_SOFT_UNKNOWN;
}

static int
month_cmp(const void *k, const void *e)
{
        return (strcmp(k, ((const struct monthlist *)e)->month));
}

static int
abfr_nentries(char *p)
{
	int r;
	const char	*errstr;

	errstr = NULL;
	/* XXX: change upper boundary to some kind of constant */
	r = strtonum(p, 1, 450, &errstr);
	if (errstr)
		return (-1);

	return (r);
}

// 234  Jan  17 2010 00:39 00 0x00
static int
abfr_parse_entry(char *p, struct abfr_entry *entry)
{
	char			*p2;
	const char		*errstr;
	struct monthlist	*m;
	int			 yearint;

	/* Keep sorted */
	struct monthlist mlist[] = {
		{ "Apr", 3 },
		{ "Aug", 7 },
		{ "Dec", 11 },
		{ "Feb", 1 },
		{ "Jan", 0 },
		{ "July", 6 },
		{ "June", 5 },
		{ "Mar", 2 },
		{ "May", 4 },
		{ "Nov", 10 },
		{ "Oct", 9 },
		{ "Sep", 8 },
	};

	p2 = p;
	p = strsep(&p2, " ");
	if (p2 == NULL)
		goto fail;

	errstr = NULL; /* XXX: only necessary one time */
	entry->bloodglucose = strtonum(p, 0, 400, &errstr); /* XXX: change 400 to sth decent */
	if (errstr)
		goto fail;

	/* Skip leading space */
	if (*p2 == ' ')
		p2++;

	p = strsep(&p2, " ");
	if (p2 == NULL)
		goto fail;

	m = bsearch(p, mlist, sizeof(mlist)/sizeof(mlist[0]),
		sizeof(mlist[0]), month_cmp);

	if (!m)
		goto fail;

	entry->ptm.tm_mon = m->number;

	/* Skip leading space */
	if (*p2 == ' ')
		p2++;

	p = strsep(&p2, " ");
	if (p2 == NULL)
		goto fail;

	errstr = NULL;
	entry->ptm.tm_mday = strtonum(p, 1, 31, &errstr);
	if (errstr)
		goto fail;

	p = strsep(&p2, " ");
	if (p2 == NULL)
		goto fail;

	errstr = NULL;
	yearint = strtonum(p, 0, 9999, &errstr);
	if (errstr)
		goto fail;
	entry->ptm.tm_year = yearint - 1900;

	p = strsep(&p2, ":");
	if (p2 == NULL)
		goto fail;

	errstr = NULL; /* XXX: only necessary one time */
	entry->ptm.tm_hour = strtonum(p, 0, 23, &errstr);
	if (errstr)
		goto fail;

	p = strsep(&p2, " ");
	if (p2 == NULL)
		goto fail;

	errstr = NULL; /* XXX: only necessary one time */
	entry->ptm.tm_min = strtonum(p, 0, 59, &errstr);
	if (errstr)
		goto fail;


	return (0);
fail:
	return (-1);
}

/* Jan  21 2010 20:40:00 */
static int
abfr_parsetime(char *p, struct tm *r)
{
	int	yearint;
	struct monthlist *m;
	const char	*errstr;
	char *p2;

	/* Keep sorted */
	struct monthlist mlist[] = {
		{ "Apr", 3 },
		{ "Aug", 7 },
		{ "Dec", 11 },
		{ "Feb", 1 },
		{ "Jan", 0 },
		{ "July", 6 },
		{ "June", 5 },
		{ "Mar", 2 },
		{ "May", 4 },
		{ "Nov", 10 },
		{ "Oct", 9 },
		{ "Sep", 8 },
	};

	p2 = p;
	p = strsep(&p2, " ");
	if (p2 == NULL)
		goto fail;

	m = bsearch(p, mlist, sizeof(mlist)/sizeof(mlist[0]),
		sizeof(mlist[0]), month_cmp);

	if (!m)
		goto fail;

	r->tm_mon = m->number;

	/* Skip leading space */
	if (*p2 == ' ')
		p2++;

	p = strsep(&p2, " ");
	if (p2 == NULL)
		goto fail;

	errstr = NULL;
	r->tm_mday = strtonum(p, 1, 31, &errstr);
	if (errstr)
		goto fail;

	p = strsep(&p2, " ");
	if (p2 == NULL)
		goto fail;

	errstr = NULL;
	yearint = strtonum(p, 0, 9999, &errstr); /* XXX: 3000 should be correspond to what can be stored in tm_year (minus 1900) */
	if (errstr)
		goto fail;

	p = strsep(&p2, ":");
	if (p2 == NULL)
		goto fail;

	errstr = NULL; /* XXX: only necessary one time */
	r->tm_hour = strtonum(p, 0, 23, &errstr);
	if (errstr)
		goto fail;

	p = strsep(&p2, ":");
	if (p2 == NULL)
		goto fail;

	errstr = NULL; /* XXX: only necessary one time */
	r->tm_min = strtonum(p, 0, 59, &errstr);
	if (errstr)
		goto fail;

	errstr = NULL; /* XXX: only necessary one time */
	r->tm_sec = strtonum(p2, 0, 59, &errstr);
	if (errstr)
		goto fail;

	return (0);

fail:
	printf("fail!\n");
	return (-1);
}

static uint16_t
abfr_calc_checksum(char *line)
{
	int i;
	uint16_t checksum = 0;

	for (i = 0; line[i] != '\0'; i++)
		checksum += line[i];

	return checksum;
}

static int
abfr_parse_checksum(char *line, uint16_t *checksum)
{
	char *end, *strchecksum, *endptr;
	unsigned long ul;

	end = line;
	strchecksum = strsep(&end, " ");
	if (end == NULL)
		return -1;

	if (*end == ' ')
		end++;

	if (strcmp(end, "END") != 0)
		return -1;

	ul = strtoul(strchecksum, &endptr, 16);
	if (strchecksum[0] == '\0' || *endptr != '\0')
		return -1;
	if (errno == ERANGE && ul == ULONG_MAX)
		return -1;

	*checksum = ul;

	return 0;
}

static void
abfr_parseline(struct abfr_dev *dev, char *line)
{
	int old_state = dev->protocol_state;

	switch(dev->protocol_state) {
		case ABFR_DEVICE_TYPE:
			abfr_line_dev(dev, line);
			break;
		case ABFR_SOFTWARE_REVISION:
			abfr_line_soft(dev, line);
			break;
		case ABFR_CURRENTDATETIME:
			abfr_line_date(dev, line);
			break;
		case ABFR_NUMBEROFRESULTS:
			abfr_line_nresults(dev, line);
			break;
		case ABFR_RESULTLINE:
			abfr_line_result(dev, line);
			break;
		case ABFR_END:
			abfr_line_end(dev, line);
			break;
		case ABFR_EMPTY:
			abfr_line_empty(dev, line);
			break;
		default:
			/* XXX: this shouldn't happen */
			break;
	}

	DPRINTF(("%s: state: %d -> %d\n", __func__, old_state, dev->protocol_state));
}

static void
abfr_line_dev(struct abfr_dev *dev, char *line)
{
	enum abfr_devtype device_type;

	device_type = abfr_parsedev(line);
	DPRINTF(("%s: device_type: %d\n", __func__, device_type));

	/* Don't continue parsing if the device type isn't known. */
	if (device_type == ABFR_DEV_UNKNOWN)
		dev->protocol_state = ABFR_FAIL;
	else
		dev->protocol_state++;
}

static void
abfr_line_soft(struct abfr_dev *dev, char *line)
{
	enum abfr_softrev softrev;

	softrev = abfr_parsesoft(line);
	DPRINTF(("%s: softrev: %d\n", __func__, softrev));

	/* Don't continue parsing if the software revision isn't known. */
	if (softrev == ABFR_SOFT_UNKNOWN)
		dev->protocol_state = ABFR_FAIL;
	else
		dev->protocol_state++;
}

static void
abfr_line_date(struct abfr_dev *dev, char *line)
{
	struct tm device_tm;	
	int r;

	r = abfr_parsetime(line, &device_tm);
	if (r == -1) {
		dev->protocol_state = ABFR_FAIL;
		return;
	}

	DPRINTF(("%s: currentdatetime\n", __func__));
	dev->protocol_state++;

	return;
}

static void
abfr_line_nresults(struct abfr_dev *dev, char *line)
{
	int nresults;

	nresults = abfr_nentries(line);
	if (nresults == -1)
		dev->protocol_state = ABFR_FAIL;
	else
		dev->protocol_state++;

	dev->nresults = nresults;

	DPRINTF(("%s: numberofresults\n", __func__));

	return;
}

static void
abfr_line_result(struct abfr_dev *dev, char *line)
{
	int r;
	struct abfr_entry *entry;

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		/* XXX: change to a fail state */
		return;
	}

	r = abfr_parse_entry(line, entry);

	/* We can't insert the entry into the database at this point because
	 * the checksum is calculated over all the messages thus we aren't sure
	 * yet if this entry is correct. Instead put all the entries in a
	 * linked list and insert them when the checksum can been verified. */
	SLIST_INSERT_HEAD(&dev->entries, entry, next);

	DPRINTF(("%s: glucose: %d\n", __func__, entry->bloodglucose));
	DPRINTF(("%s: month: %d\n", __func__, entry->ptm.tm_mon));
	DPRINTF(("%s: day: %d\n", __func__, entry->ptm.tm_mday));
	DPRINTF(("%s: year: %d\n", __func__, entry->ptm.tm_year));
	DPRINTF(("%s: hour: %d\n", __func__, entry->ptm.tm_hour));
	DPRINTF(("%s: min: %d\n", __func__, entry->ptm.tm_min));

	dev->results_processed++;
	if (dev->results_processed >= dev->nresults)
		dev->protocol_state = ABFR_END;

	DPRINTF(("%s: result\n", __func__));
}

static void
abfr_line_end(struct abfr_dev *dev, char *line)
{
	uint16_t checksum;
	int r;

	r = abfr_parse_checksum(line, &checksum);
	if (r == -1)
		return;

	if (dev->checksum == checksum) {
		struct abfr_entry *e;

		/* We are as sure as we can get that the entries are correct.
		 * Insert them into the database */
		while (!SLIST_EMPTY(&dev->entries)) {
			e = SLIST_FIRST(&dev->entries);
			SLIST_REMOVE_HEAD(&dev->entries, next);

/*
			meas_insert(dev->gm_state,
				e->bloodglucose,
				asctime(&e->ptm), "abfr");
*/

			free(e);
		}

		DPRINTF(("%s: checksum verified!\n", __func__));
	}
}

static void
abfr_line_empty(struct abfr_dev *dev, char *line)
{
}

static gboolean
abfr_in(struct device *dev, GIOChannel *gio)
{
	GIOStatus	 status;
	gsize		 terminator_pos;
	GError		*error = NULL;
	struct abfr_dev *abfr_dev = (struct abfr_dev *)dev;
	GString		*line;

	if (abfr_dev->protocol_state == ABFR_FAIL) {
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
		if (abfr_dev->protocol_state != ABFR_END)
			abfr_dev->checksum += abfr_calc_checksum(line->str);

		/* Cut off the newline terminators */
		line = g_string_truncate(line, terminator_pos);

		DPRINTF(("%s: line(%zu): \"%s\"\n", __func__, line->len, line->str));

		if (line->len > 0)
			abfr_parseline(abfr_dev, line->str);
	}

	g_string_free(line, TRUE);

	if (status == G_IO_STATUS_EOF)
		return FALSE;

	return TRUE;
}

static gboolean
abfr_out(struct device *dev, GIOChannel *gio)
{
	gchar		 buf[] = "mem";
	gsize		 wrote_len;
	GError		*error = NULL;
	GIOStatus	 status;
	struct abfr_dev *abfr_dev = (struct abfr_dev *)dev;

	if (abfr_dev->protocol_state != ABFR_SEND_MEM) {
		return FALSE;
	}

	status = g_io_channel_write_chars(gio, buf, -1, &wrote_len, &error);

	g_io_channel_flush(gio, NULL);

	DPRINTF(("%s: bytes written: %zu status: %d\n", __func__, wrote_len, status));

	abfr_dev->protocol_state++;

	return TRUE;
}

static gboolean
abfr_error(struct device *dev, GIOChannel *gio)
{
	printf("error\n");

	return TRUE;
}

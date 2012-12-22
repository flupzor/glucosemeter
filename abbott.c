/*
 * Copyright (c) 2009 Alexander Schrijver
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
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "abbott.h"

int dev_cmp(const void *k, const void *e);
int rev_cmp(const void *k, const void *e);
int month_cmp(const void *k, const void *e);

struct devlist {
	char			*devicename;
	enum abbott_devicetype	 devicetype;
};

struct softlist {
	char				*softwarename;
	enum abbott_softwarerevision	 softwaretype;
};

struct monthlist {
	char	*month;
	int 	 number;
};

int
dev_cmp(const void *k, const void *e)
{
        return (strcmp(k, ((const struct devlist *)e)->devicename));
}


enum abbott_devicetype
abbott_parsedev(char *type)
{
	const struct devlist   *p;

	/* keep sorted */
	struct devlist abbott_typelist[] = {
		{ "CDMK311-B0764", ABBOTT_DEV_CDMK311_B0764 }, // FreeStyle Freedom Lite
		{ "DAMH359-63524", ABBOTT_DEV_DAMH359_63524 }, // FreeStyle Mini
		{ "DBMN169-C4824", ABBOTT_DEV_DBMN169_C4824 }  // FreeStyle Lite
	};

	p = bsearch(type, abbott_typelist, sizeof(abbott_typelist)/sizeof(abbott_typelist[0]),
		sizeof(abbott_typelist[0]), dev_cmp);

	if (p)
		return p->devicetype;

	return ABBOTT_DEV_UNKNOWN;
}

int
rev_cmp(const void *k, const void *e)
{
        return (strcmp(k, ((const struct softlist *)e)->softwarename));
}

enum abbott_softwarerevision
abbott_parsesoft(char *rev)
{
	const struct softlist   *p;

	/* keep sorted */
	struct softlist abbott_revlist[] = {
		{ "0.31-P",ABBOTT_SOFT_0_31_P}, // On my FreeStyle Freedom Lite (The missing spaces isn't a mistake)
		{ "0.31-P1-B0764", ABBOTT_SOFT_0_31_P1_B0764 },
		{ "1.43       -P", ABBOTT_SOFT_1_43_P}, // On my FreeStyle Lite
		{ "4.0100     -P", ABBOTT_SOFT_4_0100_P}, // On my FreeStyle Mini
	};


	p = bsearch(rev, abbott_revlist, sizeof(abbott_revlist)/sizeof(abbott_revlist[0]),
		sizeof(abbott_revlist[0]), dev_cmp);

	if (p)
		return p->softwaretype;

	return ABBOTT_SOFT_UNKNOWN;
}

int
month_cmp(const void *k, const void *e)
{
        return (strcmp(k, ((const struct monthlist *)e)->month));
}

int
abbott_nentries(char *p)
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
int
abbott_parse_entry(char *p, struct abbott_entry *entry)
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
int
abbott_parsetime(char *p, struct tm *r)
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

uint16_t
abbott_calc_checksum(char *line)
{
	int i;
	uint16_t checksum = 0;

	for (i = 0; line[i] != '\0'; i++)
		checksum += line[i];

	return checksum;
}

int
abbott_parse_checksum(char *line, uint16_t *checksum)
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

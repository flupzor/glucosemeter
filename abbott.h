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

#define ABBOTT_MAX_ENTRIES	450

// XXX: do these include the NULL terminator?
#define ABBOTT_ENTRYLEN	31
#define ABBOTT_TIMELEN	16

enum abbott_devicetype {
	ABBOTT_DEV_UNKNOWN,
	ABBOTT_DEV_CDMK311_B0764, // FreeStyle Freedom Lite
	ABBOTT_DEV_DAMH359_63524, // FreeStyle Mini
	ABBOTT_DEV_DBMN169_C4824  // FreeStyle Lite
};

enum abbott_softwarerevision {
	ABBOTT_SOFT_UNKNOWN,
	ABBOTT_SOFT_4_0100_P,
	ABBOTT_SOFT_0_31_P1_B0764,
	ABBOTT_SOFT_0_31_P, /* XXX: */
	ABBOTT_SOFT_1_43_P,
};

enum abbott_state {
	PARSE_NONE,
	PARSE_DEVICE_TYPE,
	PARSE_SOFTWARE_REVISION,
	PARSE_CURRENTTIME,
	PARSE_NENTRIES,
	PARSE_FIRSTENTRY,
	PARSE_ENTRY,
	PARSE_END,
};

struct abbott_entry {
	int		bloodsugar;
	struct tm	ptm;
	int		plasmatype;
};

int	 abbott_parsetime(char *p, struct tm *r);
enum abbott_devicetype	abbott_devicetype(char *type);
enum abbott_softwarerevision abbott_softrev(char *rev);
int abbott_nentries(char *);
int abbott_parse_entry(char *p, struct abbott_entry *entry);

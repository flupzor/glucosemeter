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

#include "glucosemeter.h"

void
devicemgmt_init(struct gm_conf *conf)
{
	TAILQ_INIT(&conf->devices);
	conf->devicemgmt_status = 0;
}

void
devicemgmt_start(struct gm_conf *conf)
{
	struct device *dev;
	struct driver *driver;

	TAILQ_FOREACH(dev, &conf->devices, entry) {
		driver = dev->driver;

		driver->driver_start_fn((struct device *)dev);
	}
}

gboolean
devicemgmt_input(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	struct device *dev = (struct device *)data;
	struct driver *drv = (struct driver *)dev->driver;

	printf("in\n");

	return drv->driver_input(dev, gio);
}

gboolean
devicemgmt_output(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	struct device *dev = (struct device *)data;
	struct driver *drv = (struct driver *)dev->driver;

	return drv->driver_output(dev, gio);
}

gboolean
devicemgmt_error(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	struct device *dev = (struct device *)data;
	struct driver *drv = (struct driver *)dev->driver;

	return drv->driver_error(dev, gio);
}

void
devicemgmt_stop(struct gm_conf *conf)
{
}

int
devicemgmt_status(struct gm_conf *conf)
{
	return conf->devicemgmt_status;
}

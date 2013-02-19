/*
 * log.c
 *
 * Copyright 2013 Red Hat, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

static int log_fd = -1;
static FILE *f = NULL;

static int
open_log(void)
{
	if (log_fd > -1)
		return 0;
	log_fd = open("/var/log/grubby", O_RDWR|O_APPEND|O_CREAT|O_CLOEXEC, 0600);
	if (log_fd < 0)
		return log_fd;

	f = fdopen(log_fd, "a+");
	if (f == NULL) {
		typeof(errno) saved_errno = errno;
		close(log_fd);
		log_fd = -1;
		errno = saved_errno;
		return -1;
	}

	setbuf(f, NULL);
	return 0;
}

int
log_time(FILE *log)
{
	if (!log) {
		int rc = open_log();
		if (rc < 0)
			return rc;
	}

	time_t t = time(NULL);
	char timestr[27];

	ctime_r(&t, timestr);
	timestr[26] = '\0';
	for (int i = 26; i >= 0; i--)
		if (timestr[i] == '\n')
			timestr[i] = '\0';

	return log_message(log, "DBG: %d: %s: ", getpid(), timestr);
}

int
log_vmessage(FILE *log, const char *msg, va_list ap)
{
	int rc;

	if (!msg)
		return -1;
	if (msg[0] == '\0')
		return 0;

	if (!log) {
		rc = open_log();
		if (rc < 0)
			return rc;
	}

	va_list aq;
	va_copy(aq, ap);

	vfprintf(log ? log : f, msg, aq);
	va_end(aq);
	fdatasync(log ? fileno(log) : log_fd);

	return 0;
}

int
log_message(FILE *log, const char *msg, ...)
{
	va_list argp;

	va_start(argp, msg);
	int rc = log_vmessage(log, msg, argp);
	va_end(argp);
	return rc;
}

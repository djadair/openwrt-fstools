/*
 * Copyright (C) 2021 OpenWrt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>
#include <mtd/mtd-user.h>
#include <libubox/ulog.h>

#include "probe.h"

/*
 * Info messages are a bit too chatty for normal use
 * but handy for debug.
 */
#undef ULOG_INFO
#define ULOG_INFO( fmt, ... ) {}

/*
 * Note: would like to use strerror_r below but there is a catch-22
 * the rest of the block source code uses _GCC_SOURCE but arm/musl
 * toolchain provides a POSIX version of strerror_r for both GCC and POSIX.
 * Just punt and use strerror since probe is only going to be called
 * from one block thread at a time anyway.
 * If all else fails give up and print errno...
 */
static int mtd_ecc_invalid( const char *mtddev )
{
	struct mtd_ecc_stats stat1, stat2;
	char buf[512];
	int fd;
	int rval = false;

	if ((fd = open(mtddev, O_RDONLY)) == -1) {
		ULOG_WARN("mtd_ecc_invalid: %s %s\n",
			  mtddev,
			  strerror(errno));
		/*
		 * Hmm, strange but let normal probe handle it.
		 * This could happen if somone uses "mtdblock" in
		 * and lvm volume name for instance so we don't want
		 * to block the probe.
		 */
		return ( rval );
	}

	if (ioctl(fd, ECCGETSTATS, &stat1)) {
		ULOG_INFO("ecc_invalid: %s No stats -- assume good\n",
			  mtddev);
	} else {
		ULOG_INFO("ecc_invalid: %s fail: %d, corr: %d\n",
			  mtddev,
			  stat1.failed,
			  stat1.corrected);

		if ( sizeof( buf ) == read(fd, buf, sizeof(buf)) &&
		     !ioctl(fd, ECCGETSTATS, &stat2) &&
		     (stat1.failed == stat2.failed) ) {
			/* device looks good */
		} else {
			ULOG_WARN("ecc_invalid: %s failed, disabling probe\n",
				  mtddev);
			rval = true;
		}
	}

	close(fd);
	return ( rval );
}

/*
 * mtdblock devices with invalid ECC spew kernel errors
 * during block device probe.  This can happen either because
 * the partition is not initialized or because it was written
 * by a programmer or boot prom that uses a non-standard ECC
 * scheme that linux does not handle.
 *
 * Since boot-from-nand devices use chips that guarantee enough
 * good space for the bootloader they can and do blow off
 * error correction so we should just leave those partitions alone.
 *
 */
int check_invalid_mtdblock(const char *devpath)
{
	const char *tmp;
	char *mtdpath;
	int rval = false;

	/*
	 *  Note: filtering only happens when we are certain.
	 * Just let default probe worry about the corner cases.
	 */
	if ((tmp = strstr(devpath, "mtdblock")) &&
	    (0 <= asprintf(&mtdpath, "/dev/mtd%s",
			  &tmp[8]))) {

		/* Found mtdblock, check raw mtd for ECC errors. */
		rval = mtd_ecc_invalid( mtdpath );
		free(mtdpath);
	}

	/* Non-zero / true means skip probe. */
	return ( rval );
}

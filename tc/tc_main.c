/*
 * tc_main.c	"tc" main.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Fixes:
 *
 * Petri Mattila <petri@prihateam.fi> 990308: wrong memset's resulted in faults
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

#include "SNAPSHOT.h"
#include "utils.h"
#include "tc_util.h"
#include "tc_common.h"
#include "namespace.h"

int show_stats;
int show_details;
int show_raw;
int show_pretty;
int show_graph;
int timestamp;

int batch_mode;
int resolve_hosts;
int use_iec;
int force;
bool use_names;

static char *conf_file;

static int batch(const char *name)
{
	char *line = NULL;
	size_t len = 0;
	int ret = 0;

	batch_mode = 1;
	if (name && strcmp(name, "-") != 0) {
		if (freopen(name, "r", stdin) == NULL) {
			fprintf(stderr, "Cannot open file \"%s\" for reading: %s\n",
				name, strerror(errno));
			return -1;
		}
	}

	tc_init();

	cmdlineno = 0;
	while (getcmdline(&line, &len, stdin) != -1) {
		char *largv[100];
		int largc;

		largc = makeargs(line, largv, 100);
		if (largc == 0)
			continue;	/* blank line */

		if (tc_cmd(largc, largv)) {
			fprintf(stderr, "Command failed %s:%d\n", name, cmdlineno);
			ret = 1;
			if (!force)
				break;
		}
	}
	if (line)
		free(line);

	tc_exit();
	return ret;
}

int main(int argc, char **argv)
{
	int ret;
	char *batch_file = NULL;

	while (argc > 1) {
		if (argv[1][0] != '-')
			break;
		if (matches(argv[1], "-stats") == 0 ||
			 matches(argv[1], "-statistics") == 0) {
			++show_stats;
		} else if (matches(argv[1], "-details") == 0) {
			++show_details;
		} else if (matches(argv[1], "-raw") == 0) {
			++show_raw;
		} else if (matches(argv[1], "-pretty") == 0) {
			++show_pretty;
		} else if (matches(argv[1], "-graph") == 0) {
			show_graph = 1;
		} else if (matches(argv[1], "-Version") == 0) {
			printf("tc utility, iproute2-ss%s\n", SNAPSHOT);
			return 0;
		} else if (matches(argv[1], "-iec") == 0) {
			++use_iec;
		} else if (matches(argv[1], "-help") == 0) {
			tc_usage();
			return 0;
		} else if (matches(argv[1], "-force") == 0) {
			++force;
		} else if (matches(argv[1], "-batch") == 0) {
			argc--;	argv++;
			if (argc <= 1)
				tc_usage();
			batch_file = argv[1];
		} else if (matches(argv[1], "-netns") == 0) {
			NEXT_ARG();
			if (netns_switch(argv[1]))
				return -1;
		} else if (matches(argv[1], "-names") == 0 ||
				matches(argv[1], "-nm") == 0) {
			use_names = true;
		} else if (matches(argv[1], "-cf") == 0 ||
				matches(argv[1], "-conf") == 0) {
			NEXT_ARG();
			conf_file = argv[1];
		} else if (matches(argv[1], "-timestamp") == 0) {
			timestamp++;
		} else if (matches(argv[1], "-tshort") == 0) {
			++timestamp;
			++timestamp_short;
		} else {
			fprintf(stderr, "Option \"%s\" is unknown, try \"tc -help\".\n", argv[1]);
			return -1;
		}
		argc--;	argv++;
	}

	if (batch_file)
		return batch(batch_file);

	if (argc <= 1) {
		tc_usage();
		return 0;
	}

	tc_init();

	if (use_names && cls_names_init(conf_file)) {
		ret = -1;
		goto Exit;
	}

	ret = tc_cmd(argc-1, argv+1);
Exit:
	tc_exit();

	if (use_names)
		cls_names_uninit();

	return ret;
}

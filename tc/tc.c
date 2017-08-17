/*
 * tc.c		"tc" utility frontend.
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

struct rtnl_handle rth;

static void *BODY;	/* cached handle dlopen(NULL) */
static struct qdisc_util *qdisc_list;
static struct filter_util *filter_list;

static int print_noqopt(struct qdisc_util *qu, FILE *f,
			struct rtattr *opt)
{
	if (opt && RTA_PAYLOAD(opt))
		fprintf(f, "[Unknown qdisc, optlen=%u] ",
			(unsigned int) RTA_PAYLOAD(opt));
	return 0;
}

static int parse_noqopt(struct qdisc_util *qu, int argc, char **argv, struct nlmsghdr *n)
{
	if (argc) {
		fprintf(stderr, "Unknown qdisc \"%s\", hence option \"%s\" is unparsable\n", qu->id, *argv);
		return -1;
	}
	return 0;
}

static int print_nofopt(struct filter_util *qu, FILE *f, struct rtattr *opt, __u32 fhandle)
{
	if (opt && RTA_PAYLOAD(opt))
		fprintf(f, "fh %08x [Unknown filter, optlen=%u] ",
			fhandle, (unsigned int) RTA_PAYLOAD(opt));
	else if (fhandle)
		fprintf(f, "fh %08x ", fhandle);
	return 0;
}

static int parse_nofopt(struct filter_util *qu, char *fhandle, int argc, char **argv, struct nlmsghdr *n)
{
	__u32 handle;

	if (argc) {
		fprintf(stderr, "Unknown filter \"%s\", hence option \"%s\" is unparsable\n", qu->id, *argv);
		return -1;
	}
	if (fhandle) {
		struct tcmsg *t = NLMSG_DATA(n);

		if (get_u32(&handle, fhandle, 16)) {
			fprintf(stderr, "Unparsable filter ID \"%s\"\n", fhandle);
			return -1;
		}
		t->tcm_handle = handle;
	}
	return 0;
}

struct qdisc_util *get_qdisc_kind(const char *str)
{
	void *dlh;
	char buf[256];
	struct qdisc_util *q;

	for (q = qdisc_list; q; q = q->next)
		if (strcmp(q->id, str) == 0)
			return q;

	snprintf(buf, sizeof(buf), "%s/q_%s.so", get_tc_lib(), str);
	dlh = dlopen(buf, RTLD_LAZY);
	if (!dlh) {
		/* look in current binary, only open once */
		dlh = BODY;
		if (dlh == NULL) {
			dlh = BODY = dlopen(NULL, RTLD_LAZY);
			if (dlh == NULL)
				goto noexist;
		}
	}

	snprintf(buf, sizeof(buf), "%s_qdisc_util", str);
	q = dlsym(dlh, buf);
	if (q == NULL)
		goto noexist;

reg:
	q->next = qdisc_list;
	qdisc_list = q;
	return q;

noexist:
	q = calloc(1, sizeof(*q));
	if (q) {
		q->id = strdup(str);
		q->parse_qopt = parse_noqopt;
		q->print_qopt = print_noqopt;
		goto reg;
	}
	return q;
}


struct filter_util *get_filter_kind(const char *str)
{
	void *dlh;
	char buf[256];
	struct filter_util *q;

	for (q = filter_list; q; q = q->next)
		if (strcmp(q->id, str) == 0)
			return q;

	snprintf(buf, sizeof(buf), "%s/f_%s.so", get_tc_lib(), str);
	dlh = dlopen(buf, RTLD_LAZY);
	if (dlh == NULL) {
		dlh = BODY;
		if (dlh == NULL) {
			dlh = BODY = dlopen(NULL, RTLD_LAZY);
			if (dlh == NULL)
				goto noexist;
		}
	}

	snprintf(buf, sizeof(buf), "%s_filter_util", str);
	q = dlsym(dlh, buf);
	if (q == NULL)
		goto noexist;

reg:
	q->next = filter_list;
	filter_list = q;
	return q;
noexist:
	q = calloc(1, sizeof(*q));
	if (q) {
		strncpy(q->id, str, 15);
		q->parse_fopt = parse_nofopt;
		q->print_fopt = print_nofopt;
		goto reg;
	}
	return q;
}

void tc_usage(void)
{
	fprintf(stderr, "Usage: tc [ OPTIONS ] OBJECT { COMMAND | help }\n"
			"       tc [-force] -batch filename\n"
			"where  OBJECT := { qdisc | class | filter | action | monitor | exec }\n"
	                "       OPTIONS := { -s[tatistics] | -d[etails] | -r[aw] | -p[retty] | -b[atch] [filename] | -n[etns] name |\n"
			"                    -nm | -nam[es] | { -cf | -conf } path }\n");
}

int tc_cmd(int argc, char **argv)
{
	if (matches(*argv, "qdisc") == 0)
		return do_qdisc(argc-1, argv+1);
	if (matches(*argv, "class") == 0)
		return do_class(argc-1, argv+1);
	if (matches(*argv, "filter") == 0)
		return do_filter(argc-1, argv+1);
	if (matches(*argv, "actions") == 0)
		return do_action(argc-1, argv+1);
	if (matches(*argv, "monitor") == 0)
		return do_tcmonitor(argc-1, argv+1);
	if (matches(*argv, "exec") == 0)
		return do_exec(argc-1, argv+1);
	if (matches(*argv, "help") == 0) {
		tc_usage();
		return 0;
	}

	fprintf(stderr, "Object \"%s\" is unknown, try \"tc help\".\n",
		*argv);
	return -1;
}

void tc_init(void)
{
	tc_core_init();
	if (rtnl_open(&rth, 0) < 0) {
		fprintf(stderr, "Cannot open rtnetlink\n");
		exit(1);
	}
}

void tc_exit(void)
{
	rtnl_close(&rth);
}

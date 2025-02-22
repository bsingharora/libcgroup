// SPDX-License-Identifier: LGPL-2.1-only
/**
 * Libcgroup extended cgset.  Works with both cgroup v1 and v2
 *
 * Copyright (c) 2021-2022 Oracle and/or its affiliates.
 * Author: Tom Hromatka <tom.hromatka@oracle.com>
 */
#include "tools-common.h"
#include "abstraction-common.h"

#include <libcgroup.h>
#include <libcgroup-internal.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>

#define FL_RULES	1
#define FL_COPY		2

enum {
	COPY_FROM_OPTION = CHAR_MAX + 1
};

#ifndef UNIT_TEST
static const struct option long_options[] = {
	{"v1",			      no_argument, NULL, '1'},
	{"v2",			      no_argument, NULL, '2'},
	{"ignore-unmappable",	      no_argument, NULL, 'i'},
	{"rule",		required_argument, NULL, 'r'},
	{"help",		      no_argument, NULL, 'h'},
	{"copy-from",		required_argument, NULL, COPY_FROM_OPTION},
	{NULL, 0, NULL, 0}
};

int flags; /* used input method */

static struct cgroup *copy_name_value_from_cgroup(char src_cg_path[FILENAME_MAX])
{
	struct cgroup *src_cgroup;
	int ret = 0;

	/* create source cgroup */
	src_cgroup = cgroup_new_cgroup(src_cg_path);
	if (!src_cgroup) {
		fprintf(stderr, "can't create cgroup: %s\n",
			cgroup_strerror(ECGFAIL));
		goto scgroup_err;
	}

	/* copy the name-version values to the cgroup structure */
	ret = cgroup_get_cgroup(src_cgroup);
	if (ret != 0) {
		fprintf(stderr, "cgroup %s error: %s\n",
			src_cg_path, cgroup_strerror(ret));
		goto scgroup_err;
	}

	return src_cgroup;

scgroup_err:
	cgroup_free(&src_cgroup);

	return NULL;
}

static void usage(int status, const char *program_name)
{
	if (status != 0) {
		fprintf(stderr, "Wrong input parameters,");
		fprintf(stderr, " try %s --help' for more information.\n",
			program_name);
		return;
	}

	printf("Usage: %s [-r <name=value>] <cgroup_path> ...\n",
	       program_name);
	printf("   or: %s --copy-from <source_cgroup_path> ", program_name);
	printf("<cgroup_path> ...\n");
	printf("Set the parameters of given cgroup(s)\n");
	printf("  -1, --v1                      Provided parameters are in ");
	printf("v1 format\n");
	printf("  -2, --v2                      Provided parameters are in ");
	printf("v2 format\n");
	printf("  -i, --ignore-unmappable       Do not return an error for settings ");
	printf("that cannot be converted\n");
	printf("  -r, --variable <name>			Define parameter ");
	printf("to set\n");
	printf("  --copy-from <source_cgroup_path>	Control group whose ");
	printf("parameters will be copied\n");
}
#endif /* !UNIT_TEST */

STATIC int parse_r_flag(const char * const program_name,
			const char * const name_value_str,
			struct control_value * const name_value)
{
	char *copy, *buf;
	int ret = 0;

	copy = strdup(name_value_str);
	if (copy == NULL) {
		fprintf(stderr, "%s: not enough memory\n", program_name);
		ret = -1;
		goto err;
	}

	/* parse optarg value */
	buf = strtok(copy, "=");
	if (buf == NULL) {
		fprintf(stderr, "%s: wrong parameter of option -r: %s\n",
			program_name, optarg);
		ret = -1;
		goto err;
	}

	strncpy(name_value->name, buf, FILENAME_MAX);
	name_value->name[FILENAME_MAX-1] = '\0';

	buf = strchr(name_value_str, '=');
	/*
	 * we don't need to check the return value of strchr because we
	 * know there's already an '=' character in the string.
	 */
	buf++;

	if (strlen(buf) == 0) {
		fprintf(stderr, "%s: wrong parameter of option -r: %s\n",
			program_name, optarg);
		ret = -1;
		goto err;
	}

	strncpy(name_value->value, buf, CG_CONTROL_VALUE_MAX);
	name_value->value[CG_CONTROL_VALUE_MAX-1] = '\0';

err:
	if (copy)
		free(copy);

	return ret;
}

#ifndef UNIT_TEST
int main(int argc, char *argv[])
{
	struct control_value *name_value = NULL;
	int nv_number = 0;
	int nv_max = 0;

	struct cgroup *converted_src_cgroup;
	char src_cg_path[FILENAME_MAX];
	struct cgroup *src_cgroup;
	struct cgroup *cgroup;

	enum cg_version_t src_version = CGROUP_UNK;
	bool ignore_unmappable = false;
	int ret = 0;
	int c;

	/* no parametr on input */
	if (argc < 2) {
		fprintf(stderr, "Usage is %s -r <name=value> ", argv[0]);
		fprintf(stderr, "<relative path to cgroup>\n");
		return -1;
	}

	/* parse arguments */
	while ((c = getopt_long (argc, argv,
		"r:h12i", long_options, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(0, argv[0]);
			ret = 0;
			goto err;
		case 'r':
			if ((flags &  FL_COPY) != 0) {
				usage(1, argv[0]);
				ret = -1;
				goto err;
			}
			flags |= FL_RULES;

			/*
			 * add name-value pair to buffer
			 * (= name_value variable)
			 */
			if (nv_number >= nv_max) {
				nv_max += CG_NV_MAX;
				name_value = (struct control_value *)
					realloc(name_value,
					nv_max * sizeof(struct control_value));
				if (!name_value) {
					fprintf(stderr, "%s: ", argv[0]);
					fprintf(stderr,	"not enough memory\n");
					ret = -1;
					goto err;
				}
			}

			ret = parse_r_flag(argv[0], optarg,
					   &name_value[nv_number]);
			if (ret)
				goto err;

			nv_number++;
			break;
		case COPY_FROM_OPTION:
			if (flags != 0) {
				usage(1, argv[0]);
				ret = -1;
				goto err;
			}
			flags |= FL_COPY;
			strncpy(src_cg_path, optarg, FILENAME_MAX);
			src_cg_path[FILENAME_MAX-1] = '\0';
			break;
		case '1':
			src_version = CGROUP_V1;
			break;
		case '2':
			src_version = CGROUP_V2;
			break;
		case 'i':
			ignore_unmappable = true;
			break;
		default:
			usage(1, argv[0]);
			ret = -1;
			goto err;
		}
	}

	/* no cgroup name */
	if (!argv[optind]) {
		fprintf(stderr, "%s: no cgroup specified\n", argv[0]);
		ret = -1;
		goto err;
	}

	if (flags == 0) {
		fprintf(stderr, "%s: no name-value pair was set\n", argv[0]);
		ret = -1;
		goto err;
	}

	/* initialize libcgroup */
	ret = cgroup_init();
	if (ret) {
		fprintf(stderr, "%s: libcgroup initialization failed: %s\n",
			argv[0], cgroup_strerror(ret));
		goto err;
	}

	/* copy the name-value pairs from -r options */
	if ((flags & FL_RULES) != 0) {
		src_cgroup = create_cgroup_from_name_value_pairs(
						"tmp", name_value, nv_number);
		if (src_cgroup == NULL)
			goto err;
	}

	/* copy the name-value from the given group */
	if ((flags & FL_COPY) != 0) {
		src_cgroup = copy_name_value_from_cgroup(src_cg_path);
		if (src_cgroup == NULL)
			goto err;
	}

	while (optind < argc) {
		/* create new cgroup */
		cgroup = cgroup_new_cgroup(argv[optind]);
		if (!cgroup) {
			ret = ECGFAIL;
			fprintf(stderr, "%s: can't add new cgroup: %s\n",
				argv[0], cgroup_strerror(ret));
			goto cgroup_free_err;
		}

		/* copy the values from the source cgroup to new one */
		ret = cgroup_copy_cgroup(cgroup, src_cgroup);
		if (ret != 0) {
			fprintf(stderr, "%s: cgroup %s error: %s\n",
				argv[0], src_cg_path, cgroup_strerror(ret));
			goto cgroup_free_err;
		}

		converted_src_cgroup = cgroup_new_cgroup(cgroup->name);
		if (converted_src_cgroup == NULL) {
			ret = ECGCONTROLLERCREATEFAILED;
			goto err;
		}

		ret = cgroup_convert_cgroup(converted_src_cgroup, CGROUP_DISK,
					    src_cgroup, src_version);
		if (ret == ECGNOVERSIONCONVERT && ignore_unmappable)
			/*
			 * The user has specified that we should ignore
			 * any errors due to being unable to map from v1 to
			 * v2 or vice versa
			 */
			ret = 0;
		else if (ret)
			goto err;

		cgroup_free(&cgroup);
		cgroup = converted_src_cgroup;

		/* modify cgroup based on values of the new one */
		ret = cgroup_modify_cgroup(cgroup);
		if (ret) {
			fprintf(stderr, "%s: cgroup modify error: %s\n",
				argv[0], cgroup_strerror(ret));
			goto cgroup_free_err;
		}

		optind++;
		cgroup_free(&cgroup);
	}

cgroup_free_err:
	if (cgroup)
		cgroup_free(&cgroup);
	cgroup_free(&src_cgroup);
err:
	free(name_value);

	return ret;
}
#endif /* !UNIT_TEST */

#ifdef LIBCG_LIB
int cgroup_cgxset(const struct cgroup * const cgroup,
		  enum cg_version_t version, bool ignore_unmappable)
{
	struct cgroup *converted_cgroup;
	int ret;

	converted_cgroup = cgroup_new_cgroup(cgroup->name);
	if (converted_cgroup == NULL) {
		ret = ECGCONTROLLERCREATEFAILED;
		goto err;
	}

	ret = cgroup_convert_cgroup(converted_cgroup, CGROUP_DISK,
				    cgroup, version);
	if (ret == ECGNOVERSIONCONVERT && ignore_unmappable)
		/*
		 * The user has specified that we should ignore
		 * any errors due to being unable to map from v1 to
		 * v2 or vice versa
		 */
		ret = 0;
	else if (ret)
		goto err;

	/* modify cgroup based on values of the new one */
	ret = cgroup_modify_cgroup(converted_cgroup);
	if (ret)
		goto err;

err:
	if (converted_cgroup)
		cgroup_free(&converted_cgroup);

	return ret;
}
#endif /* LIBCG_LIB */

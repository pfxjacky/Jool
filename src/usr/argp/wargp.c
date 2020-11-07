#include "usr/argp/wargp.h"

#include <stdlib.h>
#include <string.h>

#include "common/xlat.h"
#include "common/constants.h"
#include "usr/util/str_utils.h"
#include "usr/argp/log.h"
#include "usr/argp/xlator_type.h"

const char *argp_program_version = JOOL_VERSION_STR;
const char *argp_program_bug_address = "jool@nic.mx";

int wargp_parse_bool(void *input, int key, char *str);
int wargp_parse_u8(void *field, int key, char *str);
int wargp_parse_u32(void *field, int key, char *str);
int wargp_parse_u64(void *field, int key, char *str);
int wargp_parse_l4proto(void *input, int key, char *str);
int wargp_parse_string(void *input, int key, char *str);
int wargp_parse_addr(void *void_field, int key, char *str);
int wargp_parse_prefix6(void *input, int key, char *str);
int wargp_parse_prefix4(void *input, int key, char *str);

struct wargp_type wt_bool = {
	/* Boolean opts need no argument; absence is false, presence is true. */
	.argument = NULL,
	.parse = wargp_parse_bool,
};

/* Inconsistency warning: Refers to a wargp_u8. */
struct wargp_type wt_u8 = {
	.argument = "<integer>",
	.parse = wargp_parse_u8,
};

/* Inconsistency warning: Refers to a __u32. */
struct wargp_type wt_u32 = {
	.argument = "<integer>",
	.parse = wargp_parse_u32,
};

/* Inconsistency warning: Refers to a wargp_u64. */
struct wargp_type wt_u64 = {
	.argument = "<integer>",
	.parse = wargp_parse_u64,
};

struct wargp_type wt_l4proto = {
	/* The flag itself signals the protocol. */
	.argument = NULL,
	.parse = wargp_parse_l4proto,
};

struct wargp_type wt_string = {
	.argument = "<string>",
	.parse = wargp_parse_string,
};

struct wargp_type wt_addr = {
	.argument = "<IP Address>",
	.parse = wargp_parse_addr,
};

/*
 * Because of the autocomplete candidates, this one is specifically meant to
 * refer to the pool6 prefix.
 */
struct wargp_type wt_prefix6 = {
	.argument = "<IPv6 Prefix>",
	.parse = wargp_parse_prefix6,
	.candidates = TYPICAL_XLAT_PREFIXES,
};

struct wargp_type wt_prefix4 = {
	.argument = "<IPv4 prefix>",
	.parse = wargp_parse_prefix4,
};

struct wargp_args {
	struct wargp_option *opts;
	unsigned char *input;
};

int wargp_parse_bool(void *void_field, int key, char *str)
{
	struct wargp_bool *field = void_field;
	field->value = true;
	return 0;
}

int wargp_parse_u8(void *void_field, int key, char *str)
{
	struct wargp_u8 *field = void_field;
	struct jool_result result;

	result = str_to_u8(str, &field->value, MAX_U8);
	if (result.error)
		return pr_result(&result);

	field->set = true;
	return 0;
}

int wargp_parse_u32(void *field, int key, char *str)
{
	struct jool_result result;

	result = str_to_u32(str, field);
	if (result.error)
		return pr_result(&result);

	return 0;
}

int wargp_parse_u64(void *void_field, int key, char *str)
{
	struct wargp_u64 *field = void_field;
	struct jool_result result;

	result = str_to_u64(str, &field->value);
	if (result.error)
		return pr_result(&result);

	field->set = true;
	return 0;
}

int wargp_parse_l4proto(void *void_field, int key, char *str)
{
	struct wargp_l4proto *field = void_field;

	if (field->set) {
		pr_err("Only one protocol is allowed per request.");
		return -EINVAL;
	}

	switch (key) {
	case ARGP_TCP:
		field->proto = L4PROTO_TCP;
		field->set = true;
		return 0;
	case ARGP_UDP:
		field->proto = L4PROTO_UDP;
		field->set = true;
		return 0;
	case ARGP_ICMP:
		field->proto = L4PROTO_ICMP;
		field->set = true;
		return 0;
	}

	pr_err("Unknown protocol key: %d", key);
	return -EINVAL;
}

int wargp_parse_string(void *void_field, int key, char *str)
{
	struct wargp_string *field = void_field;
	field->value = str;
	return 0;
}

int wargp_parse_addr(void *void_field, int key, char *str)
{
	struct wargp_addr *field = void_field;
	struct jool_result result;

	if (strchr(str, ':')) {
		field->proto = 6;
		result = str_to_addr6(str, &field->addr.v6);
		return pr_result(&result);
	}
	if (strchr(str, '.')) {
		field->proto = 4;
		result = str_to_addr4(str, &field->addr.v4);
		return pr_result(&result);
	}

	return ARGP_ERR_UNKNOWN;
}

int wargp_parse_prefix6(void *void_field, int key, char *str)
{
	struct wargp_prefix6 *field = void_field;
	struct jool_result result;

	field->set = true;
	result = str_to_prefix6(str, &field->prefix);
	if (result.error)
		return pr_result(&result);

	return 0;
}

int wargp_parse_prefix4(void *void_field, int key, char *str)
{
	struct wargp_prefix4 *field = void_field;
	struct jool_result result;

	field->set = true;
	result = str_to_prefix4(str, &field->prefix);
	if (result.error)
		return pr_result(&result);

	return 0;
}

static bool xt_matches(struct wargp_option const *wopt)
{
	if (!wopt->xt)
		return true;
	return xt_get() & wopt->xt;
}

static int adapt_options(struct argp *argp, struct wargp_option const *wopts,
		struct argp_option **result)
{
	struct wargp_option const *wopt;
	struct argp_option *opts;
	struct argp_option *opt;
	unsigned int total_opts;

	if (!wopts) {
		*result = NULL;
		return 0;
	}

	total_opts = 0;
	for (wopt = wopts; wopt->name; wopt++)
		if (xt_matches(wopt) && (wopt->key != ARGP_KEY_ARG))
			total_opts++;

	opts = calloc(total_opts + 1, sizeof(struct argp_option));
	if (!opts) {
		pr_err("Out of memory.");
		return -ENOMEM;
	}
	argp->options = opts;

	opt = opts;
	for (wopt = wopts; wopt->name; wopt++) {
		if (!xt_matches(wopt))
			continue;

		if (wopt->key == ARGP_KEY_ARG) {
			if (argp->args_doc) {
				pr_err("Bug: Only one ARGP_KEY_ARG option is allowed per option list.");
				free(opts);
				return -EINVAL;
			}
			argp->args_doc = wopt->type->argument;
			continue;
		}

		opt->name = wopt->name;
		opt->key = wopt->key;
		opt->arg = wopt->type->argument;
		opt->doc = wopt->doc;
		opt++;
	}

	*result = opts;
	return 0;
}

static int wargp_parser(int key, char *str, struct argp_state *state)
{
	struct wargp_args *wargs = state->input;
	struct wargp_option *opt;

	if (!wargs->opts)
		return ARGP_ERR_UNKNOWN;

	for (opt = wargs->opts; opt->name; opt++) {
		if (opt->key == key) {
			return opt->type->parse(wargs->input + opt->offset, key,
					str);
		}
	}

	return ARGP_ERR_UNKNOWN;
}

int wargp_parse(struct wargp_option *wopts, int argc, char **argv, void *input)
{
	struct wargp_args wargs = { .opts = wopts, .input = input };
	struct argp argp = { .parser = wargp_parser };
	struct argp_option *opts;
	int error;

	error = adapt_options(&argp, wopts, &opts);
	if (error)
		return error;

	error = argp_parse(&argp, argc, argv, 0, NULL, &wargs);

	if (opts)
		free(opts);
	return error;
}

void print_wargp_opts(struct wargp_option *opts)
{
	struct wargp_option *opt;

	for (opt = opts; opt->name; opt++) {
		if (opt->key != ARGP_KEY_ARG)
			printf("--%s ", opt->name);
		if (opt->type->candidates)
			printf("%s ", opt->type->candidates);
	}
	printf("--help --usage --version");
}

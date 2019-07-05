/*
 * lucihttp - HTTP utility library - multipart parser tester
 *
 * Copyright 2018 Jo-Philipp Wich <jo@mein.io>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <lucihttp/multipart-parser.h>
#include <lucihttp/utils.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>


struct test_context {
	bool is_file;
	char *header;
	char *expect_error;
	char *expect_pname;
	char *expect_pvalue;
	char *expect_hname;
	char *expect_hvalue;
	bool matched_error;
	bool matched_pname;
	bool matched_pvalue;
	bool matched_hname;
	bool matched_hvalue;
	size_t bufsize;
	const char *dumpprefix;
	unsigned int dumpcount;
	int dumpfd;
};

static void xfree(void *p)
{
	if (p)
		free(p);
}

static char *memdup(const char *data, char **copy, size_t len)
{
	xfree(*copy);

	if (!data || !len)
		return NULL;

	*copy = calloc(1, len + 1);

	if (!*copy)
		return NULL;

	memcpy(*copy, data, len);

	return *copy;
}

static bool test_callback(struct lh_mpart *p,
                          enum lh_mpart_callback_type type,
                          const char *buffer, size_t length, void *priv)
{
	char *tok = NULL, *name = NULL, *file = NULL;
	struct test_context *ctx = priv;
	char path[1024];

	switch (type)
	{
	case LH_MP_CB_HEADER_NAME:
		memdup(buffer, &ctx->header, length);

		if (ctx->header && ctx->expect_hname &&
		    !strcasecmp(ctx->header, ctx->expect_hname))
		    ctx->matched_hname = true;

		break;

	case LH_MP_CB_HEADER_VALUE:
		if (ctx->header && !strcasecmp(ctx->header, "content-disposition")) {
			tok = lh_header_attribute(buffer, length, NULL, NULL);

			if (!strcasecmp(tok, "form-data")) {
				name = lh_header_attribute(buffer, length, "name", NULL);
				file = lh_header_attribute(buffer, length, "filename", NULL);

				if (name && ctx->expect_pname &&
				    !strcmp(name, ctx->expect_pname))
					ctx->matched_pname = true;

				ctx->is_file = !!file;

				if (ctx->is_file && ctx->dumpprefix) {
					snprintf(path, sizeof(path), "%s.%u",
					         ctx->dumpprefix, ctx->dumpcount);

					ctx->dumpfd = open(path, O_WRONLY|O_CREAT|O_EXCL, 0644);

					if (ctx->dumpfd < 0)
						fprintf(stderr, "Unable to create file %s: %s\n",
						        path, strerror(errno));
					else
						ctx->dumpcount++;
				}

				xfree(name);
				xfree(file);
			}

			xfree(tok);
		}

		if (buffer && ctx->expect_hvalue &&
		    length == strlen(ctx->expect_hvalue) &&
		    !memcmp(buffer, ctx->expect_hvalue, strlen(ctx->expect_hvalue)))
		    ctx->matched_hvalue = true;

		break;

	case LH_MP_CB_PART_BEGIN:
		/* only buffer non-file data */
		return !ctx->is_file;

	case LH_MP_CB_PART_DATA:
		if (buffer && ctx->dumpfd >= 0)
			write(ctx->dumpfd, buffer, length);
		else if (buffer && ctx->expect_pvalue &&
		    length == strlen(ctx->expect_pvalue) &&
		    !memcmp(buffer, ctx->expect_pvalue, strlen(ctx->expect_pvalue)))
		    ctx->matched_pvalue = true;

		break;

	case LH_MP_CB_PART_END:
		if (ctx->dumpfd >= 0) {
			close(ctx->dumpfd);
			ctx->dumpfd = -1;
		}

		break;

	case LH_MP_CB_ERROR:
		if (buffer && ctx->expect_error &&
		    !strcmp(buffer, ctx->expect_error))
		    ctx->matched_error = true;

		break;

	default:
		break;
	}

	return true;
}

static int run_test(FILE *trace, const char *path, const char *dumpprefix,
                    size_t bufsize)
{
	struct test_context ctx = {
		.bufsize = bufsize ? bufsize : 128,
		.dumpprefix = dumpprefix,
		.dumpfd = -1
	};

	struct lh_mpart *p = NULL;
	bool ok = true;
	char line[4096];
	int rv = -1;
	FILE *file;
	size_t i;

	printf("Testing %-40s ... ", basename((char *)path));

	file = fopen(path, "r");

	if (!file) {
		fprintf(stderr, "Unable to open file: %s\n", strerror(errno));
		goto out;
	}

	p = lh_mpart_new(trace);

	if (!p) {
		fprintf(stderr, "Out of memory\n");
		goto out;
	}

	while (fgets(line, sizeof(line), file)) {
		if (!strncmp(line, "Content-Type: ", 14) &&
		    !lh_mpart_parse_boundary(p, line + 14, NULL)) {

			fprintf(stderr, "Invalid boundary header\n");
			goto out;
		}
		else if (!bufsize && !strncmp(line, "X-Buffer-Size: ", 15)) {
			unsigned int n = 0;
			char *p = NULL;

			n = strtoul(line + 15, &p, 0);

			if (line + 15 == p || *p != '\r' || n < 1 || n > sizeof(line)) {
				fprintf(stderr, "Invalid buffer size\n");
				goto out;
			}

			ctx.bufsize = n;
		}
		else if (!strncmp(line, "X-Expect-", 9)) {
			char *p = NULL, **q = NULL;

			if (!strncmp(line + 9, "Error:", 6)) {
				p = line + 9 + 6;
				q = &ctx.expect_error;
			}
			else if (!strncmp(line + 9, "Part-Name:", 10)) {
				p = line + 9 + 10;
				q = &ctx.expect_pname;
			}
			else if (!strncmp(line + 9, "Part-Value:", 11)) {
				p = line + 9 + 11;
				q = &ctx.expect_pvalue;
			}
			else if (!strncmp(line + 9, "Header-Name:", 12)) {
				p = line + 9 + 12;
				q = &ctx.expect_hname;
			}
			else if (!strncmp(line + 9, "Header-Value:", 13)) {
				p = line + 9 + 13;
				q = &ctx.expect_hvalue;
			}

			if (p && q) {
				while (*p == ' ' || *p == '\t')
					p++;

				for (i = strlen(p) - 1; i > 0; i--)
					if (p[i] != ' ' && p[i] != '\t' &&
					    p[i] != '\r' && p[i] != '\n')
						break;

				p[i + 1] = 0;

				if (!strncmp(p, "urlencoded:", 11))
					*q = lh_urldecode(p + 11, strlen(p + 11), NULL, 0);
				else
					*q = strdup(p);
			}
		}
		else if (!strcmp(line, "\r\n")) {
			break;
		}
	}

	lh_mpart_set_callback(p, test_callback, &ctx);

	while ((i = fread(line, 1, ctx.bufsize, file)) > 0) {
		ok = lh_mpart_parse(p, line, i);

		if (!ok)
			break;
	}

	if (ok)
		lh_mpart_parse(p, NULL, 0);

	if (!ctx.expect_error && p->error) {
		printf("ERROR: Expected parser to finish but got error:\n  [%s]\n",
		       p->error);

		goto out;
	}
	else if (ctx.expect_error && !p->error) {
		printf("ERROR: Expected parser to error with\n  [%s]\n"
		       "but it finished instead\n", ctx.expect_error);

		goto out;
	}
	else if (ctx.expect_error && !ctx.matched_error) {
		printf("ERROR: Expected parser to error with\n  [%s]\n"
		       "but got\n  [%s]\ninstead\n", ctx.expect_error, p->error);

		goto out;
	}
	else if (ctx.expect_pname && !ctx.matched_pname) {
		printf("ERROR: Did not find expected part name [%s]\n",
		       ctx.expect_pname);

		goto out;
	}
	else if (ctx.expect_pvalue && !ctx.matched_pvalue) {
		printf("ERROR: Did not find expected part value [%s]\n",
		       ctx.expect_pvalue);

		goto out;
	}
	else if (ctx.expect_hname && !ctx.matched_hname) {
		printf("ERROR: Did not find expected header name [%s]\n",
		       ctx.expect_hname);

		goto out;
	}
	else if (ctx.expect_hvalue && !ctx.matched_hvalue) {
		printf("ERROR: Did not find expected header value [%s]\n",
		       ctx.expect_hvalue);

		goto out;
	}

	printf("OK\n");
	rv = 0;

out:
	if (p)
		lh_mpart_free(p);

	xfree(ctx.header);
	xfree(ctx.expect_error);
	xfree(ctx.expect_pname);
	xfree(ctx.expect_pvalue);
	xfree(ctx.expect_hname);
	xfree(ctx.expect_hvalue);

	return rv;
}

static int run_tests(FILE *trace, const char *dir, size_t bufsize)
{
	DIR *tests;
	char path[128];
	struct dirent *entry;
	int fails = 0;

	tests = opendir(dir);

	if (!tests) {
		fprintf(stderr, "Unable to open tests: %s\n", strerror(errno));
		return -1;
	}

	while ((entry = readdir(tests)) != NULL) {
		if (entry->d_type == DT_REG) {
			snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

			if (run_test(trace, path, NULL, bufsize))
				fails++;
		}
	}

	closedir(tests);

	if (fails)
		printf("\n%d test cases FAILED!\n", fails);
	else
		printf("\nAll test cases OK!\n");

	return fails;
}

int main(int argc, char **argv)
{
	const char *testfile = NULL;
	const char *testdir = NULL;
	const char *dumpprefix = NULL;
	size_t bufsize = 0;
	FILE *trace = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "vb:d:f:x:")) != -1) {
		switch (opt) {
		case 'v':
			trace = stderr;
			break;

		case 'b':
			bufsize = strtoul(optarg, NULL, 0);

			if (bufsize == 0 || bufsize > 4096) {
				fprintf(stderr, "Invalid buffer size\n");
				return 1;
			}

			break;

		case 'd':
			testdir = optarg;
			break;

		case 'f':
			testfile = optarg;
			break;

		case 'x':
			dumpprefix = optarg;
			break;

		default:
			fprintf(stderr,
			        "Usage: %s [-v] [-b #] {-d <dir>|[-x pfx] -f <file>}\n",
			        argv[0]);

			return 1;
		}
	}

	if (testdir) {
		return run_tests(trace, testdir, bufsize);
	}
	else if (testfile) {
		return run_test(trace, testfile, dumpprefix, bufsize);
	}

	fprintf(stderr, "One of -d or -f is required\n");
	return 1;
}

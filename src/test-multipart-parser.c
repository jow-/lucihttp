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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <dirent.h>
#include <sys/types.h>


static int run_test(FILE *trace, const char *path)
{
	char *expect_error = NULL;
	struct lh_mpart *p;
	bool ok = true;
	char line[128];
	FILE *file;
	size_t i;

	printf("Testing %-40s ... ", basename((char *)path));

	file = fopen(path, "r");

	if (!file) {
		fprintf(stderr, "Unable to open file: %s\n", strerror(errno));
		return -1;
	}

	p = lh_mpart_new(trace);

	if (!p) {
		fprintf(stderr, "Out of memory\n");
		return -1;
	}

	while (fgets(line, sizeof(line), file)) {
		if (!strncmp(line, "Content-Type: ", 14) &&
		    !lh_mpart_parse_boundary(p, line + 14, NULL)) {

			fprintf(stderr, "Invalid boundary header\n");
			lh_mpart_free(p);
			return -1;
		}
		else if (!strncmp(line, "X-Expect-Error: ", 16)) {
			for (i = strlen(line) - 1; i > 0; i--)
				if (line[i] != ' ' && line[i] != '\t' &&
				    line[i] != '\r' && line[i] != '\n')
					break;

			line[i + 1] = 0;
			expect_error = strdup(line + 16);
		}
		else if (!strcmp(line, "\r\n")) {
			break;
		}
	}

	while ((i = fread(line, 1, sizeof(line), file)) > 0) {
		ok = lh_mpart_parse(p, line, i);

		if (!ok)
			break;
	}

	if (ok)
		lh_mpart_parse(p, NULL, 0);

	if (!expect_error && p->error) {
		printf("ERROR: Expected parser to finish but got error:\n  [%s]\n",
		       p->error);

		lh_mpart_free(p);
		return -1;
	}
	else if (expect_error && !p->error) {
		printf("ERROR: Expected parser to error with\n  [%s]\n"
		       "but it finished instead\n", expect_error);

		lh_mpart_free(p);
		return -1;
	}
	else if (expect_error && p->error) {
		if (strcmp(expect_error, p->error)) {
			printf("ERROR: Expected parser to error with\n  [%s]\n"
			       "but got\n  [%s]\ninstead\n", expect_error, p->error);

			lh_mpart_free(p);
			return -1;
		}
	}

	printf("OK\n");
	lh_mpart_free(p);
	return 0;
}

static int run_tests(FILE *trace, const char *dir)
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

			if (run_test(trace, path))
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
	FILE *trace = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "vd:f:")) != -1) {
		switch (opt) {
		case 'v':
			trace = stderr;
			break;

		case 'd':
			testdir = optarg;
			break;

		case 'f':
			testfile = optarg;
			break;

		default:
			fprintf(stderr, "Usage: %s [-v] {-d <dir>|-f <file>}\n",
			        argv[0]);

			return 1;
		}
	}

	if (testdir) {
		return run_tests(trace, testdir);
	}
	else if (testfile) {
		return run_test(trace, testfile);
	}

	fprintf(stderr, "One of -d or -f is required\n");
	return 1;
}

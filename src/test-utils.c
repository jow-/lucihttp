/*
 * lucihttp - HTTP utility library - urlencoded data parser tester
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

#include <lucihttp/utils.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>


int main(int argc, char **argv)
{
	const char *encode = NULL;
	const char *decode = NULL;
	const char *hval = NULL;
	const char *attr = NULL;
	unsigned int flags = 0;
	size_t len;
	char *rv;
	int opt;

	while ((opt = getopt(argc, argv, "e:d:f:v:a:")) != -1) {
		switch (opt) {
		case 'e':
			encode = optarg;
			break;

		case 'd':
			decode = optarg;
			break;

		case 'f':
			if (!strcmp(optarg, "full"))
				flags |= LH_URLENCODE_FULL;
			else if (!strcmp(optarg, "strict"))
				flags |= LH_URLDECODE_STRICT;
			else if (!strcmp(optarg, "if-needed"))
				flags |= LH_URLDECODE_IF_NEEDED;
			else if (!strcmp(optarg, "space-plus"))
				flags |= LH_URLENCODE_SPACE_PLUS;
			else if (!strcmp(optarg, "keep-plus"))
				flags |= LH_URLDECODE_KEEP_PLUS;
			break;

		case 'v':
			hval = optarg;
			break;

		case 'a':
			attr = optarg;
			break;

		default:
			fprintf(stderr, "Usage: %s [-f flag ...] -d <string>\n", argv[0]);
			fprintf(stderr, "       %s [-f flag ...] -e <string>\n", argv[0]);
			fprintf(stderr, "       %s -v <string> -a <string>\n", argv[0]);

			return 1;
		}
	}

	if (encode) {
		rv = lh_urlencode(encode, 0, &len, flags);
		printf("length=%zd encoded=%s\n", len, rv);
		return 0;
	}
	else if (decode) {
		rv = lh_urldecode(decode, 0, &len, flags);
		printf("length=%zd decoded=%s\n", len, rv);
		return 0;
	}
	else if (hval && attr) {
		rv = lh_header_attribute(hval, 0, attr, &len);
		printf("length=%zd value=%s\n", len, rv);
		return 0;
	}

	fprintf(stderr, "One of -d or -e or -v & -a is required\n");
	return 1;
}

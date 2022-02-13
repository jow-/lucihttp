#!/usr/bin/env -S ucode -R

let lh = require("lucihttp");

let encode_tests = [
	"http://example.org/some space",
		0,
		"http://example.org/some%20space",

	"http://example.org/some space",
		lh.ENCODE_SPACE_PLUS,
		"http://example.org/some+space",

	"http://example.org/some space",
		lh.ENCODE_FULL,
		"http%3A%2F%2Fexample.org%2Fsome%20space",

	"http://example.org/some space",
		lh.ENCODE_IF_NEEDED,
		"http://example.org/some%20space",

	"http://example.org/",
		lh.ENCODE_IF_NEEDED,
		null,

	"http://example.org/some space",
		lh.ENCODE_FULL | lh.ENCODE_IF_NEEDED | lh.ENCODE_SPACE_PLUS,
		"http%3A%2F%2Fexample.org%2Fsome+space"
];

let decode_tests = [
	"test%20test%test+test",
		lh.DECODE_PLUS,
		"test test%test test",

	"test%20test%test+test",
		lh.DECODE_STRICT,
		null,

	"test%20test%test+test",
		lh.DECODE_PLUS | lh.DECODE_IF_NEEDED,
		"test test%test test",

	"test test%test test",
		lh.DECODE_IF_NEEDED,
		null,

	"test%20test%test+test",
		lh.DECODE_KEEP_PLUS,
		"test test%test+test"
];

let header_tests = [
	"foo/bar;param=test",
		"param",
		"test",

	"foo/bar; foo=bar; param=test; bar=baz",
		"param",
		"test",

	"foo/bar; param=\"test test\"",
		"param",
		"test test",

	"foo/bar; param=\"test \\\" test\"",
		"param",
		"test \" test",

	"foo/bar; param=\"\\%22test\"",
		"param",
		"\"test",

	"foo/bar; param=invalid@token",
		"param",
		null,

	"foo/bar  ; param=x",
		null,
		"foo/bar",

	"foo-bar; param=test",
		null,
		"foo-bar",

	"foo-bar baz; param=test",
		null,
		null,

	"foo/bar/baz; param=test",
		null,
		null,

	"foo/bar; param=test",
		null,
		"foo/bar"
];

print("Performing URL encode tests ");

let input, arg, expected;

for (let i = 0; i < length(encode_tests); i += 3) {
	let input = encode_tests[i+0],
	    arg = encode_tests[i+1],
	    expected = encode_tests[i+2];

	let result = lh.urlencode(input, arg);

	if (expected == result) {
		print(".");
	}
	else {
		printf("\nERROR: Expected\n [%s]\nbut got\n [%s]\n instead.\n", expected, result);
		exit(1);
	}
}

print(" OK\n");

print("Performing URL decode tests ");

for (let i = 0; i < length(decode_tests); i += 3) {
	let input = decode_tests[i+0],
	    arg = decode_tests[i+1],
	    expected = decode_tests[i+2];

	let result = lh.urldecode(input, arg);

	if (expected == result) {
		print(".");
	}
	else {
		printf("\nERROR: Expected\n [%s]\nbut got\n [%s]\n instead.\n", expected, result);
		exit(1);
	}
}

print(" OK\n");

print("Performing header decode tests ");

for (let i = 0; i < length(header_tests); i += 3) {
	let input = header_tests[i+0],
	    arg = header_tests[i+1],
	    expected = header_tests[i+2];

	let result = lh.header_attribute(input, arg);

	if (expected == result) {
		print(".");
	}
	else {
		printf("\nERROR: Expected\n [%s]\nbut got\n [%s]\n instead.\n", expected, result);
		exit(1);
	}
}

print(" OK\n");

exit(0);

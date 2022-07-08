require "lucihttp"

local function mkflags(f)
	local _, n
	local rv = 0
	for _, n in ipairs(f) do
		rv = rv + lucihttp[n]
	end
	return rv
end

local encode_tests = {
	"http://example.org/some space",
		{ },
		"http://example.org/some%20space",

	"http://example.org/some space",
		{ "ENCODE_SPACE_PLUS" },
		"http://example.org/some+space",

	"http://example.org/some space",
		{ "ENCODE_FULL" },
		"http%3A%2F%2Fexample.org%2Fsome%20space",

	"http://example.org/some space",
		{ "ENCODE_IF_NEEDED" },
		"http://example.org/some%20space",

	"http://example.org/",
		{ "ENCODE_IF_NEEDED" },
		nil,

	"http://example.org/some space",
		{ "ENCODE_FULL", "ENCODE_IF_NEEDED", "ENCODE_SPACE_PLUS" },
		"http%3A%2F%2Fexample.org%2Fsome+space"
}

local decode_tests = {
	"test%20test%test+test",
		{ },
		"test test%test+test",

	"test%20test%test+test",
		{ "DECODE_STRICT" },
		nil,

	"test%20test%test+test",
		{ "DECODE_IF_NEEDED" },
		"test test%test+test",

	"test test%test test",
		{ "DECODE_IF_NEEDED" },
		nil,

	"test%20test%test+test",
		{ "DECODE_KEEP_PLUS" },
		"test test%test+test",

	"test%20test%test+test",
		{ "DECODE_PLUS" },
		"test test%test test"
}

local header_tests = {
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
		nil,

	"foo/bar  ; param=x",
		nil,
		"foo/bar",

	"foo-bar; param=test",
		nil,
		"foo-bar",

	"foo-bar baz; param=test",
		nil,
		nil,

	"foo/bar/baz; param=test",
		nil,
		nil,

	"foo/bar; param=test",
		nil,
		"foo/bar",

	"foo/bar; foo=x; foobar=y",
		"foobar",
		"y"
}

io.write("Performing URL encode tests ")

local i, input, arg, expected

i = 1
while encode_tests[i] do
	input, arg, expected =
		encode_tests[i+0], encode_tests[i+1], encode_tests[i+2]

	local result = lucihttp.urlencode(input, mkflags(arg))

	if expected == result then
		io.write(".")
	else
		io.write(string.format("\nERROR: Expected\n [%s]\nbut got\n [%s]\n instead.\n",
			expected or "(nil)", result or "(nil)"))
		os.exit(1)
	end

	i = i + 3
end

io.write(" OK\n")

io.write("Performing URL decode tests ")

i = 1
while decode_tests[i] do
	input, arg, expected =
		decode_tests[i+0], decode_tests[i+1], decode_tests[i+2]

	local result = lucihttp.urldecode(input, mkflags(arg))

	if expected == result then
		io.write(".")
	else
		io.write(string.format("\nERROR: Expected\n [%s]\nbut got\n [%s]\n instead.\n",
			expected or "(nil)", result or "(nil)"))
		os.exit(1)
	end

	i = i + 3
end

io.write(" OK\n")

io.write("Performing header decode tests ")

i = 1
while header_tests[i] do
	input, arg, expected =
		header_tests[i+0], header_tests[i+1], header_tests[i+2]

	local result = lucihttp.header_attribute(input, arg)

	if expected == result then
		io.write(".")
	else
		io.write(string.format("\nERROR: Expected\n [%s]\nbut got\n [%s]\n instead.\n",
			expected or "(nil)", result or "(nil)"))
		os.exit(1)
	end

	i = i + 3
end

io.write(" OK\n")

os.exit(0)

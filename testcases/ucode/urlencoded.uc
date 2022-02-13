#!/usr/bin/env -S ucode -R

let lh = require("lucihttp");

let data = { };
let parser, name, value;

// parser data callback
function callback(what, buffer, length) {
	// we encountered the start of a tuple
	if (what == parser.TUPLE) {
		name = null;
		value = null;
	}

	// parser extracted a parameter name
	else if (what == parser.NAME) {
		name = lh.urldecode(buffer);
	}

	// parser extracted a parameter value
	else if (what == parser.VALUE && name) {
		data[name] = lh.urldecode(buffer) || "";
	}

	else if (what == parser.ERROR) {
		// this callback is invoked when the parser encounters an error state, the
		// buffer argument will hold the error message in this case
		printf("ERROR %s\n", buffer);
	}

	// return true to instruct parser to buffer and reassemble values spread
	// over buffer boundaries. This ensures that callbacks are only invoked once
	// with the complete data.
	return true;
}


// instantiate parser, specify data callback
parser = lh.urlencoded_parser(callback);

// feed data chunk by chunk
parser.parse("example=");
parser.parse("test&field");
parser.parse("=value%20with%20%22quotes");
parser.parse("%22%20and%20spaces%21");
parser.parse("&empty1=&empty2&test=1");

// notify EOF
parser.parse(null);


// print gathered values:

for (let name in sort(keys(data)))
	printf("Parameter:\t%s\tValue:\t%s\n", name, data[name]);

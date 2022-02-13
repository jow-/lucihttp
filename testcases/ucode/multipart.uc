#!/usr/bin/env -S ucode -R

let lh = require("lucihttp");
let fs = require("fs");

let data = { };
let parser, file, header, key, val;

// file data callback
function file_cb(file, data, length, eof) {
	// file data completely read, we get eof == true
	if (eof) {
		// close handle
		file.fd.close();
	}

	// this is the first or subsequent chunk of file data
	else {
		// the first invocation of the file callback for this part
		if (!file.fd)
			file.fd = fs.open("/dev/stdout", "w");

		// write buffer data to file
		file.fd.write("[File callback] " + data + "\n");
	}
}

// parser data callback
function callback(what, buffer, length) {
	// we encountered the start of a multipart partition
	if (what == parser.PART_INIT) {
		key = null;
		val = null;
		filename = null;
	}

	// parser extracted a part header name
	else if (what == parser.HEADER_NAME) {
		header = lc(buffer);
	}

	// parser extracted a part header value
	else if (what == parser.HEADER_VALUE && header) {
		// if we found a Content-Disposition header, extract name and filename
		// if there is a filename, we're dealing with file upload data
		if (lc(header) == "content-disposition") {
			let filename = lh.header_attribute(buffer, "filename");
			file = filename ? { name: filename } : null;
			key = lh.header_attribute(buffer, "name");
			val = filename;
		}
	}

	// parser is about to start the partition data
	else if (what == parser.PART_BEGIN) {
		// if this part is a file upload, instruct parser to not buffer (return false)
		// else enable buffering (return true)
		return !file;
	}

	else if (what == parser.PART_DATA) {
		// if this part is a file upload, we're in unbuffered mode and may be invoked
		// multiple times, forward data to the file callback
		if (file)
			file_cb(file, buffer, length, false);

		// ... else we're only invoked once with the entire partition data buffered
		else
			val = buffer;
	}

	else if (what == parser.PART_END) {
		// if this part is a file upload then invoke the file callback once more with
		// the eof flag set to true, so that the callback can finalize the file
		// also set the parameter value to the filename
		if (file) {
			file_cb(file, nil, 0, true);
			data[key] = file.name;
		}

		// ... else assign the remembered buffered partition data as parameter value
		else if (key) {
			data[key] = val;
		}

		// reset state
		key = null;
		val = null;
		file = null;
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


// instantiate parser, extract boundary from content-type header value, specify data callback
parser = lh.multipart_parser("multipart/form-data; boundary=AaB03x", callback);

// feed data chunk by chunk
parser.parse("--AaB03x\r\nContent-Disposition: form-data; name=\"example\"\r\n\r\n");
parser.parse("This is an example\r\n--AaB03x\r\n");
parser.parse("Content-Disposition: form-data; name=\"file-1\"; filename=\"file-1.txt\"\r\n\r\n");
parser.parse("This is the content of file-1.txt\r\n--AaB03x\r\n");
parser.parse("Content-Disposition: form-data; name=\"file-2\"; filename=\"file-2.txt\"\r\n\r\n");
parser.parse("This is the content of file-2.txt\r\n--AaB03x--\r\n");

// notify EOF
parser.parse(null);


// print gathered values:

for (let key in sort(keys(data)))
	printf("Parameter:\t%s\tValue:\t%s\n", key, data[key]);


//
// Test maximum chunk size enforcement
//

parser = lh.multipart_parser("multipart/form-data; boundary=AaB03x",
	function(what, buffer, length) {
		print([ what, buffer, length ], "\n");
		return true;
	}, 1024);

parser.parse("--AaB03x\r\nContent-Disposition: form-data; name=\"example\"\r\n\r\n");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
parser.parse("\r\n--AaB03x--\r\n");
parser.parse(null);

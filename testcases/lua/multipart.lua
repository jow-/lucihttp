require "lucihttp"

local data = { }
local parser, file, header, key, val

-- file data callback
local function file_cb(file, data, length, eof)
	-- file data completely read, we get eof == true
	if eof then
		-- close handle
		file.fd:close()

	-- this is the first or subsequent chunk of file data
	else
		-- the first invocation of the file callback for this part
		if not file.fd then
			file.fd = io.open("/dev/stdout", "w")
		end

		-- write buffer data to file
		file.fd:write("[File callback] ", data, "\n")
	end
end

-- parser data callback
local function callback(what, buffer, length)
	-- we encountered the start of a multipart partition
	if what == parser.PART_INIT then
		key, val, filename = nil, nil, nil

	-- parser extracted a part header name
	elseif what == parser.HEADER_NAME then
		header = buffer:lower()

	-- parser extracted a part header value
	elseif what == parser.HEADER_VALUE and header then
		-- if we found a Content-Disposition header, extract name and filename
		-- if there is a filename, we're dealing with file upload data
		if header:lower() == "content-disposition" then
			local filename = lucihttp.header_attribute(buffer, "filename")
			file = filename and { name = filename }
			key = lucihttp.header_attribute(buffer, "name")
			val = filename
		end

	-- parser is about to start the partition data
	elseif what == parser.PART_BEGIN then
		-- if this part is a file upload, instruct parser to not buffer (return false)
		-- else enable buffering (return true)
		return not file

	elseif what == parser.PART_DATA then
		-- if this part is a file upload, we're in unbuffered mode and may be invoked
		-- multiple times, forward data to the file callback
		if file then
			file_cb(file, buffer, length, false)

		-- ... else we're only invoked once with the entire partition data buffered
		else
			val = buffer
		end

	elseif what == parser.PART_END then
		-- if this part is a file upload then invoke the file callback once more with
		-- the eof flag set to true, so that the callback can finalize the file
		-- also set the parameter value to the filename
		if file then
			file_cb(file, nil, 0, true)
			data[key] = file.name

		-- ... else assign the remembered buffered partition data as parameter value
		elseif key then
			data[key] = val
		end

		-- reset state
		key, val, file = nil, nil, nil

	elseif what == parser.ERROR then
		-- this callback is invoked when the parser encounters an error state, the
		-- buffer argument will hold the error message in this case
		print("ERROR", buffer)
	end

	-- return true to instruct parser to buffer and reassemble values spread
	-- over buffer boundaries. This ensures that callbacks are only invoked once
	-- with the complete data.
	return true
end


-- instantiate parser, extract boundary from content-type header value, specify data callback
parser = lucihttp.multipart_parser("multipart/form-data; boundary=AaB03x", callback)

-- feed data chunk by chunk
parser:parse("--AaB03x\r\nContent-Disposition: form-data; name=\"example\"\r\n\r\n")
parser:parse("This is an example\r\n--AaB03x\r\n")
parser:parse("Content-Disposition: form-data; name=\"file-1\"; filename=\"file-1.txt\"\r\n\r\n")
parser:parse("This is the content of file-1.txt\r\n--AaB03x\r\n")
parser:parse("Content-Disposition: form-data; name=\"file-2\"; filename=\"file-2.txt\"\r\n\r\n")
parser:parse("This is the content of file-2.txt\r\n--AaB03x--\r\n")

-- notify EOF
parser:parse(nil)


-- print gathered values:

local keys = {}

for key, val in pairs(data) do
	keys[#keys+1] = key
end

table.sort(keys)

for _, key in ipairs(keys) do
	print("Parameter:", key, "Value:", data[key])
end


--
-- Test maximum chunk size enforcement
--

parser = lucihttp.multipart_parser("multipart/form-data; boundary=AaB03x",
	function(what, buffer, length)
		print(what, buffer, length)
		return true
	end, 1024)

parser:parse("--AaB03x\r\nContent-Disposition: form-data; name=\"example\"\r\n\r\n")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
parser:parse("\r\n--AaB03x--\r\n")
parser:parse(nil)

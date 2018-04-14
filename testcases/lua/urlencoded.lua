require "lucihttp"

local data = { }
local parser, name, value

-- parser data callback
local function callback(what, buffer, length)
	-- we encountered the start of a tuple
	if what == parser.TUPLE then
		name, value = nil, nil

	-- parser extracted a parameter name
	elseif what == parser.NAME then
		name = lucihttp.urldecode(buffer)

	-- parser extracted a parameter value
	elseif what == parser.VALUE and name then
		data[name] = lucihttp.urldecode(buffer) or ""

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


-- instantiate parser, specify data callback
parser = lucihttp.urlencoded_parser(callback)

-- feed data chunk by chunk
parser:parse("example=")
parser:parse("test&field")
parser:parse("=value%20with%20%22quotes")
parser:parse("%22%20and%20spaces%21")
parser:parse("&empty1=&empty2&test=1")

-- notify EOF
parser:parse(nil)


-- print gathered values:

local names = {}

for name, value in pairs(data) do
	names[#names+1] = name
end

table.sort(names)

for _, name in ipairs(names) do
	print("Parameter:", name, "value:", data[name])
end

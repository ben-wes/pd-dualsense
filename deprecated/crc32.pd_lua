local crc32 = pd.Class:new():register("crc32")

function crc32:initialize(sel, atoms)
    self.inlets = 1
    self.outlets = 1
	-- precompute CRC32 table
	self.CRC32 = {}
	for i = 0, 255 do
	    local crc = i
	    for j = 1, 8 do
	        if (crc & 1) ~= 0 then
	            crc = 0xEDB88320 ~ (crc >> 1)
	        else
	            crc = crc >> 1
	        end
	    end
	    self.CRC32[i] = crc
	end
    return true
end

function crc32:hash(input)
    local hash = 0xFFFFFFFF
    local CRC32 = self.CRC32
    for i = 1, #input do
        hash = CRC32[(hash ~ input[i]) & 0xFF] ~ (hash >> 8)
    end
    hash = ~hash & 0xFFFFFFFF
    return {(hash >> 24) & 0xFF, (hash >> 16) & 0xFF, (hash >> 8) & 0xFF, hash & 0xFF}
end

function crc32:in_1_list(x)
	self:outlet(1, "list", self:hash(x))
end

function crc32:in_1_float(x)
	self:outlet(1, "list", self:hash({x}))
end

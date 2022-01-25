local f = io.open("cmd/zs/foo.zs")
local src = f:read("a")
f:close()
print(src)

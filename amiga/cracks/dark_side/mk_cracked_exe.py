import struct, sys, os

# Hunk/Block identifiers.
HUNK_HEADER  = 0x3f3
HUNK_CODE    = 0x3e9
HUNK_DATA    = 0x3ea
HUNK_BSS     = 0x3eb
HUNK_RELOC32 = 0x3ec
HUNK_END     = 0x3f2

with open(sys.argv[1], 'rb') as f:
    dat = f.read()

d = list(struct.unpack('<8I', dat[:4*8]))
a = list(struct.unpack('<8I', dat[4*8:8*8]))
dat = dat[8*8:]

# All longword aligned
assert a[0]&3==0
assert a[1]&3==0
assert a[2]&3==0

code_start = a[0]
code_end = a[2]
base = a[1]
print("Code at %08x to %08x -> %08x" % (code_start, code_end, base))

# Find the start of relocations by scanning back from the code start
i = code_start - 4
nr, = struct.unpack('>I', dat[i:i+4])
assert nr == 0  # Relocs should be terminated with nul
nr = 1
while nr != 0: # Scan for hunk number (must be zero)
    i -= 4
    nr, = struct.unpack('>I', dat[i:i+4])
i -= 4
nr, = struct.unpack('>I', dat[i:i+4])
assert nr == (code_start-i)//4-3 # Sanity-check the #relocs count
reloc_start = i
print("Relocations start at %08x" % (reloc_start))

# Extract the relocated code and the applied relocations
code = bytearray(dat[code_start:code_end])
relocs = dat[reloc_start:code_start]

# Un-relocate the code
for x in list(struct.unpack(f'>{nr}I', relocs[2*4:(2+nr)*4])):
    y, = struct.unpack('>I', code[x:x+4])
    code[x:x+4] = struct.pack('>I', y - base)

# Generate the output executable
out = bytearray()
out += struct.pack('>6I', HUNK_HEADER, 0, 1, 0, 0, (code_end-base)//4)
out += struct.pack('>2I', HUNK_CODE, (code_end-code_start)//4)
out += code
out += struct.pack('>I', HUNK_RELOC32)
out += relocs
out += struct.pack('>I', HUNK_END)

with open(sys.argv[2], 'wb') as f:
    f.write(out)


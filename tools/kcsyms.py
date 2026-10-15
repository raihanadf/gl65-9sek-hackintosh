# list symbols of one kext inside a fileset kernel collection
import struct, sys
kc, base, pat = sys.argv[1], int(sys.argv[2]), sys.argv[3]
data = open(kc, 'rb').read()
magic, cpu, sub, ftype, ncmds, size, flags, res = struct.unpack_from('<IiiIIIII', data, base)
off = base + 32
for _ in range(ncmds):
    cmd, csize = struct.unpack_from('<II', data, off)
    if cmd == 0x2:  # LC_SYMTAB
        symoff, nsyms, stroff, strsize = struct.unpack_from('<IIII', data, off + 8)
        for i in range(nsyms):
            strx, ntype, nsect, ndesc, nvalue = struct.unpack_from('<IBBHQ', data, symoff + i * 16)
            end = data.index(b'\0', stroff + strx)
            name = data[stroff + strx:end].decode()
            if pat in name:
                print(hex(nvalue), name)
    off += csize

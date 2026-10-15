# print bytes at a vm address of one kext inside a fileset kernel collection, as llvm-mc input
import struct, sys
kc, base, addr, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 16), int(sys.argv[4])
data = open(kc, 'rb').read()
ncmds = struct.unpack_from('<I', data, base + 16)[0]
off = base + 32
for _ in range(ncmds):
    cmd, csize = struct.unpack_from('<II', data, off)
    if cmd == 0x19:  # LC_SEGMENT_64
        vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', data, off + 24)
        if vmaddr <= addr < vmaddr + filesize:
            start = fileoff + addr - vmaddr
            print(' '.join('0x%02x' % b for b in data[start:start + n]))
            break
    off += csize

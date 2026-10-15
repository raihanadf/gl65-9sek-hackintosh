# replay the full solveEarlyMask detection (flags, queues, filter flag test, event source + signal slot)
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
kc, base, spurious, flt = sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 16), int(sys.argv[4], 16)
data = open(kc, 'rb').read()
ncmds = struct.unpack_from('<I', data, base + 16)[0]
segs, off = [], base + 32
for _ in range(ncmds):
    cmd, csize = struct.unpack_from('<II', data, off)
    if cmd == 0x19: segs.append(struct.unpack_from('<QQQQ', data, off + 24))
    off += csize
def mem(a, n):
    for vm, vs, fo, fs in segs:
        if vm <= a < vm + fs: return data[fo + a - vm:fo + a - vm + n]
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
def modrm(ins):
    m = ins.modrm; return m >> 6, (m >> 3) & 7, m & 7
flags = bit = None
for i, ins in enumerate(md.disasm(mem(spurious, 64), spurious)):
    if i == 4: break
    mod, reg, rm = modrm(ins) if ins.modrm else (None,)*3
    if ins.opcode[0] == 0xf6 and reg == 0 and rm == 7 and not ins.rex & 1 and mod == 2:
        flags, bit = ins.disp, ins.operands[1].imm
io = bytes.fromhex('0fb74848488b5050c1e1040fb74c0a0e8b5058d1ea31caf6c20174')
adm = bytes.fromhex('0fb74818488b5020c1e1040fb74c0a0e8b4028d1e831c8a801')
sig = bytes.fromhex('488b07ff90')
queues = source = slot = None; tests = False
for i, ins in enumerate(md.disasm(mem(flt, 600), flt)):
    if i == 96: break
    nxt = mem(ins.address + ins.size, 64)
    mod, reg, rm = modrm(ins) if ins.modrm else (None,)*3
    rbx = rm == 3 and not ins.rex & 1 and mod == 2
    if ins.opcode[0] == 0x8b and ins.rex & 8 and reg == 0 and rbx and nxt[:len(io)] == io and nxt[len(io)+1:len(io)+1+len(adm)] == adm:
        queues = ins.disp
    if ins.opcode[0] == 0xf6 and reg == 0 and rbx:
        tests = ins.disp == flags and ins.operands[1].imm == bit
    if ins.opcode[0] == 0x8b and ins.rex & 8 and reg == 7 and rbx and nxt[:5] == sig:
        source, slot = ins.disp, struct.unpack('<I', nxt[5:9])[0]; break
ok = flags is not None and bit and queues and tests and source and slot
print('flags', flags and hex(flags), 'bit', bit and hex(bit), 'queues', queues and hex(queues), 'filter-tests-same-flag', tests, 'source', source and hex(source), 'slot', slot and hex(slot), '=> ROUTE' if ok else '=> SKIP')

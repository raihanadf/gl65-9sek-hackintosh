# end to end: our compiled filterInterruptRequest + apple's real sequoia functions, watching nvme registers
import random, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RSP, UC_X86_REG_RAX

REBASE = 0xffffff8000000000
kc, kbase, kext, ours = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4], 16)
apple = {k: int(v, 16) - REBASE for k, v in (a.split('=') for a in sys.argv[5:10])}
cases = int(sys.argv[10])

def segments(data, base):
    ncmds = struct.unpack_from('<I', data, base + 16)[0]
    off, out = base + 32, []
    for _ in range(ncmds):
        cmd, csize = struct.unpack_from('<II', data, off)
        if cmd == 0x19 and data[off + 8:off + 24].rstrip(b'\0') != b'__LINKEDIT':
            out.append(struct.unpack_from('<QQQQ', data, off + 24))
        off += csize
    return out

kcdata, kxdata = open(kc, 'rb').read(), open(kext, 'rb').read()
kcsegs = [(vm - REBASE, vs, fo, fs) for vm, vs, fo, fs in segments(kcdata, kbase)]
KX = 0x40000000
kxsegs = [(vm + KX, vs, fo, fs) for vm, vs, fo, fs in segments(kxdata, 0)]

# read the plugin field addresses straight from our compiled function
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
textvm, _, textfo, _ = [s for s in segments(kxdata, 0) if s[0] <= ours < s[0] + s[1]][0]
fields = []
for ins in md.disasm(kxdata[textfo + ours - textvm:textfo + ours - textvm + 0x120], ours + KX):
    for op in ins.operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            fields.append(ins.address + ins.size + op.mem.disp)
    if ins.mnemonic == 'ret':
        break
flags_offs, msix_flag, clear_msix, write32, queues_offs, spurious = fields[:6]
print('fields', [hex(f - KX) for f in fields])

CTRL, VT, Q, ENT, EVS, EVT, MSIX, STUBS, STACK, STOP, BAR = (0x10000000 + i * 0x10000 for i in range(11))

def run(state, msix, registered):
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    for vm, vs, fo, fs in kcsegs + kxsegs:
        start, end = vm & ~0xfff, (vm + vs + 0xfff) & ~0xfff
        uc.mem_map(start, end - start)
        uc.mem_write(vm, kcdata[fo:fo + fs] if vm < KX else kxdata[fo:fo + fs])
    for a in (CTRL, VT, Q, ENT, EVS, EVT, MSIX, STUBS, STACK, STOP, BAR):
        uc.mem_map(a, 0x10000)
    uc.mem_write(STUBS, b'\xc3' * 0x100)
    # plugin state as solveEarlyMask/handleController leave it
    uc.mem_write(flags_offs, struct.pack('<Q', 0x191))
    uc.mem_write(msix_flag, bytes([0x10]))
    uc.mem_write(queues_offs, struct.pack('<Q', 0x188))
    for f, k in ((clear_msix, 'clear'), (write32, 'write32'), (spurious, 'spurious')):
        uc.mem_write(f, struct.pack('<Q', apple[k]))
    # controller object wired to apple's real register helpers
    uc.mem_write(CTRL, struct.pack('<Q', VT))
    uc.mem_write(VT + 0x960, struct.pack('<Q', apple['write32']))
    uc.mem_write(CTRL + 0x150, struct.pack('<Q', BAR))
    uc.mem_write(CTRL + 0x188, struct.pack('<Q', Q))
    uc.mem_write(CTRL + 0x191, bytes([0x10 if msix else 0]))
    uc.mem_write(CTRL + 0x110, struct.pack('<Q', EVS))
    uc.mem_write(EVS, struct.pack('<Q', EVT))
    uc.mem_write(EVT + 0x238, struct.pack('<Q', STUBS))
    uc.mem_write(CTRL + 0x158, struct.pack('<Q', MSIX))
    uc.mem_write(CTRL + 0x160, struct.pack('<I', 2))
    for i, (cq, (head, phase, statuses)) in enumerate(zip((0x40, 0x10), state)):
        table = ENT + i * 0x1000
        uc.mem_write(Q + cq + 0x08, struct.pack('<H', head))
        uc.mem_write(Q + cq + 0x10, struct.pack('<Q', table))
        uc.mem_write(Q + cq + 0x18, struct.pack('<I', phase))
        for n, s in enumerate(statuses):
            uc.mem_write(table + n * 16 + 0x0e, struct.pack('<H', s))
    events = []
    uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: events.append('signal'), begin=STUBS, end=STUBS)
    def wr(uc, access, addr, size, value, _):
        if BAR <= addr < BAR + 0x100:
            events.append(('intms' if addr - BAR == 0xc else 'intmc' if addr - BAR == 0x10 else hex(addr - BAR), value))
        elif MSIX <= addr < MSIX + 0x100:
            events.append(('msix-mask', (addr - MSIX) // 16, value))
    uc.hook_add(UC_HOOK_MEM_WRITE, wr)
    sp = STACK + 0x8000
    uc.mem_write(sp, struct.pack('<Q', STOP))
    uc.reg_write(UC_X86_REG_RSP, sp)
    uc.reg_write(UC_X86_REG_RDI, CTRL)
    uc.reg_write(UC_X86_REG_RSI, EVS)
    uc.emu_start(ours + KX, STOP, count=20000)
    masked = [struct.unpack('<I', bytes(uc.mem_read(MSIX + n * 16 + 0xc, 4)))[0] for n in range(2)]
    return events, masked, uc.reg_read(UC_X86_REG_RAX) & 0xff

def run_apple(state, msix):
    # apple's filter called directly, no wrapper involved
    global ours
    saved, ours = ours, apple['filter'] - KX
    try:
        return run(state, msix, False)
    finally:
        ours = saved

random.seed(2)
bad = 0
seen = {}
for msix in (False, True):
    for _ in range(cases):
        state = [(random.randrange(256), random.getrandbits(32), [random.getrandbits(16) for _ in range(256)]) for _ in range(2)]
        apple_ev, apple_mask, _ = run_apple(state, msix)
        ev, mask, ret = run(state, msix, True)
        signal = 'signal' in ev
        apple_signal = 'signal' in apple_ev
        if msix:
            expect = mask == [1, 1] if signal else (mask == [0, 0] and ev[:2] == [('msix-mask', 0, 1), ('msix-mask', 1, 1)])
        else:
            expect = ev == ([('intms', 1), 'signal'] if signal else [('intms', 1), ('intmc', 1)])
        ok = signal == apple_signal and expect and ret == 0
        key = (msix, signal)
        seen[key] = seen.get(key, 0) + 1
        if not ok:
            bad += 1
            print('BAD', msix, ev, mask, apple_ev, ret)
print('cases', 2 * cases, 'by (msix, signal):', seen, 'failures', bad)

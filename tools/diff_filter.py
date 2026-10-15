# differential test: apple's real FilterInterruptRequest (emulated) vs our queue check (native copy)
import ctypes, random, struct, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RSP, UC_X86_REG_RIP

REBASE = 0xffffff8000000000
kc, base, filt = sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 16) - REBASE
q_off, flags_off, msix_bit = int(sys.argv[4], 16), int(sys.argv[5], 16), int(sys.argv[6], 16)
mirror = ctypes.CDLL(sys.argv[7])
mirror.pending.restype = ctypes.c_bool

data = open(kc, 'rb').read()
ncmds = struct.unpack_from('<I', data, base + 16)[0]
segs, off = [], base + 32
for _ in range(ncmds):
    cmd, csize = struct.unpack_from('<II', data, off)
    if cmd == 0x19:
        if data[off + 8:off + 24].rstrip(b'\0') != b'__LINKEDIT':
            vm, vs, fo, fs = struct.unpack_from('<QQQQ', data, off + 24)
            segs.append((vm - REBASE, vs, fo, fs))
    off += csize

CTRL, VT, Q, ENT, EVS, EVT, MSIX, STUBS, STACK, STOP = (0x10000000 + i * 0x10000 for i in range(10))
MASK_STUB, SIGNAL_STUB = STUBS, STUBS + 0x10

def run(state, msix):
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    for vm, vs, fo, fs in segs:
        start, end = vm & ~0xfff, (vm + vs + 0xfff) & ~0xfff
        uc.mem_map(start, end - start)
        uc.mem_write(vm, data[fo:fo + fs])
    for a in (CTRL, VT, Q, ENT, EVS, EVT, MSIX, STUBS, STACK, STOP):
        uc.mem_map(a, 0x10000)
    uc.mem_write(STUBS, b'\xc3' * 0x100)
    uc.mem_write(STOP, b'\xf4')
    # controller object
    uc.mem_write(CTRL, struct.pack('<Q', VT))
    uc.mem_write(VT + 0x960, struct.pack('<Q', MASK_STUB))
    uc.mem_write(CTRL + q_off, struct.pack('<Q', Q))
    uc.mem_write(CTRL + flags_off, bytes([msix_bit if msix else 0]))
    uc.mem_write(CTRL + 0x110, struct.pack('<Q', EVS))
    uc.mem_write(EVS, struct.pack('<Q', EVT))
    uc.mem_write(EVT + 0x238, struct.pack('<Q', SIGNAL_STUB))
    uc.mem_write(CTRL + 0x158, struct.pack('<Q', MSIX))
    uc.mem_write(CTRL + 0x160, struct.pack('<I', 2))
    # two completion queues at +0x40 and +0x10, entries in separate tables
    for i, (cq, (head, phase, statuses)) in enumerate(zip((0x40, 0x10), state)):
        table = ENT + i * 0x1000
        uc.mem_write(Q + cq + 0x08, struct.pack('<H', head))
        uc.mem_write(Q + cq + 0x10, struct.pack('<Q', table))
        uc.mem_write(Q + cq + 0x18, struct.pack('<I', phase))
        for n, s in enumerate(statuses):
            uc.mem_write(table + n * 16 + 0x0e, struct.pack('<H', s))
    calls = []
    def hook(uc, addr, size, _):
        if addr == MASK_STUB:
            calls.append(('intms', uc.reg_read(UC_X86_REG_RSI), uc.reg_read(UC_X86_REG_RDX)))
        elif addr == SIGNAL_STUB:
            calls.append(('signal',))
    uc.hook_add(UC_HOOK_CODE, hook, begin=STUBS, end=STUBS + 0x100)
    sp = STACK + 0x8000
    uc.mem_write(sp, struct.pack('<Q', STOP))
    uc.reg_write(UC_X86_REG_RSP, sp)
    uc.reg_write(UC_X86_REG_RDI, CTRL)
    uc.reg_write(UC_X86_REG_RSI, EVS)
    try:
        uc.emu_start(filt, STOP, count=10000)
    except Exception as e:
        print("fault at", hex(uc.reg_read(UC_X86_REG_RIP)), calls, e); raise
    msix_masked = all(struct.unpack('<I', bytes(uc.mem_read(MSIX + n * 16 + 0xc, 4)))[0] == 1 for n in range(2))
    # same memory image for our native copy
    buf = bytearray(0x100)
    tables = []
    for i, (cq, (head, phase, statuses)) in enumerate(zip((0x40, 0x10), state)):
        t = ctypes.create_string_buffer(0x1000)
        for n, s in enumerate(statuses):
            struct.pack_into('<H', t, n * 16 + 0x0e, s)
        tables.append(t)
        struct.pack_into('<H', buf, cq + 0x08, head)
        struct.pack_into('<Q', buf, cq + 0x10, ctypes.addressof(t))
        struct.pack_into('<I', buf, cq + 0x18, phase)
    cbuf = (ctypes.c_uint8 * len(buf)).from_buffer(buf)
    ours = mirror.pending(cbuf)
    return calls, msix_masked, ours

random.seed(1)
total = mismatch = 0
stats = {'pending': 0, 'idle': 0}
for msix in (False, True):
    for _ in range(int(sys.argv[8])):
        state = [(random.randrange(256), random.getrandbits(32), [random.getrandbits(16) for _ in range(256)]) for _ in range(2)]
        calls, msix_masked, ours = run(state, msix)
        apple_signal = ('signal',) in calls
        apple_masked = msix_masked if msix else ('intms', 0xc, 1) in calls
        total += 1
        stats['pending' if apple_signal else 'idle'] += 1
        # apple must mask exactly when it signals, and our check must agree with apple's decision
        if apple_signal != ours or apple_masked != apple_signal:
            mismatch += 1
            print('MISMATCH', msix, calls, msix_masked, ours)
print(f'cases {total}  apple-signal {stats["pending"]}  apple-idle {stats["idle"]}  mismatches {mismatch}')

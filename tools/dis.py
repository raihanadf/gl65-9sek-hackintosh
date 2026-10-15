# disassemble llvm-mc style hex bytes from stdin at a given address
import sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
code = bytes(int(x, 16) for x in sys.stdin.read().split())
md = Cs(CS_ARCH_X86, CS_MODE_64)
for i, ins in enumerate(md.disasm(code, int(sys.argv[1], 16))):
    print('%3d %x  %s %s' % (i, ins.address, ins.mnemonic, ins.op_str))

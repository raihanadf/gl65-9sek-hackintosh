# replay NVMeFix's fromFunc member searches (hde-style opcode/modrm matching) on sequoia
import sys, subprocess
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
kc, base, syms = sys.argv[1], sys.argv[2], dict(l.split()[::-1] for l in open(sys.argv[3]))
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
def fn(name, n=600):
    a = syms[name]
    hexb = subprocess.run(['python3', '-I', sys.argv[4], kc, base, a, str(n)], capture_output=True, text=True).stdout
    return bytes(int(x, 16) for x in hexb.split()), int(a, 16)
def from_func(name, opcode, reg, rm, ninsts=128):
    code, a = fn(name)
    for i, ins in enumerate(md.disasm(code, a)):
        if i == ninsts: break
        op = ins.opcode[0]
        if op == opcode and ins.modrm and ((ins.modrm >> 3) & 7) == reg and (ins.modrm & 7) == (rm & 7) and bool(ins.rex & 1) == bool(rm & 8):
            return hex(ins.disp), ins.mnemonic + ' ' + ins.op_str
    return None
print('result        ', from_func('__ZN16AppleNVMeRequest9GetStatusEv', 0x8b, 0, 7))
print('command       ', from_func('__ZN16AppleNVMeRequest9GetOpcodeEv', 0x0f, 0, 7))
print('prpDescriptor ', from_func('__ZN16IONVMeController20IssueIdentifyCommandEP18IOMemoryDescriptorjb', 0x89, 7, 14))
print('ANS2 (expect None)', from_func('__ZN16IONVMeController22FilterInterruptRequestEP28IOFilterInterruptEventSource', 0x80, 7, 7, 32))

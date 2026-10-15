# decode xnu's 7-bit packed aapl,panic-info from `nvram -x aapl,panic-info` output
import plistlib, re, sys

text = open(sys.argv[1]).read()
plist = re.search(r'<\?xml.*?</plist>', text, re.S)
data = plistlib.loads(plist.group(0).encode())['aapl,panic-info']
out, acc, bits = bytearray(), 0, 0
for byte in data:
    acc |= byte << bits
    bits += 8
    while bits >= 7:
        out.append(acc & 0x7f)
        acc >>= 7
        bits -= 7
sys.stdout.write(out.decode('ascii', 'replace'))

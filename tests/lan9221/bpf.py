#!/usr/bin/env python3
"""Run the actual BPF EtherType classification expression on boundary values."""
from pathlib import Path
import re
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root/'sys/sockets/inet4/bpf.c').read_text()
expression = re.search(r'if \(([^\n]+)\)\n\s*pktype = ETHPROTO_8023;', source).group(1)
program = '''#include <assert.h>
typedef unsigned short ushort;
static unsigned short classify(unsigned short input) {
 short pktype = (short)input;
 if (EXPR) pktype = 1;
 return (unsigned short)pktype;
}
int main(void) {
 assert(classify(0x0800)==0x0800); /* DHCP/IPv4 */
 assert(classify(0x0806)==0x0806); /* ARP */
 assert(classify(0x86dd)==0x86dd); /* signed-short boundary */
 assert(classify(1500)==1);
 assert(classify(1535)==1);
 assert(classify(1536)==1536);
 return 0;
}
'''.replace('EXPR',expression)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(program)
 subprocess.run(['cc','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: MiNTNet BPF preserves IPv4/ARP/high-bit EtherTypes; only lengths become 802.3')

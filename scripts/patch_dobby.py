import re
import sys

path = "Dobby/source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.asm"

with open(path, 'rb') as f:
    raw = f.read()

content = raw.decode('utf-8', errors='replace')
before = content

# Replace adrp TMP_REG_0, cdecl(sym)@PAGE with adr TMP_REG_0, cdecl(sym)
content = re.sub(
    r'adrp\s+TMP_REG_0,\s+cdecl\(common_closure_bridge_handler\)@PAGE\n',
    'adr TMP_REG_0, cdecl(common_closure_bridge_handler)\n',
    content
)
# Remove the add TMP_REG_0, TMP_REG_0, cdecl(sym)@PAGEOFF line
content = re.sub(
    r'add\s+TMP_REG_0,\s+TMP_REG_0,\s+cdecl\(common_closure_bridge_handler\)@PAGEOFF\n',
    '',
    content
)

if content == before:
    print("ERROR: No changes made - regex did not match!")
    for i, line in enumerate(content.splitlines()):
        if 'common_closure_bridge_handler' in line:
            print(f"Line {i+1}: {repr(line)}")
    sys.exit(1)

with open(path, 'w', newline='\n') as f:
    f.write(content)

print("Patch applied successfully")
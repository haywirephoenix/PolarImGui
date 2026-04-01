import re
import sys

path = "Dobby/source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.asm"

with open(path, 'rb') as f:
    raw = f.read()

print(f"File size: {len(raw)} bytes")
print(f"Contains CRLF: {'\\r\\n' in raw.decode('latin-1')}")

content = raw.decode('utf-8', errors='replace')

# Show the relevant lines
for i, line in enumerate(content.splitlines()):
    if 'common_closure_bridge_handler' in line:
        print(f"Line {i+1}: {repr(line)}")

before = content

content = re.sub(r'adrp\s+x17,\s+common_closure_bridge_handler@PAGE\r?\n',
                 'adr x17, common_closure_bridge_handler\n', content)
content = re.sub(r'add\s+x17,\s+x17,\s+common_closure_bridge_handler@PAGEOFF\r?\n',
                 '', content)

if content == before:
    print("ERROR: No changes made - regex did not match!")
    sys.exit(1)

with open(path, 'w', newline='\n') as f:
    f.write(content)

print("Patch applied successfully")
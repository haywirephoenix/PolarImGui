import re

path = "Dobby/source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.asm"
with open(path, 'rb') as f:
    content = f.read().decode('utf-8', errors='replace')

content = re.sub(r'adrp\s+x17,\s+common_closure_bridge_handler@PAGE\r?\n',
                 'adr x17, common_closure_bridge_handler\n', content)
content = re.sub(r'add\s+x17,\s+x17,\s+common_closure_bridge_handler@PAGEOFF\r?\n',
                 '', content)

with open(path, 'w', newline='\n') as f:
    f.write(content)

print("Patch applied successfully")
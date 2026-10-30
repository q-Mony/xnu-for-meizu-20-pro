#!/usr/bin/env python3
"""Install the gated Negroni personalities into the assembled XNU work source."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root / '.cache/xnu-work/iokit/KernelConfigTables.cpp'
text = source.read_text()
marker = '#if defined(XNU_NEGRONI_DISPLAY) && XNU_NEGRONI_DISPLAY'
if marker in text:
    raise SystemExit(0)
anchor = '#if defined(XNU_NATIVE_AES) && XNU_NATIVE_AES'
if text.count(anchor) != 1:
    raise SystemExit('KernelConfigTables.cpp lacks the expected personality anchor')
addition = '''#if defined(XNU_NEGRONI_DISPLAY) && XNU_NEGRONI_DISPLAY
    "   ,{"
    "     'IOClass'         = IOCoreSurfaceRoot;"
    "     'IOProviderClass' = IOResources;"
    "     'IOMatchCategory' = IOCoreSurfaceRoot;"
    "   }"
    "   ,{"
    "     'IOClass'         = AppleCLCD;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = 'negroni,display';"
    "   }"
#endif
'''
source.write_text(text.replace(anchor, addition + anchor))
print(f'installed gated Negroni personality: {source}')

#!/usr/bin/env python3
"""Check default-off wiring, the closed ABI and the XNU interest path."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'package'
XNU = Path('/Users/ym/Downloads/633c0edf-workspace/nova/unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work')
service = (PACKAGE / 'MeizuKeyStoreNotification.cpp').read_text()
bridge = (PACKAGE / 'meizu-aks-notify-xnu.patch').read_text()
config = (ROOT / 'configs/meizu20pro_defconfig').read_text()
board = (ROOT / 'board.toml').read_text()
interest = (XNU / 'iokit/Kernel/IOService.cpp').read_text()
user_client = (XNU / 'iokit/Kernel/IOUserClient.cpp').read_text()

assert 'CONFIG_XNU_MEIZU_AKS_NOTIFY=y' not in [line.strip() for line in config.splitlines()]
assert 'meizu-aks-notify=1' not in [line.strip() for line in board.splitlines()]
assert '"build-meizu-aks-notify.py"' in board
assert 'class AppleKeyStore' not in service and 'OSDefineMetaClassAndStructors(AppleKeyStore' not in service
assert service.count('newUserClient(') == 2
assert service.count('return kIOReturnUnsupported;') == 2
assert 'externalMethod(' not in service and 'IOUserClient' not in service.split('class MeizuKeyStoreNotification')[0]
assert 'service->registerService(kIOServiceSynchronous);' in service
assert 'service->release(); // registry attachment' not in service
assert 'meizu_keystore_notification_status = 5;' in service
assert 'if (real)' in service and 'OSMetaClass::getMetaClassWithName(name)' in service
assert 'meizuAksVolatileData(' in service
assert 'meizuAksExactRequest(' in bridge and 'if (!*service)' in bridge
assert 'IOTaskRegistryCompatibilityMatching(current_task(), dict);' in bridge
assert '#if defined(XNU_MEIZU_AKS_NOTIFY) && XNU_MEIZU_AKS_NOTIFY' in bridge
assert 'gIOGeneralInterest' in interest
for marker in ('typeOfInterest != gIOGeneralInterest', 'setProperty( typeOfInterest, notifyList)',
               'enqueue(&notifyList->fCommandChain, &notify->chain)',
               'applyToInterestNotifiers(this, typeOfInterest, applier, context)'):
    assert marker in interest, marker
for marker in ('IOServiceMessageUserNotification', 'notify = service->registerInterest( sym,',
               'err = kIOReturnSuccess;'):
    assert marker in user_client, marker
with tempfile.TemporaryDirectory(prefix='meizu-aks-host-') as d:
    binary = Path(d) / 'policy-test'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    str(PACKAGE / 'tests/MeizuAksNotificationPolicyTests.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: default off, distinct class, both opens denied, registration lifetime and general-interest path')

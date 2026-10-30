# R17 corrected host suite

Exact single command: `python3 run-host-suite-nova.py`

* Primary REAL pre-fix RED (life-normal, loc4, sid0, logmem0, hide0, twice): 4/2 reproduced exit 86 / assert log_encode.c:120.
* Full matrix: 56 pre cases, 56 post cases; post GREEN PASS.
* Console comparisons: 50 identical, 0 mismatched, 6 pre-RED skipped.
* Worst case: life-max line 1079 <= 2048; sbfault base 444 <= 1024; sbfault regs 716 <= 1024.
* Leak checks: {"life-max": {"exit": 0, "leaks": 0, "log": "<nova-root>/artifacts-nova/boot-speed-round17-nova/log-encode-fix-nova/host/r17-red-green/logs/leaks-life-max.log"}, "sbfault-max": {"exit": 0, "leaks": 0, "log": "<nova-root>/artifacts-nova/boot-speed-round17-nova/log-encode-fix-nova/host/r17-red-green/logs/leaks-sbfault-max.log"}}

Failures: none

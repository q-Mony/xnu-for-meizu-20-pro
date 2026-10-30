#!/usr/bin/env python3
"""Execute the production Session, thread/stage dispatch and poll loop on the host.

Only hardware/IOKit/thread APIs are replaced. Extraction keeps the original
allocation and stage guards under test, without compiling ARM assembly on x86.
Pass --source to prove the suite rejects an older or mutated probe.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT / 'package/MeizuTouchProbe.cpp')
args = parser.parse_args()
source = args.source.read_text()
region = source[source.index('struct Region {'):source.index('\nbool mapRegion')]
session = source[source.index('struct Session {'):source.index('// TLMM page')]
loop = source[source.index('void eventLoop('):source.index('\nint stageFromBootArgs()')]
# The bounded parser precedes stageFromBootArgs and is included with the loop.
thread = source[source.index('int stageFromBootArgs()'):source.index('}  // namespace', source.index('int stageFromBootArgs()'))]
probe = source[source.index('extern "C" void native_meizu_touch_probe(void)'):]
preamble = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
extern "C" {
#include "GeniI2c.h"
#include "GoodixBerlin.h"
#include "MeizuTouchBoard.h"
}
static unsigned constructed;
struct IOMemoryMap {};
struct IOBufferMemoryDescriptor {};
template<class T> struct OSSharedPtr {
    T *pointer;
    OSSharedPtr() : pointer(nullptr) { ++constructed; }
};
static std::map<std::string, std::string> args;
static std::vector<int> calls;
static unsigned allocations, reads, slept, failures;
static int failAt;
static bool allocationFails;
using wait_result_t = int;
using thread_t = void *;
using event_t = void *;
constexpr int THREAD_UNINT = 0, THREAD_AWAKENED = 0, THREAD_CONTINUE_NULL = 0, KERN_SUCCESS = 0;
uint32_t native_meizu_touch_stats[16];
int native_touch_error, native_touch_last_x, native_touch_last_y;
uint32_t native_touch_count, native_touch_frames, native_touch_valid, gIrqEvents;
void *current_thread() { return nullptr; }
void thread_set_thread_name(void *, const char *) {}
void thread_deallocate(void *) {}
void assert_wait(event_t, int) {}
void clear_wait(void *, int) {}
void thread_block(int) { assert(false); }
int kernel_thread_start(void (*entry)(void *, int), void *context, thread_t *t) {
    *t = nullptr;
    calls.push_back(0);
    entry(context, 0);
    return KERN_SUCCESS;
}
void kprintf(const char *, ...) {}
void IOLog(const char *, ...) {}
void fail(unsigned, int code) { ++failures; native_touch_error = code; }
void *IOMalloc(size_t size) {
    ++allocations;
    if (allocationFails) return nullptr;
    void *p = std::malloc(size);
    assert(p);
    std::memset(p, 0xa5, size); // Do not let allocator zeroing hide missing initialization.
    return p;
}
bool PE_parse_boot_arg_str(const char *key, char *dst, int length) {
    auto it = args.find(key);
    if (it == args.end()) return false;
    std::strncpy(dst, it->second.c_str(), length - 1);
    dst[length - 1] = 0;
    return true;
}
bool PE_parse_boot_argn(const char *key, void *dst, int length) {
    auto it = args.find(key);
    if (it == args.end()) return false;
    assert(length == sizeof(unsigned));
    char *end;
    auto value = std::strtoull(it->second.c_str(), &end, 0);
    if (*end) {
        std::strncpy(static_cast<char *>(dst), it->second.c_str(), length - 1);
        static_cast<char *>(dst)[length - 1] = 0;
    } else *static_cast<unsigned *>(dst) = static_cast<unsigned>(value);
    return true;
}
struct SleepReached {};
void IOSleep(unsigned ms) { slept = ms; throw SleepReached{}; }
int gb_read_reg(gb_dev *, uint32_t, uint8_t *dst, size_t size) {
    ++reads;
    std::memset(dst, 0, size); // Idle controller must still yield for pollMs.
    return 0;
}
int gb_poll_event(gb_dev *, gb_event *) { assert(false); return 0; }
'''
stubs = r'''
void checkDefaults(const Session &s) {
    assert(constructed == 9);
    assert(s.panelX == MTB_PANEL_MAX_X && s.panelY == MTB_PANEL_MAX_Y);
    assert(!s.dmaBuf && !s.level);
    assert(!s.vote && !s.haltCore && !s.haltCore2x && !s.haltMAhb && !s.haltSAhb && !s.haltS4 && !s.gpll0Mode);
    assert(!s.events && !s.checksumErrors && !s.busErrors && !s.emptyPolls);
    for (auto value : s.levelHz) assert(!value);
    for (auto value : s.pinCtl) assert(!value);
    for (auto value : s.cmdAddrReg) assert(!value);
    for (const Region *r : {&s.gccLow, &s.gccS4, &s.gccHalt, &s.gccVote, &s.tlmm,
                            &s.wrapper, &s.se, &s.rsc, &s.cmdb}) assert(!r->va && !r->keep.pointer);
    assert(!s.bus.se.ctx && !s.bus.se.rd && !s.bus.se.wr && !s.bus.se.delay_us);
    assert(!s.bus.soft_ctx && !s.bus.soft_read && !s.bus.soft_write && !s.bus.poll_limit);
    assert(!s.chip.bus && !s.chip.addr && !s.chip.info.touch_data_addr);
}
bool step(int n) { calls.push_back(n); return failAt != n; }
bool snapshot(Session &s) { checkDefaults(s); return step(1); }
bool enable(Session &) { return step(2); }
bool openBus(Session &) { return step(3); }
bool identify(Session &) { return step(4); }
bool armIrq(Session &) { calls.push_back(5); return false; }
void publish(const Session &, const gb_event &) { assert(false); }
'''
tests = r'''
void reset() {
    if (gSession) { gSession->~Session(); std::free(gSession); gSession = nullptr; }
    args.clear(); calls.clear();
    constructed = allocations = reads = slept = failures = 0;
    failAt = 0; allocationFails = false;
    native_touch_error = 0;
    std::memset(native_meizu_touch_stats, 0, sizeof native_meizu_touch_stats);
}
void run() { try { native_meizu_touch_probe(); } catch (SleepReached &) {} }
void expectCalls(std::initializer_list<int> expected) { assert(calls == std::vector<int>(expected)); }
void pollCase(const char *poll, unsigned expected) {
    reset(); args["meizu-touch"] = "4";
    if (poll) args["meizu-touch-poll"] = poll;
    run();
    expectCalls({0, 1, 2, 3, 4});
    assert(gSession && gSession->pollMs == expected && slept == expected && reads == 1);
    assert(!gSession->flipX && !gSession->flipY);
}
int main() {
    pollCase(nullptr, 4);
    for (auto input : {"", "0", "-1", "101", "4294967300", "18446744073709551620",
                       "abc", "A", "4junk", "1.5", "0x", "0b", "0x100000004", "1k", "+4",
                       "0000000000000000000000000000000000000000000000000000000000004"}) pollCase(input, 4);
    for (unsigned value = 1; value <= 100; ++value) pollCase(std::to_string(value).c_str(), value);
    pollCase("0x10", 16); pollCase("0b10", 2); pollCase("010", 8);
    reset(); run(); assert(calls.empty() && !allocations);
    for (auto input : {"", "0", "-1", "5", "99", "bogus", "4junk", "4294967300", "0x100000004"}) {
        reset(); args["meizu-touch"] = input; run(); assert(calls.empty() && !allocations);
    }
    for (int stage = 1; stage <= 4; ++stage) {
        reset(); args["meizu-touch"] = std::to_string(stage);
        args["meizu-touch-irq"] = "1"; run();
        if (stage == 1) expectCalls({0, 1});
        if (stage == 2) expectCalls({0, 1, 2});
        if (stage == 3) expectCalls({0, 1, 2, 3, 4});
        if (stage == 4) expectCalls({0, 1, 2, 3, 4, 5});
        assert(slept == (stage == 4 ? 4u : 0u)); // Failed IRQ attach falls back to bounded polling.
    }
    for (int failed = 1; failed <= 4; ++failed) {
        reset(); args["meizu-touch"] = "4"; args["meizu-touch-irq"] = "1"; failAt = failed; run();
        assert(calls.size() == static_cast<size_t>(failed + 1) && calls.back() == failed);
        assert(!reads && !slept);
    }
    reset(); args["meizu-touch"] = "4"; allocationFails = true; run();
    expectCalls({0}); assert(failures == 1 && !gSession && !constructed);
    reset(); args["meizu-touch"] = "4"; native_meizu_touch_stats[0] = 1; run();
    assert(calls.empty() && !allocations);
    reset(); args["meizu-touch"] = "4"; args["meizu-touch-flip-x"] = "1"; args["meizu-touch-flip-y"] = "2"; run();
    assert(gSession->flipX == 1 && gSession->flipY == 1);
    reset();
    std::puts("PASS: Session construction/defaults, bounded poll + idle sleep, stage/IRQ/failure guards");
}
'''
scratch = Path('/Users/ym/Downloads/633c0edf-workspace/nova/scratch-nova')
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='meizu-touch-host-', dir=scratch) as directory:
    cpp = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    cpp.write_text(preamble + region + session + stubs + loop + thread + probe + tests)
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I', str(ROOT / 'package'), '-I', str(ROOT / 'drivers/NativeGeniI2c'),
                    '-I', str(ROOT / 'drivers/NativeGoodixBerlin'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

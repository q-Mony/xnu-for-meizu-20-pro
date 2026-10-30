// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include "../../../kits/VerifiedRoot/include/verified_root.h"
#include "../RootTransportLayout.h"
using IOReturn = uint32_t;
constexpr IOReturn kIOReturnSuccess=0, kIOReturnBadArgument=1, kIOReturnIOError=2, kIOReturnNotPrivileged=3;
#include "../RootWindowRead.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
struct Shared {
  std::mutex window, control;
  std::condition_variable changed;
  unsigned attempts=0;
  bool concurrent=false;
  uint8_t bounce[kDataMax];
};
struct Ops {
  struct Read { uint64_t lba; uint32_t blocks; };
  Shared &shared;
  const uint64_t absolute, bytes;
  std::vector<uint8_t> disk, output;
  std::vector<Read> commands;
  bool held=false, parked=false;
  unsigned reads=0, copies=0, checks=0, badRead=0, badVerify=0, shortCopyAt=1;
  uint64_t shortCopy=UINT64_MAX;
  Ops(Shared &s, uint64_t at=0, uint64_t length=3*kDataMax+4608)
    : shared(s), absolute(at), bytes(length), disk((length+4095)&~uint64_t(4095),0xdd), output(length,0xa5) {
    for (size_t i=0;i<bytes;++i) disk[i]=uint8_t(i*17+i/4096);
  }
  void lock() {
    { std::lock_guard<std::mutex> g(shared.control); ++shared.attempts; shared.changed.notify_all(); }
    shared.window.lock(); held=true;
  }
  void unlock() { CHECK(held); held=false; shared.window.unlock(); }
  IOReturn read(uint64_t lba,uint32_t blocks,void *out) {
    CHECK(held && blocks && blocks<=kDataMax/4096 && lba<=UINT32_MAX);
    CHECK(blocks<=uint64_t(UINT32_MAX)+1-lba);
    ++reads; commands.push_back({lba,blocks});
    if(reads==badRead) return kIOReturnIOError;
    CHECK(lba*4096>=absolute);
    uint64_t offset=lba*4096-absolute;
    CHECK(offset<=disk.size() && blocks*4096<=disk.size()-offset);
    memcpy(out,disk.data()+offset,blocks*4096); return kIOReturnSuccess;
  }
  IOReturn verify(uint64_t index,const void *data,size_t length) {
    CHECK(held); ++checks;
    if(shared.concurrent && !parked) {
      parked=true;
      std::unique_lock<std::mutex> guard(shared.control);
      shared.changed.wait(guard,[&]{return shared.attempts>=2;});
    }
    if(checks==badVerify) return kIOReturnNotPrivileged;
    uint64_t offset=index*VR_BLOCK_BYTES;
    CHECK(offset<bytes && length==std::min(uint64_t(VR_BLOCK_BYTES),bytes-offset));
    CHECK(!memcmp(data,disk.data()+offset,length)); return kIOReturnSuccess;
  }
  uint64_t copy(uint64_t offset,const void *data,uint64_t length) {
    CHECK(held); ++copies;
    uint64_t n=copies==shortCopyAt ? std::min(length,shortCopy) : length;
    CHECK(offset<=output.size() && n<=output.size()-offset);
    memcpy(output.data()+offset,data,n); return n;
  }
  bool untouched(uint64_t start=0) const {
    return std::all_of(output.begin()+start,output.end(),[](uint8_t b){return b==0xa5;});
  }
};
static void batchCount() {
  Shared s; Ops o(s);
  auto r=ufsReadWindow(o,o.absolute,o.bytes,0,kDataMax/512,s.bounce);
  std::printf("batch-count: status=%u done=%llu read_commands=%u verify_units=%u copy_calls=%u\n",
              r.status,(unsigned long long)r.done,o.reads,o.checks,o.copies);
  CHECK(r.status==0 && r.done==kDataMax && o.reads==1 && o.checks==kDataMax/VR_BLOCK_BYTES && o.copies==1);
  CHECK(!o.held && !memcmp(o.output.data(),o.disk.data(),r.done));
}
static void batchAtomic() {
  for(unsigned failed=1;failed<=kDataMax/VR_BLOCK_BYTES;++failed) {
    Shared s; Ops o(s);o.badVerify=failed;
    auto r=ufsReadWindow(o,o.absolute,o.bytes,0,kDataMax/512,s.bounce);
    std::printf("batch-atomic: failed_unit=%u status=%u done=%llu copies=%u\n",failed,r.status,(unsigned long long)r.done,o.copies);
    CHECK(r.status==kIOReturnNotPrivileged && !r.done && !o.copies && o.untouched() && !o.held);
  }
}
int main(int argc,char **argv) {
  if(argc==2 && !strcmp(argv[1],"--batch-count")) { batchCount(); return 0; }
  if(argc==2 && !strcmp(argv[1],"--batch-atomic")) { batchAtomic(); return 0; }
  CHECK(argc==1);
  batchCount(); batchAtomic();
  Shared s;
  CHECK(!ufsWindowGeometry(UINT64_MAX-4095,4096));
  CHECK(!ufsWindowGeometry(0,UINT64_MAX-511));
  CHECK(ufsWindowGeometry(uint64_t(UINT32_MAX)*4096,4096));
  CHECK(!ufsWindowGeometry(uint64_t(UINT32_MAX)*4096,4608));
  // Every 512-byte offset in a verification unit, including windows whose
  // physical base is only 4 KiB aligned. Each request needs exactly two reads.
  for(uint64_t base : {uint64_t(0),uint64_t(4096),uint64_t(3*4096)}) {
    for(unsigned offset=0;offset<VR_BLOCK_BYTES/512;++offset) {
      Ops o(s,base); auto r=ufsReadWindow(o,base,o.bytes,offset,257,s.bounce);
      CHECK(r.status==0 && r.done==257*512 && o.reads==2 && o.checks==9 && o.copies==2 && !o.held);
      CHECK(!memcmp(o.output.data(),o.disk.data()+offset*512,r.done));
      CHECK(o.commands[0].lba==base/4096 && o.commands[0].blocks==32);
      CHECK(o.commands[1].lba==(base+kDataMax)/4096 && o.commands[1].blocks==4);
    }
  }
  // Small requests cover only their required verification units, not a full batch.
  for(unsigned offset=0;offset<2*VR_BLOCK_BYTES/512;++offset) {
    Ops o(s,4096);auto r=ufsReadWindow(o,o.absolute,o.bytes,offset,1,s.bounce);
    CHECK(r.status==0 && r.done==512 && o.reads==1 && o.checks==1 && o.commands[0].blocks==4);
    CHECK(o.commands[0].lba==(o.absolute+(offset*512/VR_BLOCK_BYTES)*VR_BLOCK_BYTES)/4096);
    CHECK(!memcmp(o.output.data(),o.disk.data()+offset*512,512));
  }
  { Ops o(s,4096);auto r=ufsReadWindow(o,o.absolute,o.bytes,31,2,s.bounce);
    CHECK(r.status==0 && r.done==1024 && o.reads==1 && o.checks==2 && o.commands[0].blocks==8); }
  for(uint64_t tail : {uint64_t(512),uint64_t(4608)}) {
    Ops o(s,4096,kDataMax+tail);auto r=ufsReadWindow(o,o.absolute,o.bytes,0,o.bytes/512,s.bounce);
    CHECK(r.status==0 && r.done==o.bytes && o.reads==2 && o.checks==9);
    CHECK(o.commands[1].blocks==(tail+4095)/4096 && !memcmp(o.output.data(),o.disk.data(),o.bytes));
    Ops only(s,4096,kDataMax+tail);r=ufsReadWindow(only,only.absolute,only.bytes,kDataMax/512,tail/512,s.bounce);
    CHECK(r.status==0 && r.done==tail && only.reads==1 && only.checks==1);
  }
  { Ops o(s,uint64_t(UINT32_MAX)*4096,4096);auto r=ufsReadWindow(o,o.absolute,o.bytes,0,8,s.bounce);
    CHECK(r.status==0 && r.done==4096 && o.commands[0].lba==UINT32_MAX && o.commands[0].blocks==1); }
  for(unsigned failed=9;failed<=16;++failed) {
    Ops o(s);o.badVerify=failed;auto r=ufsReadWindow(o,0,o.bytes,0,512,s.bounce);
    CHECK(r.status==3 && r.done==kDataMax && o.copies==1 && o.untouched(kDataMax) && !o.held);
    CHECK(!memcmp(o.output.data(),o.disk.data(),kDataMax));
  }
  { Ops o(s);o.badRead=2;auto r=ufsReadWindow(o,0,o.bytes,0,257,s.bounce);
    CHECK(r.status==2 && r.done==kDataMax && o.copies==1 && o.untouched(kDataMax) && !o.held); }
  for(unsigned at : {1u,2u}) {
    Ops o(s);o.shortCopy=17;o.shortCopyAt=at;auto r=ufsReadWindow(o,0,o.bytes,0,257,s.bounce);
    // Preserve the existing production completion contract: a short copy
    // reports only fully completed previous batches, not its partial bytes.
    uint64_t prior=at==1?0:kDataMax;
    CHECK(r.status==2 && r.done==prior && !o.held && o.untouched(prior+17));
    CHECK(!memcmp(o.output.data(),o.disk.data(),prior+17));
  }
  { Ops o(s);for(uint64_t count : {uint64_t(0),UINT64_MAX}) {
      auto r=ufsReadWindow(o,0,o.bytes,0,count,s.bounce);CHECK(r.status==1 && !o.reads); }
    for(uint64_t block : {o.bytes/512,UINT64_MAX}) {
      auto r=ufsReadWindow(o,0,o.bytes,block,1,s.bounce);CHECK(r.status==1 && !o.reads); }
    CHECK(ufsReadWindow(o,0,o.bytes,0,1,nullptr).status==1);
    CHECK(ufsReadWindow(o,512,o.bytes,0,1,s.bounce).status==1);
    CHECK(ufsReadWindow(o,0,o.bytes-1,0,1,s.bounce).status==1); }
  s.attempts=0;s.concurrent=true;
  Ops first(s),second(s);UfsWindowResult a{},b{};
  std::thread one([&]{a=ufsReadWindow(first,0,first.bytes,0,256,s.bounce);});
  { std::unique_lock<std::mutex> guard(s.control);s.changed.wait(guard,[&]{return s.attempts==1;}); }
  std::thread two([&]{b=ufsReadWindow(second,0,second.bytes,32,256,s.bounce);});
  one.join();two.join();
  CHECK(a.status==0 && b.status==0 && a.done==kDataMax && b.done==kDataMax);
  CHECK(!memcmp(first.output.data(),first.disk.data(),kDataMax));
  CHECK(!memcmp(second.output.data(),second.disk.data()+VR_BLOCK_BYTES,kDataMax));
  CHECK(s.window.try_lock());s.window.unlock();
  puts("PASS: 16KiB verification, 128KiB batches, 32 offsets, non-16KiB physical bases, small reads, tail/padding, final LBA, batch-atomic failure, short copy contract, concurrent bounce and unlocked completion");
}

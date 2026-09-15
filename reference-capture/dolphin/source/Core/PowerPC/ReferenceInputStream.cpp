// Copyright 2026 Melee Web contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "Core/PowerPC/ReferenceInputStream.h"
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Common/DirectIOFile.h"
#include "Core/CoreTiming.h"
#include "Core/PowerPC/ReferenceCaptureObserver.h"
#include "Core/System.h"
#include "InputCommon/GCPadStatus.h"

namespace ReferenceCapture
{
namespace
{
using Bytes = std::array<u8, 40>;
constexpr std::array<u8, 16> HEADER = {'M','W','R','I', 1,0, 16,0, 40,0,0,0, 0,0,0,0};
constexpr size_t MAX_BYTES = 64 * 1024 * 1024;
constexpr size_t CAPACITY = 4096;
u64 Get(const u8* p, size_t n) { u64 v = 0; for (size_t i=0;i<n;++i) v |= u64(p[i]) << (8*i); return v; }
void Put(u8* p, u64 v, size_t n) { for(size_t i=0;i<n;++i) p[i]=u8(v>>(8*i)); }
u32 CRC(const u8* p, size_t n)
{
  u32 c=~0U; for(size_t i=0;i<n;++i) { c ^= p[i]; for(int j=0;j<8;++j) c=(c>>1)^(0xedb88320U & (0U-(c&1U))); } return ~c;
}
std::string Env(const char* key) { const char* p=std::getenv(key); return p ? p : ""; }
Bytes Pack(u64 seq,u64 tick,u32 port,const GCPadStatus& p,u32 kind=1)
{
  Bytes b{}; Put(b.data(),seq,8); Put(b.data()+8,tick,8); Put(b.data()+16,port,4);
  Put(b.data()+20,p.button,2);
  const std::array<u8,10> fields={p.stickX,p.stickY,p.substickX,p.substickY,p.triggerLeft,p.triggerRight,p.analogA,p.analogB,p.switches,u8(p.isConnected)};
  std::copy(fields.begin(),fields.end(),b.begin()+22); Put(b.data()+32,kind,4); Put(b.data()+36,CRC(b.data(),36),4); return b;
}
GCPadStatus Unpack(const Bytes& b)
{
  GCPadStatus p{}; p.button=u16(Get(b.data()+20,2)); p.stickX=b[22];p.stickY=b[23];p.substickX=b[24];p.substickY=b[25];p.triggerLeft=b[26];p.triggerRight=b[27];p.analogA=b[28];p.analogB=b[29];p.switches=b[30];p.isConnected=b[31]!=0;return p;
}
struct Slot { Bytes bytes{}; std::atomic<bool> ready{false}; };
struct Stream
{
  int mode=0; bool initialized=false;
  std::string path,status_path;
  std::atomic<bool> invalid{false},finish{false},natural{false};
  std::atomic<u64> head{0},tail{0};
  std::atomic<u64> count{0},last_tick{0};
  std::array<Slot,CAPACITY> ring{};
  std::vector<Bytes> samples;
  std::thread writer;
  std::mutex error_mutex;
  std::string error;
  ~Stream() { finish.store(true); if(writer.joinable()) writer.join(); }
  bool Fail(const char* why)
  {
    bool expected=false;
    if(invalid.compare_exchange_strong(expected,true)) { std::lock_guard lock(error_mutex); error=why; }
    finish.store(true); Observer::Fail(why); return false;
  }
  bool Status(bool complete,u64 events)
  {
    if(status_path.empty()) return false;
    std::string reason; {std::lock_guard lock(error_mutex); reason=error;}
    // Error messages are fixed developer strings without quotes or user paths.
    std::string json="{\"version\":1,\"mode\":\""+std::string(mode==2?"replay":"record")+"\",\"events\":"+std::to_string(events)+",\"complete\":"+(complete?"true":"false")+",\"invalid\":"+(invalid.load()?"true":"false")+",\"error\":"+(reason.empty()?"null":"\""+reason+"\"")+"}\n";
    const auto temporary=status_path+".tmp";
    std::error_code ec;std::filesystem::remove(temporary,ec);
    File::DirectIOFile f(temporary,File::AccessMode::Write,File::OpenMode::Create);
    if(!f.IsOpen() || !f.Write(reinterpret_cast<const u8*>(json.data()),json.size()) || !f.Flush() || !f.Close())return false;
    std::filesystem::rename(temporary,status_path,ec);return !ec;
  }
  bool Init()
  {
    if(initialized)return !invalid.load(); initialized=true;
    const auto record=Env("MWRC_INPUT_RECORD"),replay=Env("MWRC_INPUT_REPLAY");
    if(record.empty() && replay.empty())return true;
    mode=replay.empty()?1:2;path=mode==1?record:replay;status_path=Env("MWRC_INPUT_STATUS");
    if((!record.empty() && !replay.empty()) || status_path.empty())return Fail("input recording activation is invalid");
    if(mode==2)
    {
      std::error_code ec;auto size=std::filesystem::file_size(path,ec);
      if(ec || size<56 || size>MAX_BYTES || (size-16)%40)return Fail("input stream size is invalid");
      std::ifstream file(path,std::ios::binary);std::array<u8,16> h{};
      if(!file.read(reinterpret_cast<char*>(h.data()),h.size()) || h!=HEADER)return Fail("input stream header is invalid");
      u64 previous=0;const size_t records=(size-16)/40;
      for(size_t i=0;i<records;++i)
      {
        Bytes b{};if(!file.read(reinterpret_cast<char*>(b.data()),b.size()))return Fail("input stream is truncated");
        const auto seq=Get(b.data(),8),tick=Get(b.data()+8,8),port=Get(b.data()+16,4),kind=Get(b.data()+32,4);
        if(seq!=i || tick<previous || Get(b.data()+36,4)!=CRC(b.data(),36))return Fail("input sequence or checksum is invalid");
        previous=tick;
        if(i==records-1) { if(kind!=2 || port!=0xffffffffU)return Fail("input stream lacks completed footer"); }
        else { if(kind!=1 || !((port<4)||(port>=256 && port<260)) || b[31]>1)return Fail("input sample is invalid"); samples.push_back(b); }
      }
      if(samples.empty())return Fail("input stream has no controller samples");
      return Status(false,0) || Fail("input status write failed");
    }
    writer=std::thread([this]{Write();});return true;
  }
  void Record(u64 tick,u32 port,const GCPadStatus& p)
  {
    if(finish.load() || invalid.load())return;
    if(16+(count+2)*40>MAX_BYTES) { Fail("input stream exceeds recording bound");return; }
    auto& slot=ring[count%CAPACITY];if(slot.ready.load(std::memory_order_acquire)){Fail("input ring overflow");return;}
    slot.bytes=Pack(count,tick,port,p);slot.ready.store(true,std::memory_order_release);last_tick=tick;++count;head.store(count,std::memory_order_release);
  }
  GCPadStatus Replay(u64 tick,u32 port)
  {
    GCPadStatus disconnected{};disconnected.isConnected=false;
    if(finish.load() || invalid.load())return disconnected;
    if(count>=samples.size()){Fail("input stream exhausted before original teardown");return disconnected;}
    const auto& b=samples[count];
    if(Get(b.data()+8,8)!=tick || Get(b.data()+16,4)!=port){Fail("input poll port or emulated tick diverged");return disconnected;}
    ++count;last_tick=tick;return Unpack(b);
  }
  void Write()
  {
    File::DirectIOFile f(path,File::AccessMode::Write,File::OpenMode::Create);
    if(!f.IsOpen() || !f.Write(HEADER.data(),HEADER.size()))Fail("input stream creation failed");
    if(!Status(false,0))Fail("input status write failed");
    u64 n=0;auto next=std::chrono::steady_clock::now();
    for(;;)
    {
      while(n<head.load(std::memory_order_acquire))
      {
        auto& slot=ring[n%CAPACITY];if(!slot.ready.load(std::memory_order_acquire))break;
        if(f.IsOpen() && !f.Write(slot.bytes.data(),slot.bytes.size())){Fail("input stream writer failed");f.Close();}
        slot.ready.store(false,std::memory_order_release);++n;tail.store(n);
      }
      if(finish.load() && n>=head.load())break;
      if(std::chrono::steady_clock::now()>=next){if(!Status(false,n))Fail("input status write failed");next=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);}
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    GCPadStatus empty{};empty.isConnected=false;
    const auto end=Pack(n,last_tick,0xffffffffU,empty,natural.load()&&!invalid.load()?2:3);
    if(!f.IsOpen() || !f.Write(end.data(),end.size()) || !f.Flush() || !f.Close())Fail("input stream finalization failed");
    if(!Status(natural.load()&&!invalid.load(),n))Fail("input final status write failed");
  }
};
// One isolated Dolphin process owns one session; avoid cross-singleton destruction order.
Stream& State(){static Stream* value=new Stream;return *value;}
}
bool InputStream::Initialize(){return State().Init();}
bool InputStream::IsReplaying(){return Observer::IsEnabled() && State().mode==2;}
bool InputStream::IsRecording(){return Observer::IsEnabled() && State().mode==1;}
void InputStream::Record(Core::System& s,u32 p,const GCPadStatus& pad){if(IsRecording())State().Record(s.GetCoreTiming().GetTicks(),p,pad);}
GCPadStatus InputStream::Replay(Core::System& s,u32 p){return State().Replay(s.GetCoreTiming().GetTicks(),p);}
bool InputStream::AdapterConnected(Core::System& s,u32 p,bool connected)
{
  if(IsReplaying())return Replay(s,p+256).isConnected;
  GCPadStatus pad{};pad.isConnected=connected;if(IsRecording())Record(s,p+256,pad);return connected;
}
void InputStream::RequestFinish(bool natural)
{
  auto& s=State();if(!s.mode)return;
  if(natural && s.mode==2 && s.count!=s.samples.size())s.Fail("original teardown preceded input stream completion");
  s.natural.store(natural);s.finish.store(true);
}
bool InputStream::WaitComplete()
{
  auto& s=State();if(!s.mode)return true;
  if(!s.finish.load())RequestFinish(false);
  if(s.writer.joinable())s.writer.join();
  if(s.mode==2 && !s.Status(s.natural.load()&&!s.invalid.load(),s.count))s.Fail("input final status write failed");
  return !s.invalid.load();
}
}

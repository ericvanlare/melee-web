#include "dat_audio_stream.hpp"
#include <algorithm>
#include <cstring>
#include <map>
namespace melee_web {
namespace {
void require(bool ok,const char* why){if(!ok)throw DatError(why);}
struct Reader {
    std::span<const uint8_t> bytes;
    void range(size_t o,size_t n)const{require(o<=bytes.size()&&n<=bytes.size()-o,"HPS byte range exceeds file");}
    uint16_t half(size_t o)const{range(o,2);return uint16_t(bytes[o])<<8|bytes[o+1];}
    uint32_t word(size_t o)const{return uint32_t(half(o))<<16|half(o+2);}
};
}
DatAudioStream::DatAudioStream(std::span<const uint8_t> bytes){
    Reader r{bytes};r.range(0,128);
    require(bytes.size()<=64U*1024U*1024U,"HPS input exceeds stream budget");
    require(std::memcmp(bytes.data()," HALPST\0",8)==0,"Invalid HPS magic");
    sample_rate=r.word(8);const auto count=r.word(12);
    require(sample_rate>0&&sample_rate<=192000&&count>=1&&count<=2,"Invalid HPS rate or channel count");
    for(uint32_t i=0;i<count;i++){
        const size_t at=16+i*56;AudioChannel c;
        require(r.half(at)<=1&&r.half(at+2)==0,"Unsupported HPS channel format");
        c.looping=r.half(at);c.loop_nibble=r.word(at+4);c.end_nibble=r.word(at+8);c.current_nibble=r.word(at+12);
        require(c.current_nibble==2&&c.loop_nibble==2,"Unsupported HPS initial channel address");
        for(unsigned k=0;k<16;k++)c.coefficients[k]=int16_t(r.half(at+16+k*2));
        require(r.half(at+48)==0,"Unsupported HPS DSP gain");
        c.predictor_scale=r.half(at+50);c.history1=int16_t(r.half(at+52));c.history2=int16_t(r.half(at+54));
        require((c.predictor_scale&0xff80)==0,"Invalid HPS header predictor");
        channel_headers.push_back(std::move(c));
    }
    uint32_t cursor=128;size_t budget=0;
    std::map<uint32_t,size_t> visited;
    while(cursor!=UINT32_MAX){
        if(auto found=visited.find(cursor);found!=visited.end()){loop_block=found->second;break;}
        require(cursor>=128&&cursor%32==0&&blocks.size()<16384,"Invalid HPS block chain address or length");
        r.range(cursor,32);AudioStreamBlock b;
        b.file_offset=cursor;b.payload_size=r.word(cursor);b.end_nibble=r.word(cursor+4);b.next_offset=r.word(cursor+8);
        require(b.payload_size>0&&b.payload_size<=65536&&b.payload_size%32==0&&b.payload_size%count==0,"Invalid HPS transfer size");
        r.range(size_t(cursor)+32,b.payload_size);
        const uint32_t channel_bytes=b.payload_size/count;
        require(b.end_nibble>=2&&b.end_nibble<uint64_t(channel_bytes)*2&&b.end_nibble%16>=2,"HPS channel end exceeds block payload");
        const uint64_t end=uint64_t(cursor)+32+b.payload_size;
        for(const auto& prior:blocks)require(end<=prior.file_offset||cursor>=uint64_t(prior.file_offset)+32+prior.payload_size,"Overlapping HPS block extents");
        for(uint32_t i=0;i<count;i++){
            auto c=channel_headers[i];const size_t history=cursor+12+i*8;
            c.predictor_scale=r.half(history);c.history1=int16_t(r.half(history+2));c.history2=int16_t(r.half(history+4));
            if(blocks.empty())require(c.predictor_scale==channel_headers[i].predictor_scale&&c.history1==channel_headers[i].history1&&c.history2==channel_headers[i].history2,"HPS initial header and first block histories disagree");
            c.current_nibble=2;c.end_nibble=b.end_nibble;c.looping=false;
            const auto payload=bytes.subspan(size_t(cursor)+32+i*channel_bytes,channel_bytes);
            require(payload[0]==c.predictor_scale,"HPS block header/frame predictor mismatch");
            decode_audio_adpcm(payload,c,2,c.predictor_scale,c.history1,c.history2,c.pcm,budget);
            b.channels.push_back(std::move(c));
        }
        visited.emplace(cursor,blocks.size());blocks.push_back(std::move(b));cursor=blocks.back().next_offset;
    }
    require(!blocks.empty(),"HPS contains no audio blocks");
}
const AudioStreamBlock& DatAudioStream::block(uint32_t offset)const{
    auto found=std::find_if(blocks.begin(),blocks.end(),[&](const auto& b){return b.file_offset==offset;});
    require(found!=blocks.end(),"HPS requested block offset is absent");return *found;
}
}

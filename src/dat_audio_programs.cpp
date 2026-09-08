#include "dat_audio_programs.hpp"
#include <algorithm>
namespace melee_web {
namespace {void require(bool v,const char* m){if(!v)throw DatError(m);}}
DatAudioPrograms::DatAudioPrograms(std::span<const uint8_t> bytes){
 require(bytes.size()>=20&&bytes.size()<=4*1024*1024&&bytes.size()%4==0,"Invalid SEM byte count");
 words.reserve(bytes.size()/4);for(size_t i=0;i<bytes.size();i+=4)words.push_back(uint32_t(bytes[i])<<24|uint32_t(bytes[i+1])<<16|uint32_t(bytes[i+2])<<8|bytes[i+3]);
 size_t at=0;for(auto& table:tables){require(at<words.size(),"Missing SEM table count");uint32_t count=words[at++];require(count<=65536&&count<=words.size()-at,"SEM table exceeds bank");table.assign(words.begin()+at,words.begin()+at+count);at+=count;}
 require(!tables[2].empty()&&!tables[3].empty(),"SEM sound bank/program table is empty");
 require(std::is_sorted(tables[2].begin(),tables[2].end()),"SEM bank program starts are unordered");
 for(auto start:tables[2])require(start<tables[3].size(),"SEM bank start exceeds program table");
 for(unsigned t:{1U,3U,4U})for(auto offset:tables[t])require(offset%4==0&&offset/4>=at&&offset/4<words.size(),"SEM program pointer is outside command data");
 for(auto offset:tables[3]){
  const size_t begin=offset/4;bool terminal=false;size_t steps=0;uint64_t immediate_work=0;uint32_t loop_count=0;
  for(size_t i=begin;i<words.size()&&steps++<65536;i++){
   // FD words are original exporter metadata; the source switch advances
   // past them without changing the sound program state.
   uint32_t word=words[i],op=word>>24;require(op<=21||op==0xfd,"Unsupported SEM command opcode");
   if(op==2)loop_count=word&0xffff;
   if(op==3){uint32_t back=word&0xffffff;require(back>0&&back<=i-begin+1,"SEM loop escapes its program");
    bool delay=false,resets_counter=false;for(size_t j=i-back+1;j<=i;j++){uint32_t c=words[j],type=c>>24;resets_counter|=type==2;delay|=(type==0&&(c&0xffffff))||(((type>=6&&type<=11)||(type>=16&&type<=19))&&(c&0xffff00))||((type==12||type==13)&&(c&0xff0000));}
    require(delay||(loop_count>0&&!resets_counter),"SEM unbounded loop lacks a timed yield");
    if(!delay){immediate_work+=uint64_t(back)*loop_count;require(immediate_work<=65536,"SEM immediate loop work exceeds callback budget");}
   }
   if(op==14||op==15){terminal=true;break;}
  }
  require(terminal,"SEM program is unterminated or exceeds budget");
 }
}
uint32_t DatAudioPrograms::program_index(uint32_t id)const{
 uint32_t bank=id/10000,index=id%10000;require(bank<tables[2].size(),"SEM sound bank absent");index+=tables[2][bank];require(index<tables[3].size()&&(bank+1==tables[2].size()||index<tables[2][bank+1]),"SEM sound program absent");return index;
}
}

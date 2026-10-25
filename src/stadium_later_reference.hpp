#pragma once
// Fixed private original-reference contexts. Checked against the canonical JSON
// by the focused component control; expected source states do not enter gameplay.
#include <array>
#include <cstddef>
#include <cstdint>
namespace melee_web::stadium_later_reference {
struct Context {
 const char* id;
 unsigned profile, recipe_bytes, frames;
 std::array<unsigned,12> first,last;
 unsigned pause_first,pause_last,pause_frame,menu_ticks,notification_order;
 std::array<uint8_t,32> recipe_sha256;
};
inline constexpr Context legacy={"original-b-2006-940-v1",2,111922,2006,
 {0,145,270,462,675,807,940,1132,1343,1476,1601,1794},{144,269,461,674,806,939,1131,1342,1475,1600,1793,2005},
450,461,56,535,1887,
 {0x41,0xac,0x04,0x3c,0xa7,0xd8,0x69,0xa0,0x8f,0x86,0xcf,0x81,0xfc,0x7d,0x4c,0xe6,0xf0,0x5a,0xba,0x43,0x94,0x24,0x3f,0x4a,0xab,0xf6,0xf5,0x7a,0xa1,0xc9,0x4e,0x36}};
inline constexpr Context observed_b={"original-b-2013-943-v1",3,112230,2013,
 {0,150,283,476,690,818,943,1135,1347,1484,1609,1802},{149,282,475,689,817,942,1134,1346,1483,1608,1801,2012},
464,475,57,536,0,
 {0x5f,0xb5,0x3b,0xdc,0x71,0xb3,0x5b,0x80,0x0b,0xc3,0x45,0x1b,0x1d,0x91,0x17,0x70,0x9e,0xde,0xf2,0x41,0x3a,0x38,0xa8,0xf7,0x31,0x1e,0xe3,0x52,0x64,0x7a,0x67,0x55}};
inline bool known(const Context* c){return c==&legacy||c==&observed_b;}
template<class Recipe>bool topology(const Recipe& recipe,const Context& c){
 constexpr std::array<unsigned,12> scenes={1,2,3,4,1,2,3,4,1,2,3,4};
 if(!known(&c)||recipe.version!=8||recipe.frames.size()!=c.frames||
    recipe.spans.size()!=scenes.size()||!recipe.whole_session()||recipe.diagnostic_entity_prefix)return false;
 for(std::size_t i=0;i<scenes.size();++i)
  if(recipe.spans[i].scene!=scenes[i]||recipe.spans[i].first_frame!=c.first[i]||
     recipe.spans[i].last_frame!=c.last[i])return false;
 return true;
}
}

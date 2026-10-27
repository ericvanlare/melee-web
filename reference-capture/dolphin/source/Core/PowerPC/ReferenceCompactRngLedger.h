// Copyright 2026 Melee Web contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace ReferenceCapture::CompactRng
{
constexpr std::size_t ROW_BYTES = 64;
constexpr std::size_t MAX_HEADER_BYTES = 4096;
constexpr std::size_t FOOTER_BYTES = 176;
constexpr std::size_t MAX_BYTES = 16 * 1024 * 1024;
constexpr std::uint32_t MAX_LIFETIME_EVENTS = 131072;
constexpr std::uint32_t REQUESTED_PAIR_EVENTS = 262144;
constexpr std::uint32_t MAX_EVENTS = (MAX_BYTES - 8 - MAX_HEADER_BYTES - FOOTER_BYTES) / ROW_BYTES;
constexpr std::uint32_t LAST_OFFSET = 3500;
constexpr std::uint32_t MAX_NATURAL_TAIL = 4;
constexpr std::size_t QUEUE_ROWS = 4096;
static_assert(MAX_EVENTS < REQUESTED_PAIR_EVENTS);
static_assert(8 + MAX_HEADER_BYTES + MAX_EVENTS * ROW_BYTES + FOOTER_BYTES <= MAX_BYTES);

inline void Put(std::uint8_t*& p, std::uint64_t value, unsigned bytes)
{
  for (unsigned i = bytes; i != 0; --i) *p++ = static_cast<std::uint8_t>(value >> ((i - 1) * 8));
}
inline bool Mem1(std::uint32_t p) { return p >= 0x80000000 && p <= 0x817ffffc && !(p & 3); }
struct Record
{
  std::uint8_t site = 0, status = 0, symbol = 0, slot = 0xff;
  std::uint32_t tick = 0, match = 0, ordinal = 0;
  std::uint64_t sequence = 0;
  std::uint32_t draw = 0, lr = 0, seed_pointer = 0, seed = 0;
  std::uint32_t gobj = 0, proc = 0, callback = 0;
  std::uint16_t classifier = 0;
  std::uint8_t p_link = 0, s_link = 0, entity = 0xff;
  std::uint32_t fighter_kind = 0xffffffff;
};
// Codes retain original return-site identity: Rand, Randf, Randi respectively.
// Symbol zero is outside; one explicitly unresolved; remaining codes are exact
// address mappings, never inferred from a classifier/link tuple.
inline std::uint8_t Symbol(std::uint32_t callback)
{
  return !callback ? 0 : callback == 0x801d1390 ? 2 : callback == 0x8005c9a4 ? 3 :
         callback == 0x8006a360 ? 4 : 1;
}
inline bool Encode(const Record& r, std::array<std::uint8_t, ROW_BYTES>* output)
{
  if (!output || r.site < 1 || r.site > 3 || r.status > 1 || r.symbol > 4 ||
      !Mem1(r.seed_pointer) || !Mem1(r.lr) || r.sequence > 9007199254740991ULL)
    return false;
  if (!r.status)
  {
    if (r.symbol || r.gobj || r.proc || r.callback || r.classifier || r.p_link || r.s_link ||
        r.slot != 0xff || r.entity != 0xff || r.fighter_kind != 0xffffffff) return false;
  }
  else
  {
    if (!Mem1(r.gobj) || !Mem1(r.proc) || !Mem1(r.callback) || r.symbol != Symbol(r.callback))
      return false;
    if (r.symbol == 4)
    {
      if (r.classifier != 4 || r.p_link != 8 || r.s_link != 1 || r.slot >= 4 || r.entity >= 2)
        return false;
    }
    else if (r.slot != 0xff || r.entity != 0xff || r.fighter_kind != 0xffffffff) return false;
  }
  auto* p = output->data();
  *p++ = r.site; *p++ = r.status; *p++ = r.symbol; *p++ = r.slot;
  Put(p,r.tick,4);Put(p,r.match,4);Put(p,r.ordinal,4);Put(p,r.sequence,8);
  Put(p,r.draw,4);Put(p,r.lr,4);Put(p,r.seed_pointer,4);Put(p,r.seed,4);
  Put(p,r.gobj,4);Put(p,r.proc,4);Put(p,r.callback,4);Put(p,r.classifier,2);
  *p++=r.p_link;*p++=r.s_link;*p++=r.entity;*p++=0;*p++=0;*p++=0;
  Put(p,r.fighter_kind,4);
  return p == output->data() + output->size();
}
struct Budget
{
  std::uint32_t total = 0;
  std::array<std::uint32_t,2> lifetime{};
  bool Accept(unsigned index)
  {
    if (index >= lifetime.size() || total >= MAX_EVENTS || lifetime[index] >= MAX_LIFETIME_EVENTS)
      return false;
    ++total; ++lifetime[index]; return true;
  }
};
// The observer reads actual GObj/Fighter spans and uses its existing registered
// source entity map before calling this leaf. No guest reads occur in the leaf.
inline bool FighterIdentity(std::uint32_t gobj, std::uint32_t fighter, std::uint32_t backlink,
                            std::uint8_t actual_slot, std::uint32_t actual_kind,
                            std::uint32_t registered_fighter, std::uint32_t registered_kind)
{
  return Mem1(gobj) && Mem1(fighter) && backlink == gobj && actual_slot < 4 &&
         fighter == registered_fighter && actual_kind == registered_kind;
}
}  // namespace ReferenceCapture::CompactRng

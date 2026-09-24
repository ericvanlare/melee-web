#ifndef MELEE_WEB_SOURCE_DSP_HW_REGS_H
#define MELEE_WEB_SOURCE_DSP_HW_REGS_H

/* Keep every authored register name and offset from the pinned header, while
 * replacing only the MMIO storage with a checked fixture object. */
#include_next <dolphin/hw_regs.h>

#undef __DSPRegs
#define __DSPRegs melee_web_dsp_regs

class MeleeWebDspRegister {
public:
    explicit MeleeWebDspRegister(unsigned index) : index_(index) {}
    operator u16() const;
    MeleeWebDspRegister& operator=(u16 value);

private:
    unsigned index_;
};

class MeleeWebDspRegisterBank {
public:
    MeleeWebDspRegister operator[](unsigned index) {
        return MeleeWebDspRegister(index);
    }
};

extern MeleeWebDspRegisterBank melee_web_dsp_regs;

#endif

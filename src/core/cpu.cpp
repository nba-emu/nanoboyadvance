/*
 * Copyright (C) 2018 Frederic Meyer. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <climits>
#include <cstring>
#include "cpu.hpp"

using namespace ARM;
using namespace NanoboyAdvance::GBA;

constexpr int CPU::s_ws_nseq[4]; /* Non-sequential SRAM/WS0/WS1/WS2 */
constexpr int CPU::s_ws_seq0[2]; /* Sequential WS0 */
constexpr int CPU::s_ws_seq1[2]; /* Sequential WS1 */
constexpr int CPU::s_ws_seq2[2]; /* Sequential WS2 */

CPU::CPU(Config* config)
    : config(config)
    , cpu(this)
    , apu(this)
    , ppu(this)
    , dma(this)
    , timers(this)
{
    Reset();
}
    
void CPU::Reset() {
    cpu.Reset();

    auto& state = cpu.GetState();

    state.bank[ARM::BANK_SVC][ARM::BANK_R13] = 0x03007FE0; 
    state.bank[ARM::BANK_IRQ][ARM::BANK_R13] = 0x03007FA0;
    state.reg[13] = 0x03007F00;
    state.cpsr.f.mode = ARM::MODE_USR;
    state.r15 = 0x08000000;

    /* Clear-out all memory buffers. */
    std::memset(memory.bios, 0, 0x04000);
    std::memset(memory.wram, 0, 0x40000);
    std::memset(memory.iram, 0, 0x08000);
    std::memset(memory.pram, 0, 0x00400);
    std::memset(memory.oam,  0, 0x00400);
    std::memset(memory.vram, 0, 0x18000);

    /* Load BIOS. This really should not be done here. */
    size_t size;
    auto file = std::fopen("bios.bin", "rb");
    std::uint8_t* rom;

    if (file == nullptr) {
        std::puts("Error: unable to open bios.bin");
        while (1) {}
        return;
    }

    std::fseek(file, 0, SEEK_END);
    size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    if (size > 0x4000) {
        std::puts("Error: BIOS image too large.");
        return;
    }

    if (std::fread(memory.bios, 1, size, file) != size) {
        std::puts("Error: unable to fully read the ROM.");
        return;
    }

    mmio.keyinput = 0x3FF;

    /* Reset interrupt control. */
    mmio.irq_ie = 0;
    mmio.irq_if = 0;
    mmio.irq_ime = 0;

    /* Reset waitstates. */
    mmio.waitcnt.sram = 0;
    mmio.waitcnt.ws0_n = 0;
    mmio.waitcnt.ws0_s = 0;
    mmio.waitcnt.ws1_n = 0;
    mmio.waitcnt.ws1_s = 0;
    mmio.waitcnt.ws2_n = 0;
    mmio.waitcnt.ws2_s = 0;
    mmio.waitcnt.phi = 0;
    mmio.waitcnt.prefetch = 0;
    mmio.waitcnt.cgb = 0;
    /* TODO: implement register 0x04000800. */
    for (int i = 0; i < 2; i++) {
        cycles16[i][0x0] = 1;
        cycles32[i][0x0] = 1;
        cycles16[i][0x1] = 1;
        cycles32[i][0x1] = 1;
        cycles16[i][0x2] = 3;
        cycles32[i][0x2] = 6;
        cycles16[i][0x3] = 1;
        cycles32[i][0x3] = 1;
        cycles16[i][0x4] = 1;
        cycles32[i][0x4] = 1;
        cycles16[i][0x5] = 1;
        cycles32[i][0x5] = 2;
        cycles16[i][0x6] = 1;
        cycles32[i][0x6] = 2;
        cycles16[i][0x7] = 1;
        cycles32[i][0x7] = 1;
        cycles16[i][0xF] = 1;
        cycles32[i][0xF] = 1;
    }
    UpdateCycleLUT();

    mmio.haltcnt = HaltControl::RUN;

    timers.Reset();
    dma.Reset();
    apu.Reset();
    ppu.Reset();
}

/* TODO: Does this really belong into the CPU class? */
void CPU::SetSlot1(uint8_t* rom, size_t size) {
    memory.rom.data = rom;
    memory.rom.size = size;
    Reset();
}

void CPU::RegisterEvent(EventDevice& event) {
    events.insert(&event);
}

void CPU::UnregisterEvent(EventDevice& event) {
    events.erase(&event);
}

void CPU::RunFor(int cycles) {
    int previous;
    
    while (cycles > 0) {
        ticks_cpu_left += ticks_to_event;
        
        while (ticks_cpu_left > 0) {
            std::uint16_t fire = mmio.irq_ie & mmio.irq_if;

            if (mmio.haltcnt == HaltControl::HALT && fire)
                mmio.haltcnt = HaltControl::RUN;

            previous = ticks_cpu_left;

            if (dma.IsRunning()) {
                dma.Run();
            } else if (mmio.haltcnt == HaltControl::RUN) {
                if (mmio.irq_ime && fire)
                    cpu.SignalIrq();
                cpu.Run();
            } else {
                /* TODO: inaccurate due to timer interrupts. */
                timers.Run(ticks_cpu_left);
                ticks_cpu_left = 0;
                break;
            }
        }
        
        int elapsed = ticks_to_event + ticks_cpu_left; /* CHECKME */
        
        cycles -= ticks_to_event;
        ticks_to_event = INT_MAX;
        
        /* Update cycle counters and get cycles to next event. */
        for (auto it = events.begin(); it != events.end();) {
            auto event = *it;
            ++it;
            event->wait_cycles -= elapsed;
            if (event->wait_cycles <= 0) {
                event->Tick();
            }
            if (event->wait_cycles < ticks_to_event) {
                ticks_to_event = event->wait_cycles;
            }
        }
    }
}

void CPU::UpdateCycleLUT() {
    auto& waitcnt = mmio.waitcnt;
    
    int sram_cycles = 1 + s_ws_nseq[waitcnt.sram];
    
    /* SRAM waitstates */
    cycles16[ACCESS_NSEQ][0xE] = sram_cycles;
    cycles32[ACCESS_NSEQ][0xE] = sram_cycles;
    cycles16[ACCESS_SEQ ][0xE] = sram_cycles;
    cycles32[ACCESS_SEQ ][0xE] = sram_cycles;

    /* ROM waitstates */
    for (int i = 0; i < 2; i++) {
        /* ROM: WS0/WS1/WS2 non-sequential timing. */
        cycles16[ACCESS_NSEQ][0x8 + i] = 1 + s_ws_nseq[waitcnt.ws0_n];
        cycles16[ACCESS_NSEQ][0xA + i] = 1 + s_ws_nseq[waitcnt.ws1_n];
        cycles16[ACCESS_NSEQ][0xC + i] = 1 + s_ws_nseq[waitcnt.ws2_n];
        
        /* ROM: WS0/WS1/WS2 sequential timing. */
        cycles16[ACCESS_SEQ][0x8 + i] = 1 + s_ws_seq0[waitcnt.ws0_s];
        cycles16[ACCESS_SEQ][0xA + i] = 1 + s_ws_seq1[waitcnt.ws1_s];
        cycles16[ACCESS_SEQ][0xC + i] = 1 + s_ws_seq2[waitcnt.ws2_s];
        
        /* ROM: WS0/WS1/WS2 32-bit non-sequential access: 1N access, 1S access */
        cycles32[ACCESS_NSEQ][0x8 + i] = cycles16[ACCESS_NSEQ][0x8] + 
                                         cycles16[ACCESS_SEQ ][0x8];
        cycles32[ACCESS_NSEQ][0xA + i] = cycles16[ACCESS_NSEQ][0xA] +
                                         cycles16[ACCESS_SEQ ][0xA];
        cycles32[ACCESS_NSEQ][0xC + i] = cycles16[ACCESS_NSEQ][0xC] +
                                         cycles16[ACCESS_SEQ ][0xC];
        
        /* ROM: WS0/WS1/WS2 32-bit sequential access: 2S accesses */
        cycles32[ACCESS_SEQ][0x8 + i] = cycles16[ACCESS_SEQ][0x8] * 2;
        cycles32[ACCESS_SEQ][0xA + i] = cycles16[ACCESS_SEQ][0xA] * 2;
        cycles32[ACCESS_SEQ][0xC + i] = cycles16[ACCESS_SEQ][0xC] * 2;    
    }
}

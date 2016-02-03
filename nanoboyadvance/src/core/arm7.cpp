/*
* Copyright (C) 2015 Frederic Meyer
*
* This file is part of nanoboyadvance.
*
* nanoboyadvance is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* nanoboyadvance is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with nanoboyadvance. If not, see <http://www.gnu.org/licenses/>.
*/

#include "arm7.h"

namespace NanoboyAdvance
{
    ARM7::ARM7(GBAMemory* memory, bool hle)
    {
        // Assign given memory instance to core
        this->memory = memory;

        // Map the static registers r0-r7, r15
        gprs[0] = &r0;
        gprs[1] = &r1;
        gprs[2] = &r2;
        gprs[3] = &r3;
        gprs[4] = &r4;
        gprs[5] = &r5;
        gprs[6] = &r6;
        gprs[7] = &r7;
        gprs[15] = &r15;
        RemapRegisters();

        // Skip bios boot logo
        r15 = 0x8000000;
        r13 = 0x3007F00;
        r13_svc = 0x3007FE0;
        r13_irq = 0x3007FA0;

        // Set hle flag
        this->hle = hle;

        // If speedup switch (ARM7_FASTHAX) is set, we need to build the decode caches
        #ifdef ARM7_FASTHAX
        for (int i = 0; i <= 0xFFFF; i++)
        {
            thumb_decode[i] = THUMBDecode(i);      
        }
        for (int i = 0; i <= 0xFFFFF; i++)
        {
            arm_decode[i] = ARMDecode((i & 0xFFF) | ((i & 0xFF000) << 8));
        }
        #endif
    }
    
    u32 ARM7::GetGeneralRegister(ARM7Mode mode, int r)
    {
        ARM7Mode old_mode = (ARM7Mode)(cpsr & 0x1F); // this code is quite hacky but it works
        u32 value;
        cpsr = (cpsr & ~0x1F) | (u32)mode;
        RemapRegisters();
        value = reg(r);
        cpsr = (cpsr & ~0x1F) | (u32)old_mode;
        RemapRegisters();
        return value;
    }

    u32 ARM7::GetCurrentStatusRegister()
    {
        return cpsr;
    }

    u32 ARM7::GetSavedStatusRegister(ARM7Mode mode)
    {
        switch (mode)
        {
        case ARM7Mode::FIQ: return spsr_fiq;
        case ARM7Mode::SVC: return spsr_svc;
        case ARM7Mode::Abort: return spsr_abt;
        case ARM7Mode::IRQ: return spsr_irq;
        case ARM7Mode::Undefined: return spsr_und;        
        }
        return 0;
    }

    void ARM7::SetCallback(ARMCallback hook)
    {
        debug_hook = hook;
    }

    void ARM7::SetGeneralRegister(ARM7Mode mode, int r, u32 value)
    {
        ARM7Mode old_mode = (ARM7Mode)(cpsr & 0x1F); // this code is quite hacky but it works
        cpsr = (cpsr & ~0x1F) | (u32)mode;
        RemapRegisters();
        reg(r) = value;
        cpsr = (cpsr & ~0x1F) | (u32)old_mode;
        RemapRegisters();
    }

    void ARM7::SetCurrentStatusRegister(u32 value)
    {
        cpsr = value;
    }
    
    void ARM7::SetSavedStatusRegister(ARM7Mode mode, u32 value)
    {
        switch (mode)
        {
        case ARM7Mode::FIQ: spsr_fiq = value; break;
        case ARM7Mode::SVC: spsr_svc = value; break;
        case ARM7Mode::Abort: spsr_abt = value; break;
        case ARM7Mode::IRQ: spsr_irq = value; break;
        case ARM7Mode::Undefined: spsr_und = value; break;        
        }
    }
    
    void ARM7::RemapRegisters()
    {
        switch (cpsr & 0x1F)
        {
        case (u32)ARM7Mode::User:
            gprs[8] = &r8;
            gprs[9] = &r9;
            gprs[10] = &r10;
            gprs[11] = &r11;
            gprs[12] = &r12;
            gprs[13] = &r13;
            gprs[14] = &r14;
            pspsr = &spsr_def;
            break;
        case (u32)ARM7Mode::FIQ:
            gprs[8] = &r8_fiq;
            gprs[9] = &r9_fiq;
            gprs[10] = &r10_fiq;
            gprs[11] = &r11_fiq;
            gprs[12] = &r12_fiq;
            gprs[13] = &r13_fiq;
            gprs[14] = &r14_fiq;
            pspsr = &spsr_fiq;
            break;
        case (u32)ARM7Mode::IRQ:
            gprs[8] = &r8;
            gprs[9] = &r9;
            gprs[10] = &r10;
            gprs[11] = &r11;
            gprs[12] = &r12;
            gprs[13] = &r13_irq;
            gprs[14] = &r14_irq;
            pspsr = &spsr_irq;
            break;
        case (u32)ARM7Mode::SVC:
            gprs[8] = &r8;
            gprs[9] = &r9;
            gprs[10] = &r10;
            gprs[11] = &r11;
            gprs[12] = &r12;
            gprs[13] = &r13_svc;
            gprs[14] = &r14_svc;
            pspsr = &spsr_svc;
            break;
        case (u32)ARM7Mode::Abort:
            gprs[8] = &r8;
            gprs[9] = &r9;
            gprs[10] = &r10;
            gprs[11] = &r11;
            gprs[12] = &r12;
            gprs[13] = &r13_abt;
            gprs[14] = &r14_abt;
            pspsr = &spsr_abt;
            break;
        case (u32)ARM7Mode::Undefined:
            gprs[8] = &r8;
            gprs[9] = &r9;
            gprs[10] = &r10;
            gprs[11] = &r11;
            gprs[12] = &r12;
            gprs[13] = &r13_und;
            gprs[14] = &r14_und;
            pspsr = &spsr_und;
            break;
        case (u32)ARM7Mode::System:
            gprs[8] = &r8;
            gprs[9] = &r9;
            gprs[10] = &r10;
            gprs[11] = &r11;
            gprs[12] = &r12;
            gprs[13] = &r13;
            gprs[14] = &r14;
            pspsr = &spsr_def;
            break;
        }
    }

    void ARM7::Step()
    {
        bool thumb = (cpsr & Thumb) == Thumb;
        u32 pc_page = r15 >> 24;
        ARMCallbackExecute* data = (ARMCallbackExecute*)malloc(sizeof(ARMCallbackExecute));
        
        // Tell the debugger which instruction we're currently at
        data->address = r15 - (thumb ? 4 : 8);
        data->thumb = thumb;
        DebugHook(ARM_CALLBACK_EXECUTE, data);
        free(data);
        
        // Determine if emulator is shitty
        if (pc_page == 0xFF) {
            cout << "Emulator is shitty, pipe_status=" << pipe_status << endl;
            string lol;
            cin >> lol;
        }

        // Determine if the cpu runs in arm or thumb mode and do corresponding work
        if (thumb)
        {
            r15 &= ~1;
            //switch (pipe_status)
            //{
            //case 0:
            if (pipe_status == 0)
            {            
                pipe_opcode[0] = memory->ReadHWord(r15);
                //break;
            }            
            //case 1:
            else if (pipe_status == 1)
            {            
                pipe_opcode[1] = memory->ReadHWord(r15);
                #ifdef ARM7_FASTHAX
                pipe_decode[0] = thumb_decode[pipe_opcode[0]];
                #else
                pipe_decode[0] = THUMBDecode(pipe_opcode[0]);
                #endif                
                //break;
            }
            //case 2:
            else if (pipe_status == 2)
            {
                pipe_opcode[2] = memory->ReadHWord(r15);
                #ifdef ARM7_FASTHAX
                pipe_decode[1] = thumb_decode[pipe_opcode[1]];
                #else
                pipe_decode[1] = THUMBDecode(pipe_opcode[1]);
                #endif    
                THUMBExecute(pipe_opcode[0], pipe_decode[0]);
                //break;
            }
            //case 3:
            else if (pipe_status == 3)
            {
                pipe_opcode[0] = memory->ReadHWord(r15);
                #ifdef ARM7_FASTHAX
                pipe_decode[2] = thumb_decode[pipe_opcode[2]];
                #else
                pipe_decode[2] = THUMBDecode(pipe_opcode[2]);
                #endif    
                THUMBExecute(pipe_opcode[1], pipe_decode[1]);
                //break;
            }
            //case 4:
            else 
            {
                pipe_opcode[1] = memory->ReadHWord(r15);
                #ifdef ARM7_FASTHAX
                pipe_decode[0] = thumb_decode[pipe_opcode[0]];
                #else
                pipe_decode[0] = THUMBDecode(pipe_opcode[0]);
                #endif    
                THUMBExecute(pipe_opcode[2], pipe_decode[2]);
                //break;
            }
        }
        else
        {
            r15 &= ~3;
            switch (pipe_status)
            {
            case 0:
                pipe_opcode[0] = memory->ReadWord(r15);
                break;
            case 1:
                pipe_opcode[1] = memory->ReadWord(r15);
                #ifdef ARM7_FASTHAX
                pipe_decode[0] = arm_decode[arm_pack_instr(pipe_opcode[0])];
                #else
                pipe_decode[0] = ARMDecode(pipe_opcode[0]);
                #endif                
                break;
            case 2:
                pipe_opcode[2] = memory->ReadWord(r15);
                #ifdef ARM7_FASTHAX
                pipe_decode[1] = arm_decode[arm_pack_instr(pipe_opcode[1])];
                #else
                pipe_decode[1] = ARMDecode(pipe_opcode[1]);
                #endif 
                ARMExecute(pipe_opcode[0], pipe_decode[0]);
                break;
            case 3:
                pipe_opcode[0] = memory->ReadWord(r15);
                #ifdef ARM7_FASTHAX
                pipe_decode[2] = arm_decode[arm_pack_instr(pipe_opcode[2])];
                #else
                pipe_decode[2] = ARMDecode(pipe_opcode[2]);
                #endif 
                ARMExecute(pipe_opcode[1], pipe_decode[1]);
                break;
            case 4:
                pipe_opcode[1] = memory->ReadWord(r15);
                #ifdef ARM7_FASTHAX
                pipe_decode[0] = arm_decode[arm_pack_instr(pipe_opcode[0])];
                #else
                pipe_decode[0] = ARMDecode(pipe_opcode[0]);
                #endif 
                ARMExecute(pipe_opcode[2], pipe_decode[2]);
                break;
            }
        }

        // Emulate "unpredictable" behaviour
        last_fetched_opcode = (cpsr & Thumb) ? ReadHWord(r15) : ReadWord(r15);
        last_fetched_offset = r15;
        if (r15 < 0x4000) last_bios_offset = r15;

        // Clear the pipeline if required
        if (flush_pipe)
        {
            pipe_status = 0;
            flush_pipe = false;
            return;
        }
        
        // Update instruction pointer
        r15 += thumb ? 2 : 4;
        
        // Update pipeline status
        if (++pipe_status == 5)
        {
            pipe_status = 2;
        }
    }

    void ARM7::FireIRQ()
    {
        if ((cpsr & IRQDisable) == 0)
        {
            r14_irq = r15 - ((cpsr & Thumb) ? 4 : 8) + 4;
            spsr_irq = cpsr;
            cpsr = (cpsr & ~0x3F) | (u32)ARM7Mode::IRQ | IRQDisable;
            RemapRegisters();
            r15 = 0x18;
            pipe_status = 0;
            //LOG(LOG_INFO, "Issued interrupt, r14_irq=0x%x, r15=0x%x", r14_irq, r15);
        }
        //else { LOG(LOG_INFO, "Interrupt(s) requested but blocked (either by interrupt or swi)") }
    }

    void NanoboyAdvance::ARM7::SWI(int number)
    {
        switch (number)
        {
        case 0x01: break;
        case 0x02: break;
        // DIV
        case 0x06:
        {
            u32 mod = r0 % r1;
            u32 div = r0 / r1;
            r0 = div;
            r1 = mod;
            break;
        }
        // CpuSet
        case 0x0B:
        {
            u32 source = r0;
            u32 dest = r1;
            u32 length = r2 & 0xFFFFF;
            bool fixed = r2 & (1 << 24) ? true : false;
            if (r2 & (1 << 26))
            {
                for (u32 i = 0; i < length; i++)
                {
                    WriteWord(dest, ReadWord(source));
                    dest += 4;
                    if (!fixed) source += 4;
                }
            }
            else
            {
                for (u32 i = 0; i < length; i++)
                {
                    WriteHWord(dest, ReadHWord(source));
                    dest += 2;
                    if (!fixed) source += 2;
                }
            }
            break;
        }
        // CpuFastSet
        case 0x0C:
        {
            u32 source = r0;
            u32 dest = r1;
            u32 length = r2 & 0xFFFFF;
            bool fixed = r2 & (1 << 24) ? true : false;
            for (u32 i = 0; i < length; i++)
            {
                WriteWord(dest, ReadWord(source));
                dest += 4;
                if (!fixed) source += 4;
            }
            break;
        }
        // LZ77UncompVRAM / LZ77UncompWRAM
        case 0x11:
        case 0x12:
        {
            int amount = memory->ReadWord(r0) >> 8;
            u32 source = r0 + 4;
            u32 dest = r1;
            while (amount > 0)
            {
                u8 encoder = memory->ReadByte(source++);

                // Process 8 blocks encoded by the encoder
                for (int i = 7; i >= 0; i--)
                {
                    if (encoder & (1 << i))
                    {
                        // Compressed
                        u16 value = memory->ReadHWord(source);
                        u32 disp = (value >> 8) | ((value & 0xF) << 8);
                        u32 n = ((value >> 4) & 0xF) + 3;
                        source += 2;

                        for (u32 j = 0; j < n; j++)
                        {
                            u16 value = memory->ReadByte(dest - disp - 1);
                            memory->WriteHWord(dest, value);
                            dest++;
                            amount--;
                            if (amount == 0)
                            {
                                return;
                            }
                        }
                    }
                    else
                    {
                        // Uncompressed
                        u8 value = memory->ReadByte(source++);
                        memory->WriteHWord(dest++, value);
                        amount--;
                        if (amount == 0)
                        {
                            return;
                        }
                    }
                }
            }
            break;
        }
        default:
            LOG(LOG_ERROR, "Unimplemented software interrupt 0x%x", number);
            break;
        }
    }
}

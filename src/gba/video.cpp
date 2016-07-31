///////////////////////////////////////////////////////////////////////////////////
//
//  NanoboyAdvance is a modern Game Boy Advance emulator written in C++
//  with performance, platform independency and reasonable accuracy in mind.
//  Copyright (C) 2016 Frederic Meyer
//
//  This file is part of nanoboyadvance.
//
//  nanoboyadvance is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 2 of the License, or
//  (at your option) any later version.
//
//  nanoboyadvance is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with nanoboyadvance. If not, see <http://www.gnu.org/licenses/>.
//
///////////////////////////////////////////////////////////////////////////////////


#include "video.h"


/* TODO: 1) Improve RenderSprites method and allow for OBJWIN
 *          mask generation
 *       2) Implement OBJWIN
 *       3) Lookup if offset to RenderSprites is always constant
 *       4) Fix rotate-scale logic and apply to RenderSprites and Mode3-5
 */

namespace NanoboyAdvance
{
    const int GBAVideo::VBLANK_INTERRUPT = 1;
    const int GBAVideo::HBLANK_INTERRUPT = 2;
    const int GBAVideo::VCOUNT_INTERRUPT = 4;


    ///////////////////////////////////////////////////////////
    /// \author  Frederic Meyer
    /// \date    July 31th, 2016
    /// \fn      Constructor
    ///
    ///////////////////////////////////////////////////////////
    GBAVideo::GBAVideo(GBAInterrupt* interrupt)
    {
        // Assign interrupt struct to video device
        this->m_Interrupt = interrupt;

        // Zero init memory buffers
        memset(m_PAL, 0, 0x400);
        memset(m_VRAM, 0, 0x18000);
        memset(m_OAM, 0, 0x400);
        memset(m_Buffer, 0, 240 * 160 * 4);
    }


    ///////////////////////////////////////////////////////////
    /// \author  Frederic Meyer
    /// \date    July 31th, 2016
    /// \fn      RenderBackgroundMode0
    ///
    ///////////////////////////////////////////////////////////
    void GBAVideo::RenderBackgroundMode0(int id)
    {
        struct Background bg = this->m_BG[id];

        int width = ((bg.size & 1) + 1) * 256;
        int height = ((bg.size >> 1) + 1) * 256;
        int y_scrolled = (m_VCount + bg.y) % height;
        int row = y_scrolled / 8;
        int row_rmdr = y_scrolled % 8;
        int left_area = 0;
        int right_area = 1;
        u32 line_buffer[width];
        u32 offset;

        if (row >= 32)
        {
            left_area = (bg.size & 1) + 1;
            right_area = 3;
            row -= 32;
        }

        offset = bg.map_base + left_area * 0x800 + 64 * row;

        for (int x = 0; x < width / 8; x++)
        {
            u16 tile_encoder = (m_VRAM[offset + 1] << 8) | 
                                m_VRAM[offset];
            int tile_number = tile_encoder & 0x3FF;
            bool horizontal_flip = tile_encoder & (1 << 10);
            bool vertical_flip = tile_encoder & (1 << 11); 
            int row_rmdr_flip = row_rmdr;
            u32* tile_data;

            if (vertical_flip)
                row_rmdr_flip = 7 - row_rmdr_flip;

            if (bg.true_color)
            {
                tile_data = DecodeTileLine8BPP(bg.tile_base, tile_number, row_rmdr_flip, false);
            } else 
            {
                int palette = tile_encoder >> 12;
                tile_data = DecodeTileLine4BPP(bg.tile_base, palette * 0x20, tile_number, row_rmdr_flip);
            }

            if (horizontal_flip)
                for (int i = 0; i < 8; i++)
                    line_buffer[x * 8 + i] = tile_data[7 - i];
            else
                for (int i = 0; i < 8; i++)
                    line_buffer[x * 8 + i] = tile_data[i];

            delete[] tile_data;
            
            if (x == 31) 
                offset = bg.map_base + right_area * 0x800 + 64 * row;
            else
                offset += 2;
        }

        for (int i = 0; i < 240; i++)
            m_BgBuffer[id][i] = line_buffer[(bg.x + i) % width];
    }

    ///////////////////////////////////////////////////////////
    /// \author  Frederic Meyer
    /// \date    July 31th, 2016
    /// \fn      RenderBackgroundMode1
    ///
    ///////////////////////////////////////////////////////////
    void GBAVideo::RenderBackgroundMode1(int id)
    {
        // Rendering variables
        u32 tile_block_base = m_BG[id].tile_base;
        u32 map_block_base = m_BG[id].map_base;
        bool wraparound = m_BG[id].wraparound;
        int blocks = ((m_BG[id].size) + 1) << 4;
        int size = blocks * 8;
        
        for (int i = 0; i < 240; i++) {
            float dec_bgx = m_BG[id].x_ref_int;
            float dec_bgy = m_BG[id].y_ref_int;
            float dec_bgpa = DecodeGBAFloat16(m_BG[id].pa);
            float dec_bgpb = DecodeGBAFloat16(m_BG[id].pb);
            float dec_bgpc = DecodeGBAFloat16(m_BG[id].pc);
            float dec_bgpd = DecodeGBAFloat16(m_BG[id].pd);
            int x = dec_bgx + (dec_bgpa * i) + (dec_bgpb * m_VCount);
            int y = dec_bgy + (dec_bgpc * i) + (dec_bgpd * m_VCount);
            int tile_internal_line;
            int tile_internal_x;
            int tile_row;
            int tile_column;
            int tile_number;
            
            if ((x >= size) || (y >= size)) {
                if (wraparound) {
                    x = x % size;
                    y = y % size;
                } else {
                    m_BgBuffer[id][i] = 0;
                    continue;
                }
            }
            if (x < 0) {
                if (wraparound) {
                    x = (size + x) % size;
                } else {
                    m_BgBuffer[id][i] = 0;
                    continue;
                }
            }
            if (y < 0) {
                if (wraparound) {
                    y = (size + y) % size;
                } else {
                    m_BgBuffer[id][i] = 0;
                    continue;
                }
            }
            
            tile_internal_line = y % 8;
            tile_internal_x = x % 8;
            tile_row = (y - tile_internal_line) / 8;
            tile_column = (x - tile_internal_x) / 8;
            tile_number = m_VRAM[map_block_base + tile_row * blocks + tile_column];
            m_BgBuffer[id][i] = DecodeTilePixel8BPP(tile_block_base, tile_number, tile_internal_line, tile_internal_x, false);
        }
    }
       
    ///////////////////////////////////////////////////////////
    /// \author  Frederic Meyer
    /// \date    July 31th, 2016
    /// \fn      RenderSprites
    ///
    ///////////////////////////////////////////////////////////
    void GBAVideo::RenderSprites(int priority, u32 tile_base)
    {
        // Process OBJ127 first, because OBJ0 has highest priority (OBJ0 overlays OBJ127, not vice versa)
        u32 offset = 127 * 8;

        // Walk all entries
        for (int i = 0; i < 128; i++)
        {
            u16 attribute0 = (m_OAM[offset + 1] << 8) | m_OAM[offset];
            u16 attribute1 = (m_OAM[offset + 3] << 8) | m_OAM[offset + 2];
            u16 attribute2 = (m_OAM[offset + 5] << 8) | m_OAM[offset + 4];

            // Only render those which have matching priority
            if (((attribute2 >> 10) & 3) == priority)
            {
                int width;
                int height;
                int x = attribute1 & 0x1FF;
                int y = attribute0 & 0xFF;
                GBAVideoSpriteShape shape = static_cast<GBAVideoSpriteShape>(attribute0 >> 14);
                int size = attribute1 >> 14;

                // Decode width and height
                switch (shape)
                {
                case GBAVideoSpriteShape::Square:
                    switch (size)
                    {
                    case 0: width = 8; height = 8; break;
                    case 1: width = 16; height = 16; break;
                    case 2: width = 32; height = 32; break;
                    case 3: width = 64; height = 64; break;
                    }
                    break;
                case GBAVideoSpriteShape::Horizontal:
                    switch (size)
                    {
                    case 0: width = 16; height = 8; break;
                    case 1: width = 32; height = 8; break;
                    case 2: width = 32; height = 16; break;
                    case 3: width = 64; height = 32; break;
                    }
                    break;
                case GBAVideoSpriteShape::Vertical:
                    switch (size)
                    {
                    case 0: width = 8; height = 16; break;
                    case 1: width = 8; height = 32; break;
                    case 2: width = 16; height = 32; break;
                    case 3: width = 32; height = 64; break;
                    }
                    break;
                case GBAVideoSpriteShape::Prohibited:
                    width = 0;
                    height = 0;
                    break;
                }

                // Determine if there is something to render for this sprite
                if (m_VCount >= y && m_VCount <= y + height - 1)
                {
                    int internal_line = m_VCount - y;
                    int displacement_y;
                    int row;
                    int tiles_per_row = width / 8;
                    int tile_number = attribute2 & 0x3FF;
                    int palette_number = attribute2 >> 12;
                    bool rotate_scale = attribute0 & (1 << 8) ? true : false;
                    bool horizontal_flip = !rotate_scale && (attribute1 & (1 << 12));
                    bool vertical_flip = !rotate_scale && (attribute1 & (1 << 13));
                    bool color_mode = attribute0 & (1 << 13) ? true : false;
                    
                    // It seems like the tile number is divided by two in 256 color mode
                    // Though we should check this because I cannot find information about this in GBATEK
                    if (color_mode)
                    {
                        tile_number /= 2;
                    }

                    // Apply (outer) vertical flip if required
                    if (vertical_flip) 
                    {
                        internal_line = height - internal_line;                    
                    }

                    // Calculate some stuff
                    displacement_y = internal_line % 8;
                    row = (internal_line - displacement_y) / 8;

                    // Apply (inner) vertical flip
                    if (vertical_flip)
                    {
                        displacement_y = 7 - displacement_y;
                        row = (height / 8) - row;
                    }

                    // Render all visible tiles of the sprite
                    for (int j = 0; j < tiles_per_row; j++)
                    {
                        int current_tile_number;
                        u32* tile_data;

                        // Determine the tile to render
                        if (m_Obj.two_dimensional)
                        {
                            current_tile_number = tile_number + row * tiles_per_row + j;
                        }
                        else
                        {
                            current_tile_number = tile_number + row * 32 + j;
                        }

                        // Render either in 256 colors or 16 colors mode
                        if (color_mode)
                        {
                            // 256 colors
                            tile_data = DecodeTileLine8BPP(tile_base, current_tile_number, displacement_y, true);
                        }
                        else
                        {
                            // 16 colors (use palette_nummer)
                            tile_data = DecodeTileLine4BPP(tile_base, 0x200 + palette_number * 0x20, current_tile_number, displacement_y);
                        }

                        // Copy data
                        if (horizontal_flip) // not very beautiful but faster
                        {
                            for (int k = 0; k < 8; k++)
                            {
                                int dst_index = x + (tiles_per_row - j - 1) * 8 + (7 - k);
                                u32 color = tile_data[k];
                                if ((color >> 24) != 0 && dst_index < 240)
                                {
                                    m_ObjBuffer[priority][dst_index] = color;
                                }
                            }

                        }
                        else
                        {
                            for (int k = 0; k < 8; k++)
                            {
                                int dst_index = x + j * 8 + k;
                                u32 color = tile_data[k];
                                if ((color >> 24) != 0 && dst_index < 240)
                                {
                                    m_ObjBuffer[priority][dst_index] = tile_data[k];
                                }
                            }
                        }

                        // We don't need that memory anymore
                        delete[] tile_data;
                    }
                }
            }

            // Update offset to the next entry
            offset -= 8;
        }
    }    

    ///////////////////////////////////////////////////////////
    /// \author  Frederic Meyer
    /// \date    July 31th, 2016
    /// \fn      Render
    ///
    ///////////////////////////////////////////////////////////
    void GBAVideo::Render()
    {
        bool first_bg = true;
        bool win_none = !m_Win[0].enable && !m_Win[1].enable && !m_ObjWin.enable;
        
        // Reset obj buffers
        memset(m_ObjBuffer[0], 0, sizeof(u32)*240);
        memset(m_ObjBuffer[1], 0, sizeof(u32)*240);
        memset(m_ObjBuffer[2], 0, sizeof(u32)*240);
        memset(m_ObjBuffer[3], 0, sizeof(u32)*240);

        // Emulate the effect caused by "Forced Blank"
        if (m_ForcedBlank) {
            for (int i = 0; i < 240; i++) {
                m_Buffer[m_VCount * 240 + i] = 0xFFF8F8F8;
            }
            return;
        }

        // Call mode specific rendering logic
        switch (m_VideoMode) {
        case 0:
        {
            // BG Mode 0 - 240x160 pixels, Text mode
            // TODO: Consider using no loop
            for (int i = 0; i < 4; i++) {
                if (m_BG[i].enable) {
                    RenderBackgroundMode0(i);
                }
            }
            break;
        }
        case 1:
        {        
            // BG Mode 1 - 240x160 pixels, Text and RS mode mixed
            if (m_BG[0].enable) {
                RenderBackgroundMode0(0);
            }
            if (m_BG[1].enable) {
                RenderBackgroundMode0(1);
            }
            if (m_BG[2].enable) {
                RenderBackgroundMode1(2);
            }
            break;
        }
        case 2:
        {
            // BG Mode 2 - 240x160 pixels, RS mode
            if (m_BG[2].enable) {
                RenderBackgroundMode1(2);
            }
            if (m_BG[3].enable) {
                RenderBackgroundMode1(3);
            }
            break;
        }
        case 3:
            // BG Mode 3 - 240x160 pixels, 32768 colors
            // Bitmap modes are rendered on BG2 which means we must check if it is enabled
            if (m_BG[2].enable)
            {
                u32 offset = m_VCount * 240 * 2;
                for (int x = 0; x < 240; x++)
                {
                    m_BgBuffer[2][x] = DecodeRGB5((m_VRAM[offset + 1] << 8) | m_VRAM[offset]);
                    offset += 2;
                }
            }
            break;
        case 4:
            // BG Mode 4 - 240x160 pixels, 256 colors (out of 32768 colors)
            // Bitmap modes are rendered on BG2 which means we must check if it is enabled
            if (m_BG[2].enable)
            {
                u32 page = m_FrameSelect ? 0xA000 : 0;
                for (int x = 0; x < 240; x++)
                {
                    u8 index = m_VRAM[page + m_VCount * 240 + x];
                    u16 rgb5 = m_PAL[index * 2] | (m_PAL[index * 2 + 1] << 8);
                    m_BgBuffer[2][x] = DecodeRGB5(rgb5);
                }
            }
            break;
        case 5:
            // BG Mode 5 - 160x128 pixels, 32768 colors
            // Bitmap modes are rendered on BG2 which means we must check if it is enabled
            if (m_BG[2].enable)
            {
                u32 offset = (m_FrameSelect ? 0xA000 : 0) + m_VCount * 160 * 2;
                for (int x = 0; x < 240; x++)
                {
                    if (x < 160 && m_VCount < 128)
                    {
                        m_BgBuffer[2][x] = DecodeRGB5((m_VRAM[offset + 1] << 8) | m_VRAM[offset]);
                        offset += 2;
                    }
                    else
                    {
                        // The unused space is filled with the first color from pal ram as far as I can see
                        u16 rgb5 = m_PAL[0] | (m_PAL[1] << 8);
                        m_BgBuffer[2][x] = DecodeRGB5(rgb5);
                    }
                }
            }
            break;
        }
        
        // Check if objects are enabled..
        if (m_Obj.enable) {
            // .. and render all of them to their buffers if so
            RenderSprites(0, 0x10000);
            RenderSprites(1, 0x10000);
            RenderSprites(2, 0x10000);
            RenderSprites(3, 0x10000);
        }
    
        // Compose screen
        if (win_none) {
            for (int i = 3; i >= 0; i--) {
                for (int j = 3; j >= 0; j--) {
                    if (m_BG[j].enable && m_BG[j].priority == i) {
                        DrawLineToBuffer(m_BgBuffer[j], first_bg);
                        first_bg = false;
                    }
                }
                if (m_Obj.enable) {
                    DrawLineToBuffer(m_ObjBuffer[i], false);
                }
            }
        } else {
            // Compose outer window area
            for (int i = 3; i >= 0; i--) {
                for (int j = 3; j >= 0; j--) {
                    if (m_BG[j].enable && m_BG[j].priority == i && m_WinOut.bg[j]) {
                        DrawLineToBuffer(m_BgBuffer[j], first_bg);
                        first_bg = false;
                    }
                }
                if (m_Obj.enable && m_WinOut.obj) {
                    DrawLineToBuffer(m_ObjBuffer[i], false);
                }
            }
            
            // Compose inner window[0/1] area
            for (int i = 1; i >= 0; i--) {
                if (m_Win[i].enable && (
                    (m_Win[i].top <= m_Win[i].bottom && m_VCount >= m_Win[i].top && m_VCount <= m_Win[i].bottom) ||
                    (m_Win[i].top > m_Win[i].bottom && !(m_VCount <= m_Win[i].top && m_VCount >= m_Win[i].bottom))
                )) {
                    u32 win_buffer[240];
                    
                    // Anything that is not covered by bg and obj will be black
                    for (int j = 0; j < 240; j++) {
                        win_buffer[j] = 0xFF000000;
                    }

                    // Draw backgrounds and sprites if any
                    for (int j = 3; j >= 0; j--) {
                        for (int k = 3; k >= 0; k--) {
                            if (m_BG[k].enable && m_BG[k].priority == j && m_Win[i].bg_in[k]) {
                                OverlayLineBuffers(win_buffer, m_BgBuffer[k]);
                            }
                        }
                        if (m_Obj.enable && m_Win[i].obj_in) {
                            OverlayLineBuffers(win_buffer, m_ObjBuffer[j]);
                        }
                    }

                    // Make the window buffer transparent in the outer area
                    if (m_Win[i].left <= m_Win[i].right + 1) {
                        for (int j = 0; j <= m_Win[i].left; j++) {
                            if (j < 240) {
                                win_buffer[j] = 0;
                            }
                        }
                        for (int j = m_Win[i].right; j < 240; j++) {
                            if (j >= 0) {
                                win_buffer[j] = 0;
                            }
                        }
                    } else {
                        for (int j = m_Win[i].right; j <= m_Win[i].left; j++) {
                            if (j >= 0 && j < 240) {
                                win_buffer[j] = 0;
                            }
                        }
                    }
                    
                    // Draw the window buffer
                    DrawLineToBuffer(win_buffer, false);
                }
            }
        }
    }

    ///////////////////////////////////////////////////////////
    /// \author  Frederic Meyer
    /// \date    July 31th, 2016
    /// \fn      Step
    ///
    ///////////////////////////////////////////////////////////
    void NanoboyAdvance::GBAVideo::Step()
    {
        m_Ticks++;
        m_RenderScanline = false;
        m_VCountFlag = m_VCount == m_VCountSetting;

        switch (m_State)
        {
        case GBAVideoState::Scanline:
        {
            if (m_Ticks >= 960)
            {
                m_HBlankDMA = true;
                m_State = GBAVideoState::HBlank;
                
                if (m_HBlankIRQ)
                    m_Interrupt->if_ |= HBLANK_INTERRUPT;

                //Render(vcount);
                m_RenderScanline = true;
                m_Ticks = 0;
            }
            break;
        }
        case GBAVideoState::HBlank:
            if (m_Ticks >= 272)
            {
                m_VCount++;

                if (m_VCountFlag && m_VCountIRQ)
                    m_Interrupt->if_ |= VCOUNT_INTERRUPT;
                
                if (m_VCount == 160)
                {
                    m_BG[2].x_ref_int = DecodeGBAFloat32(m_BG[2].x_ref);
                    m_BG[2].y_ref_int = DecodeGBAFloat32(m_BG[2].y_ref);
                    m_BG[3].x_ref_int = DecodeGBAFloat32(m_BG[3].x_ref);
                    m_BG[3].y_ref_int = DecodeGBAFloat32(m_BG[3].y_ref);

                    m_HBlankDMA = false;
                    m_VBlankDMA = true;
                    m_State = GBAVideoState::VBlank;

                    if (m_VBlankIRQ)
                        m_Interrupt->if_ |= VBLANK_INTERRUPT;
                }
                else
                {
                    m_HBlankDMA = false;
                    m_State = GBAVideoState::Scanline;
                }

                m_Ticks = 0;
            }
            break;
        case GBAVideoState::VBlank:
        {
            if (m_Ticks >= 1232)
            {
                m_VCount++;

                if (m_VCountFlag && m_VCountIRQ)
                    m_Interrupt->if_ |= VCOUNT_INTERRUPT;

                if (m_VCount == 227)
                {
                    m_VBlankDMA = false;
                    m_State = GBAVideoState::Scanline;
                    m_VCount = 0;
                }

                m_Ticks = 0;
            }
            break;
        }
        }
    }
}
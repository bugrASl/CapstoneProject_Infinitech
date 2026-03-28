/*
 * @file    wireless_packet.c
 * @brief   Wireless packet structure and compression implementation.
 * @author  bugrASl
 */

#include "wireless_packet.h"

/*============== WL_Pack =================================================================================*/

void WL_Pack(const WL_Packet *in, uint8_t *out)
{
    out[0]                                              =   in->seq;
    out[1]                                              =   in->flags;

    int byte_idx                                        =   2;

    for (int s = 0; s < WL_SAMPLES_PER_PACKET; s++)
    {
        for (int c = 0; c < WL_NUM_CHANNELS; c += 2)
        {
            uint16_t val0                               =   in->samples[s].ch[c] & 0x0FFF;
            uint16_t val1                               =   in->samples[s].ch[c + 1] & 0x0FFF;

            out[byte_idx++]                             =   (uint8_t)(val0 & 0xFF);
            out[byte_idx++]                             =   (uint8_t)(((val0 >> 8) & 0x0F) | ((val1 << 4) & 0xF0));
            out[byte_idx++]                             =   (uint8_t)((val1 >> 4) & 0xFF);
        }
    }

    for (int i = 0; i < WL_RESERVED_SIZE; i++)
    {
        out[byte_idx++]                                 =   in->reserved[i];
    }
}

/*==========================================================================================================*/

/*============== WL_Unpack ===============================================================================*/

void WL_Unpack(const uint8_t *in, WL_Packet *out)
{
    out->seq                                            =   in[0];
    out->flags                                          =   in[1];

    int byte_idx                                        =   2;

    for (int s = 0; s < WL_SAMPLES_PER_PACKET; s++)
    {
        for (int c = 0; c < WL_NUM_CHANNELS; c += 2)
        {
            uint8_t b0                                  =   in[byte_idx++];
            uint8_t b1                                  =   in[byte_idx++];
            uint8_t b2                                  =   in[byte_idx++];

            out->samples[s].ch[c]                       =   (uint16_t)b0 | ((uint16_t)(b1 & 0x0F) << 8);
            out->samples[s].ch[c + 1]                   =   (uint16_t)((b1 & 0xF0) >> 4) | ((uint16_t)b2 << 4);
        }
    }

    for (int i = 0; i < WL_RESERVED_SIZE; i++)
    {
        out->reserved[i]                                =   in[byte_idx++];
    }
}

/*==========================================================================================================*/

/*
 * @file    wireless_packet.h
 * @brief   Wireless packet structure and compression logic.
 * @author  bugrASl
 */

#ifndef WIRELESS_PACKET_H
#define WIRELESS_PACKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*============== PACKET CONSTANTS ==========================================================================*/

#define WL_PAYLOAD_SIZE         32
#define WL_NUM_CHANNELS         6
#define WL_SAMPLES_PER_PACKET   3
#define WL_RESERVED_SIZE        3

/*============== FLAGS =====================================================================================*/

#define WL_FLAG_BATT_LOW        (1 << 0)
#define WL_FLAG_SYS_ERR         (1 << 1)

/*============== STRUCTURES ================================================================================*/

typedef struct {
    uint16_t ch[WL_NUM_CHANNELS];
} WL_SampleSet;

typedef struct {
    uint8_t         seq;
    uint8_t         flags;
    WL_SampleSet    samples[WL_SAMPLES_PER_PACKET];
    uint8_t         reserved[WL_RESERVED_SIZE];
} WL_Packet;

/*============== API =======================================================================================*/

void WL_Pack(const WL_Packet *in, uint8_t *out);
void WL_Unpack(const uint8_t *in, WL_Packet *out);

/*==========================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* WIRELESS_PACKET_H */


/*
 * @file	nrf24l01.c
 * @brief	Unified NRF24L01+ driver for STM32 HAL — TX and RX roles
 * @author	bugrASl
 *
 * This single driver serves both sides of the InfiniTech wireless link:
 *
 * Code structure:
 *   [1] Pin control helpers     — always compiled
 *   [2] SPI register access     — always compiled
 *   [3] Common utility          — always compiled
 *   [4] NRF_Init                — always compiled (branches on role internally)
 *   [5] RX-only functions       — compiled only if NRF_ROLE_RX
 *   [6] TX-only functions       — compiled only if NRF_ROLE_TX
 */
 
#include "nrf24l01.h"

/*=============== SPI CONTROL FUNCTIONS ====================================================================*/
uint8_t NRF_ReadReg(NRF_Handle *hnrf, uint8_t reg){
	uint8_t cmd = NRF_CMD_R_REGISTER | (reg & 0x1F);
    uint8_t val;
    csn_low();
    HAL_SPI_Transmit(hnrf->hspi, &cmd, 1, NRF_SPI_TIMEOUT_MS);
    HAL_SPI_Receive(hnrf->hspi, &val, 1, NRF_SPI_TIMEOUT_MS);
    csn_high();
    return val;
}
void    NRF_WriteReg(NRF_Handle *hnrf, uint8_t reg, uint8_t val){
	uint8_t cmd = NRF_CMD_W_REGISTER | (reg & 0x1F);
    csn_low();
    HAL_SPI_Transmit(hnrf->hspi, &cmd, 1, NRF_SPI_TIMEOUT_MS);
    HAL_SPI_Transmit(hnrf->hspi, &val, 1, NRF_SPI_TIMEOUT_MS);
    csn_high();
}
void    NRF_ReadRegMulti(NRF_Handle *hnrf, uint8_t reg, uint8_t *buf, uint8_t len){
	uint8_t cmd = NRF_CMD_R_REGISTER | (reg & 0x1F);
    csn_low();
    HAL_SPI_Transmit(hnrf->hspi, &cmd, 1, NRF_SPI_TIMEOUT_MS);
    HAL_SPI_Receive(hnrf->hspi, buf, len, NRF_SPI_TIMEOUT_MS);
    csn_high();
}
void    NRF_WriteRegMulti(NRF_Handle *hnrf, uint8_t reg, const uint8_t *buf, uint8_t len){
	uint8_t cmd = NRF_CMD_W_REGISTER | (reg & 0x1F);
    csn_low();
    HAL_SPI_Transmit(hnrf->hspi, &cmd, 1, NRF_SPI_TIMEOUT_MS);
    HAL_SPI_Transmit(hnrf->hspi, (uint8_t *)buf, len, NRF_SPI_TIMEOUT_MS);
    csn_high();
}
static uint8_t nrf_send_cmd(NRF_Handle *hnrf, uint8_t cmd){
    uint8_t status;
    csn_low();
    HAL_SPI_TransmitReceive(hnrf->hspi, &cmd, &status, 1, NRF_SPI_TIMEOUT_MS);
    csn_high();
    return status;
}
/*==========================================================================================================*/

/*==========================================================================================================*/
/*=============== COMMON UTILITY FUNCTIONS =================================================================*/
/*==========================================================================================================*/
uint8_t NRF_GetStatus(NRF_Handle *hnrf){
    hnrf->last_status = nrf_send_cmd(hnrf, NRF_CMD_NOP);
    return hnrf->last_status;
}
void NRF_ClearIRQ(NRF_Handle *hnrf, uint8_t flags){
    NRF_WriteReg(hnrf, NRF_REG_STATUS, flags);
}
void NRF_FlushRX(NRF_Handle *hnrf){
    nrf_send_cmd(hnrf, NRF_CMD_FLUSH_RX);
}
void NRF_FlushTX(NRF_Handle *hnrf){
    nrf_send_cmd(hnrf, NRF_CMD_FLUSH_TX);
}
void NRF_PowerDown(NRF_Handle *hnrf){
    ce_low();
    uint8_t config = NRF_ReadReg(hnrf, NRF_REG_CONFIG);
    config &= ~NRF_CONFIG_PWR_UP;
    NRF_WriteReg(hnrf, NRF_REG_CONFIG, config);
}
void NRF_PowerUp(NRF_Handle *hnrf){
    uint8_t config = NRF_ReadReg(hnrf, NRF_REG_CONFIG);
    config |= NRF_CONFIG_PWR_UP;
    NRF_WriteReg(hnrf, NRF_REG_CONFIG, config);
    HAL_Delay(2);												/* Oscillator startup: 1.5 ms per datasheet */
	#if defined(NRF_ROLE_RX)
    ce_high();     												/* RX: start listening */
	#endif														/* TX: CE stays low until NRF_Transmit() pulses it */
}
NRF_Status NRF_Init(NRF_Handle *hnrf, SPI_HandleTypeDef *hspi,
                     uint8_t channel, const uint8_t addr[NRF_ADDR_WIDTH]){
    /* Store parameters */
    hnrf->hspi    = hspi;
    hnrf->channel = channel;
    for (int i = 0; i < NRF_ADDR_WIDTH; i++)
        hnrf->rx_addr[i] = addr[i];

    /* Enter known state */
    ce_low();
    csn_high();
    HAL_Delay(5);

    /* Chip detection */
    uint8_t aw_val = (NRF_ADDR_WIDTH - 2);
    NRF_WriteReg(hnrf, NRF_REG_SETUP_AW, aw_val);
    if (NRF_ReadReg(hnrf, NRF_REG_SETUP_AW) != aw_val)
        return NRF_ERR_NOT_DETECTED;

    /* Radio parameters (shared by TX and RX) */
    NRF_WriteReg(hnrf, NRF_REG_RF_CH, channel);
    NRF_WriteReg(hnrf, NRF_REG_RF_SETUP,
                NRF_RF_SETUP_RF_DR_LOW | NRF_RF_SETUP_RF_PWR_3);

    /* Pipe 0: auto-ACK, fixed payload */
    NRF_WriteReg(hnrf, NRF_REG_EN_AA, 0x01);
    NRF_WriteReg(hnrf, NRF_REG_EN_RXADDR, 0x01);
    NRF_WriteReg(hnrf, NRF_REG_RX_PW_P0, NRF_PAYLOAD_SIZE);

    /* Auto-retransmit: 1500 µs delay, 15 retries */
    NRF_WriteReg(hnrf, NRF_REG_SETUP_RETR, 0x5F);

    /* Address configuration (role-dependent) */
	#if defined(NRF_ROLE_TX)
		/* 
		 * TX: set TX_ADDR and RX_ADDR_P0 to same address.
		 * RX_ADDR_P0 must match TX_ADDR for auto-ACK to work —
		 * the ACK comes back on the TX address.
		 */
		NRF_WriteRegMulti(hnrf, NRF_REG_TX_ADDR, addr, NRF_ADDR_WIDTH);
		NRF_WriteRegMulti(hnrf, NRF_REG_RX_ADDR_P0, addr, NRF_ADDR_WIDTH);
	#else
		/*
		 * RX: set RX_ADDR_P0 to listen on this address.
		 */
		NRF_WriteRegMulti(hnrf, NRF_REG_RX_ADDR_P0, addr, NRF_ADDR_WIDTH);
	#endif

    /* Clear FIFOs and IRQ flags */
    NRF_FlushRX(hnrf);
    NRF_FlushTX(hnrf);
    NRF_ClearIRQ(hnrf, NRF_STATUS_RX_DR | NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT);

    /* CONFIG register (role-dependent) */
    uint8_t config = NRF_CONFIG_EN_CRC | NRF_CONFIG_CRCO | NRF_CONFIG_PWR_UP;

	#if defined(NRF_ROLE_RX)
		/*
		 * RX mode: unmask RX_DR interrupt, mask TX_DS and MAX_RT
		 */
		config |= NRF_CONFIG_PRIM_RX;
		config |= NRF_CONFIG_MASK_TX_DS | NRF_CONFIG_MASK_MAX_RT;
	#else
		/*
		 * TX mode: unmask TX_DS and MAX_RT interrupts, mask RX_DR
		 */
		config |= NRF_CONFIG_MASK_RX_DR;
		/* PRIM_RX = 0 → TX mode */
	#endif

    NRF_WriteReg(hnrf, NRF_REG_CONFIG, config);
    HAL_Delay(2);												/* Oscillator startup */

	#if defined(NRF_ROLE_RX)
		ce_high();  											/* RX: start listening immediately */
	#endif														/* TX: CE stays low — pulsed per-packet in NRF_Transmit() */

    return NRF_OK;
}
/*==========================================================================================================*/

/*==========================================================================================================*/
/*=============== RX-ONLY FUNCTIONS ========================================================================*/
/*==========================================================================================================*/
#if defined(NRF_ROLE_RX)

bool NRF_DataAvailable(NRF_Handle *hnrf){
    uint8_t status = NRF_GetStatus(hnrf);
    if (status & NRF_STATUS_RX_DR)
        return true;
    return (status & NRF_STATUS_RX_P_NO_MASK) != NRF_STATUS_RX_FIFO_EMPTY;
}

NRF_Status NRF_ReadPayload(NRF_Handle *hnrf, uint8_t *buf){
    if (!NRF_DataAvailable(hnrf))
        return NRF_ERR_RX_EMPTY;

    uint8_t cmd = NRF_CMD_R_RX_PAYLOAD;
    csn_low();
    HAL_SPI_Transmit(hnrf->hspi, &cmd, 1, NRF_SPI_TIMEOUT_MS);
    HAL_SPI_Receive(hnrf->hspi, buf, NRF_PAYLOAD_SIZE, NRF_SPI_TIMEOUT_MS);
    csn_high();

    NRF_ClearIRQ(hnrf, NRF_STATUS_RX_DR);
    return NRF_OK;
}

#endif /* NRF_ROLE_RX */
/*==========================================================================================================*/

/*==========================================================================================================*/
/*=============== TX-ONLY FUNCTIONS ========================================================================*/
/*==========================================================================================================*/
#if defined(NRF_ROLE_TX)

/* Write payload to the TX FIFO (does not transmit yet) */
static NRF_Status nrf_write_tx_fifo(NRF_Handle *hnrf, const uint8_t *data){
    /* Check if TX FIFO is full */
    uint8_t fifo_st = NRF_ReadReg(hnrf, NRF_REG_FIFO_STATUS);
    if (fifo_st & NRF_FIFO_TX_FULL)
        return NRF_ERR_TX_FIFO_FULL;

    uint8_t cmd = NRF_CMD_W_TX_PAYLOAD;
    csn_low();
    HAL_SPI_Transmit(hnrf->hspi, &cmd, 1, NRF_SPI_TIMEOUT_MS);
    HAL_SPI_Transmit(hnrf->hspi, (uint8_t *)data, NRF_PAYLOAD_SIZE, NRF_SPI_TIMEOUT_MS);
    csn_high();

    return NRF_OK;
}

NRF_Status NRF_Transmit(NRF_Handle *hnrf, const uint8_t *data){
    /* Load payload into TX FIFO */
    NRF_Status ret = nrf_write_tx_fifo(hnrf, data);
    if (ret != NRF_OK)
        return ret;

    /* Pulse CE high for >10 µs to trigger transmission. */
    ce_high();
    for (volatile int i = 0; i < 100; i++){}  /* ~10-20 µs delay */
    ce_low();

    /* Poll for completion: TX_DS or MAX_RT.
     * With 250 kbps, 32-byte packet + preamble + address + CRC ≈ 1.3 ms.
     * With 15 retries × 1.5 ms spacing = worst case ~24 ms.
     * Timeout at 75 ms gives 3× headroom (was 50 ms / 2×). */

    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 75){
        uint8_t status = NRF_GetStatus(hnrf);

        if (status & NRF_STATUS_TX_DS){
            NRF_ClearIRQ(hnrf, NRF_STATUS_TX_DS);
            return NRF_OK;
        }

        if (status & NRF_STATUS_MAX_RT){
            NRF_ClearIRQ(hnrf, NRF_STATUS_MAX_RT);
            NRF_FlushTX(hnrf);  /* Discard the failed packet */
            return NRF_ERR_TX_MAX_RT;
        }
    }

    /* Timeout — shouldn't happen with correct config */
    NRF_FlushTX(hnrf);
    return NRF_ERR_TX_TIMEOUT;
}

NRF_Status NRF_TransmitNoBlock(NRF_Handle *hnrf, const uint8_t *data){
    NRF_Status ret = nrf_write_tx_fifo(hnrf, data);
    if (ret != NRF_OK)
        return ret;

    /* Pulse CE to start transmission */
    ce_high();
    for (volatile int i = 0; i < 100; i++){}
    ce_low();

    return NRF_OK;
}

NRF_Status NRF_TransmitPoll(NRF_Handle *hnrf){
    uint8_t status = NRF_GetStatus(hnrf);

    if (status & NRF_STATUS_TX_DS){
        NRF_ClearIRQ(hnrf, NRF_STATUS_TX_DS);
        return NRF_OK;
    }
    if (status & NRF_STATUS_MAX_RT){
        NRF_ClearIRQ(hnrf, NRF_STATUS_MAX_RT);
        NRF_FlushTX(hnrf);
        return NRF_ERR_TX_MAX_RT;
    }

    return NRF_ERR_SPI; 										/* Still in progress */
}

void NRF_GetTxStats(NRF_Handle *hnrf, uint8_t *lost_pkts, uint8_t *retx_pkts){
    uint8_t observe = NRF_ReadReg(hnrf, NRF_REG_OBSERVE_TX);
    *lost_pkts = (observe >> 4) & 0x0F;   						/* PLOS_CNT: 0-15 */
    *retx_pkts =  observe       & 0x0F;   						/* ARC_CNT:  0-15 */
}

#endif /* NRF_ROLE_TX */
/*==========================================================================================================*/

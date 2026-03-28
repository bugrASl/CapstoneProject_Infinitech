/*
 * @file    bsau_adc.c
 * @brief   BSAU ADC/DMA acquisition module — implementation.
 * @author  bugrASl
 */
 
#include "bsau_adc.h"
#include "log.h"
#include "adc.h"
#include "tim.h"
 
/*============== STATE =====================================================================================*/
 
volatile uint16_t   g_adc_dma_buf[ADC_DMA_BUF_SIZE];
volatile uint16_t   g_adc_snapshot[ADC_DMA_BUF_SIZE];
volatile uint8_t    g_pkt_ready                         =   0;
volatile uint32_t   g_adc_dropped                       =   0;
volatile uint32_t   g_adc_error_code                    =   0;
/*==========================================================================================================*/

/*============== BSAU_ADC_Init ===========================================================================*/

void BSAU_ADC_Init(void)
{
    LOG("ADC", "BSAU_ADC_Init", "RUN", "Calibrating ADC1...");

    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK)
    {
        LOG("ADC", "BSAU_ADC_Init", "FAIL", "Calibration failed");
        Error_Handler();
    }
    LOG("ADC", "BSAU_ADC_Init", "OK", "Calibration complete");

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_dma_buf, ADC_DMA_BUF_SIZE) != HAL_OK)
    {
        LOG("ADC", "BSAU_ADC_Init", "FAIL", "DMA start failed");
        Error_Handler();
    }

    if (HAL_TIM_Base_Start(&htim6) != HAL_OK)
    {
        LOG("ADC", "BSAU_ADC_Init", "FAIL", "TIM6 start failed");
        Error_Handler();
    }

    LOG("ADC", "BSAU_ADC_Init", "OK", "Pipeline running (TIM6 trig, DMA circ)");
}
/*==========================================================================================================*/

/*============== BSAU_ADC_GetBattery =====================================================================*/

uint16_t BSAU_ADC_GetBattery(void)
{
    uint32_t sum                                =   0;

    for (int s = 0; s < ADC_DMA_SAMPLES; s++)
    {
        sum                                    +=   g_adc_snapshot[s * ADC_DMA_CHANNELS + ADC_BATT_INDEX];
    }

    return (uint16_t)(sum / ADC_DMA_SAMPLES);
}
/*==========================================================================================================*/

/*============== HAL CALLBACKS ===========================================================================*/
/*
 * These run in DMA ISR context (priority 0). Do NOT call LOG() here —
 * HAL_UART_Transmit uses HAL_GetTick() which won't increment if SysTick
 * has equal or lower priority → deadlock.
 */

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        if (g_pkt_ready)
        {
            g_adc_dropped++;
            return;
        }

        for (int i = 0; i < ADC_DMA_BUF_SIZE; i++)
        {
            g_adc_snapshot[i]                   =   g_adc_dma_buf[i];
        }

        g_pkt_ready                             =   1;
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        /* Store for deferred logging from main loop — never call LOG from ISR */
        g_adc_error_code                        =   hadc->ErrorCode;
    }
}
/*==========================================================================================================*/

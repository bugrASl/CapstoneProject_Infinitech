/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SHM_SIZE 0x10000
#define M4_LOOP_RATE_MS 1
#define SHM_BUFFER_SIZE 256
#define SERVO_NEUTRAL 1500
#define IPC_TIMEOUT_MS 500
#define RADIO_SPI_TIMEOUT 10
#define SERVO_PWM_FREQ 50
#define RADIO_PAYLOAD_LEN 32
#define SHM_START_ADDR 0x38000000
#define IPC_HSEM_BOOT_SYNC 2
#define SERVO_ABS_MIN_PULSE 500
#define SERVO_ABS_MAX_PULSE 2500
#define E_STOP_Pin GPIO_PIN_13
#define E_STOP_GPIO_Port GPIOC
#define NRF_CSN_Pin GPIO_PIN_14
#define NRF_CSN_GPIO_Port GPIOD
#define NRF_CE_Pin GPIO_PIN_15
#define NRF_CE_GPIO_Port GPIOD
#define NRF_IRQ_Pin GPIO_PIN_6
#define NRF_IRQ_GPIO_Port GPIOG

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

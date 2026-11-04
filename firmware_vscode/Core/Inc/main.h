/* USER CODE BEGIN Header */
/** 
  * @file           : main.h
  * @brief          : Header for main.c file.
  */
/* USER CODE END Header */

// Define to prevent recursive inclusion
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

// Includes
#include "stm32f4xx_hal.h"

// Private includes 
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

// Exported types 
/* USER CODE BEGIN ET */

/* USER CODE END ET */

// Exported constants
/* USER CODE BEGIN EC */

/* USER CODE END EC */

// Exported macro 
/* USER CODE BEGIN EM */

/* USER CODE END EM */

// Exported functions prototypes
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

// Private defines 
#define CS_Pin GPIO_PIN_2
#define CS_GPIO_Port GPIOA
#define RSE_Pin GPIO_PIN_3
#define RSE_GPIO_Port GPIOA
#define RS_Pin GPIO_PIN_4
#define RS_GPIO_Port GPIOA
#define SCK_Pin GPIO_PIN_5
#define SCK_GPIO_Port GPIOA
#define BUTTON_DOWN_Pin GPIO_PIN_6
#define BUTTON_DOWN_GPIO_Port GPIOA
#define MOSI_Pin GPIO_PIN_7
#define MOSI_GPIO_Port GPIOA
#define BUTTON_MENU_Pin GPIO_PIN_0
#define BUTTON_MENU_GPIO_Port GPIOB
#define BUTTON_ENTER_Pin GPIO_PIN_1
#define BUTTON_ENTER_GPIO_Port GPIOB
#define BUTTON_UP_Pin GPIO_PIN_10
#define BUTTON_UP_GPIO_Port GPIOB
#define SCL_Pin GPIO_PIN_8
#define SCL_GPIO_Port GPIOA
#define SDA_Pin GPIO_PIN_4
#define SDA_GPIO_Port GPIOB
#define ALERT_Pin GPIO_PIN_7
#define ALERT_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

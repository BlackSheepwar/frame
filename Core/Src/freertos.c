/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "svc_key_logic.h"
#include "bsp_iic.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
typedef StaticQueue_t osStaticMessageQDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for KEY_T */
osThreadId_t KEY_THandle;
uint32_t KEY_TBuffer[ 128 ];
osStaticThreadDef_t KEY_TControlBlock;
const osThreadAttr_t KEY_T_attributes = {
  .name = "KEY_T",
  .cb_mem = &KEY_TControlBlock,
  .cb_size = sizeof(KEY_TControlBlock),
  .stack_mem = &KEY_TBuffer[0],
  .stack_size = sizeof(KEY_TBuffer),
  .priority = (osPriority_t) osPriorityRealtime,
};
/* Definitions for KEY_CMD_T */
osThreadId_t KEY_CMD_THandle;
uint32_t KEY_CMD_TBuffer[ 128 ];
osStaticThreadDef_t KEY_CMD_TControlBlock;
const osThreadAttr_t KEY_CMD_T_attributes = {
  .name = "KEY_CMD_T",
  .cb_mem = &KEY_CMD_TControlBlock,
  .cb_size = sizeof(KEY_CMD_TControlBlock),
  .stack_mem = &KEY_CMD_TBuffer[0],
  .stack_size = sizeof(KEY_CMD_TBuffer),
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for OLED_T */
osThreadId_t OLED_THandle;
uint32_t OLED_TBuffer[ 256 ];
osStaticThreadDef_t OLED_TControlBlock;
const osThreadAttr_t OLED_T_attributes = {
  .name = "OLED_T",
  .cb_mem = &OLED_TControlBlock,
  .cb_size = sizeof(OLED_TControlBlock),
  .stack_mem = &OLED_TBuffer[0],
  .stack_size = sizeof(OLED_TBuffer),
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for KEY_EVT_Q */
osMessageQueueId_t KEY_EVT_QHandle;
uint8_t KEY_EVT_QBuffer[ 8 * sizeof( key_event_msg_t ) ];
osStaticMessageQDef_t KEY_EVT_QControlBlock;
const osMessageQueueAttr_t KEY_EVT_Q_attributes = {
  .name = "KEY_EVT_Q",
  .cb_mem = &KEY_EVT_QControlBlock,
  .cb_size = sizeof(KEY_EVT_QControlBlock),
  .mq_mem = &KEY_EVT_QBuffer,
  .mq_size = sizeof(KEY_EVT_QBuffer)
};
/* Definitions for IIC_LOCK */
osMutexId_t IIC_LOCKHandle;
const osMutexAttr_t IIC_LOCK_attributes = {
  .name = "IIC_LOCK"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void Task_Key_Scan(void *argument);
void Task_Key_Cmd(void *argument);
void Task_OLED(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of IIC_LOCK */
  IIC_LOCKHandle = osMutexNew(&IIC_LOCK_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* 初始化 */
  BSP_IIC_Init();
  Svc_Key_Logic_Init();
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of KEY_EVT_Q */
  KEY_EVT_QHandle = osMessageQueueNew (8, sizeof(key_event_msg_t), &KEY_EVT_Q_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of KEY_T */
  KEY_THandle = osThreadNew(Task_Key_Scan, NULL, &KEY_T_attributes);

  /* creation of KEY_CMD_T */
  KEY_CMD_THandle = osThreadNew(Task_Key_Cmd, NULL, &KEY_CMD_T_attributes);

  /* creation of OLED_T */
  OLED_THandle = osThreadNew(Task_OLED, NULL, &OLED_T_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
__weak void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_Task_Key_Scan */
/**
* @brief Function implementing the KEY_T thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Task_Key_Scan */
__weak void Task_Key_Scan(void *argument)
{
  /* USER CODE BEGIN Task_Key_Scan */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Task_Key_Scan */
}

/* USER CODE BEGIN Header_Task_Key_Cmd */
/**
* @brief Function implementing the KEY_CMD_T thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Task_Key_Cmd */
__weak void Task_Key_Cmd(void *argument)
{
  /* USER CODE BEGIN Task_Key_Cmd */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Task_Key_Cmd */
}

/* USER CODE BEGIN Header_Task_OLED */
/**
* @brief Function implementing the OLED_T thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Task_OLED */
__weak void Task_OLED(void *argument)
{
  /* USER CODE BEGIN Task_OLED */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Task_OLED */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */


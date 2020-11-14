/*
 * tof.cpp
 *
 *  Created on: 11.11.2020 г.
 *      Author: Denislav Trifonov
 */

#include "tof.h"

static I2C_HandleTypeDef hi2c1;
static I2C_STATUS g_eI2cStatus                = I2C_STATUS_NOT_INIT;
static PCB_USED   g_eUsedPCB                  = XNUCLEO_53L3A2;
static TOF_MEASURING_MODE g_eTofMeasuringMode = TOF_MEASURING_MODE_INTERRUPT;

static TOF_STATE g_eToFSensorState[SENSORS_SUPPORTED] = {STATE_NOT_INIT};

static uint8_t g_arrToFSensorInterruptStatus[SENSORS_SUPPORTED];

static uint8_t g_nVL53L3CX_Address[SENSORS_SUPPORTED] = {0x52};

static GPIO_InitTypeDef g_arrToFGPIOs[SENSORS_SUPPORTED] = {
		{GPIO_PIN_3, GPIO_MODE_IT_FALLING, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, 0}};

static GPIO_TypeDef* g_arrToFPorts[SENSORS_SUPPORTED] =
{GPIOC};

static IRQn_Type g_arrToFGPIOInterruptSources[SENSORS_SUPPORTED] =
{EXTI3_IRQn};

static uint8_t g_arrToFGPIOInterruptPriority[SENSORS_SUPPORTED] =
{0};

static VL53LX_Dev_t g_ToFSensorDriverData[SENSORS_SUPPORTED];
static VL53LX_MultiRangingData_t g_ToFSensorMeasurementData[SENSORS_SUPPORTED];

/* Private data  ---------------------------------------------------------*/


/* Private function prototypes -----------------------------------------------*/
static void I2C_Init(void);
static void GPIO_Init(TOF_SUPPORTED_SENSORS eSensor);

/* Public function definitions  -----------------------------------------------*/

// @brief initialize selected ToF sensor
/* ======================================================*/
void ToF_Init(TOF_SUPPORTED_SENSORS eSensor)
/* ======================================================*/
{
	uint8_t nDummyByte  = 0;

	g_eToFSensorState[eSensor] = STATE_INIT_IN_PROCESS;

	// Initialize the VL53L3CX GPIO pin
	GPIO_Init(eSensor);

	/* Initialize I2C communication.
	 * This function is going to be executed only once
	 * because each VL53L3CX sensor is connected to the MCU
	 * via same I2C interface.
	 */
	if (g_eI2cStatus == I2C_STATUS_NOT_INIT)
	{
		g_eI2cStatus = I2C_STATUS_INIT;
		I2C_Init();
	}

	g_ToFSensorDriverData[eSensor].I2cHandle  = &hi2c1;
	g_ToFSensorDriverData[eSensor].I2cDevAddr = g_nVL53L3CX_Address[eSensor];

	/* If XNUCLEO board is used, XSHUT pin of the VL53L3CX sensor is not
	 * directly connected to GPIO of the MCU. Instead of this, it is connected
	 * to STMPE1600 GPIO expander. This expander is also connected to the MCU via I2C.
	 * In order to enable/disable ToF sensor, the MCU has to send corresponding I2C command
	 */
	if (g_eUsedPCB == XNUCLEO_53L3A2)
	{
		uint8_t nInitStatus = 0;
		XNUCLEO53L3A2_Init();
		nInitStatus = XNUCLEO53L3A2_ResetId(XNUCLEO_SENSOR_CENTER, 0);
		HAL_Delay(2);
		nInitStatus = XNUCLEO53L3A2_ResetId(XNUCLEO_SENSOR_CENTER, 1);
		HAL_Delay(2);

		if (nInitStatus)
		{
			LEDs_SetLEDState(RED_LED, LED_ON);
		}
	}

	// Check the I2C communication with VL53L3CX
	VL53LX_RdByte(&g_ToFSensorDriverData[eSensor], 0x010F, &nDummyByte);
	if (nDummyByte != 0xEA)
	{
		LEDs_SetLEDState(RED_LED, LED_ON);
	}

	VL53LX_RdByte(&g_ToFSensorDriverData[eSensor], 0x0110, &nDummyByte);

	if (nDummyByte != 0xAA)
	{
		LEDs_SetLEDState(RED_LED, LED_ON);
	}

	if (VL53LX_WaitDeviceBooted(&g_ToFSensorDriverData[eSensor]))
	{
		LEDs_SetLEDState(RED_LED, LED_ON);
	}

	if (VL53LX_DataInit(&g_ToFSensorDriverData[eSensor]))
	{
		LEDs_SetLEDState(RED_LED, LED_ON);
	}

	if (VL53LX_PerformXTalkCalibration(&g_ToFSensorDriverData[eSensor]))
	{
		LEDs_SetLEDState(RED_LED, LED_ON);
	}

	if (VL53LX_SetDistanceMode(&g_ToFSensorDriverData[eSensor], VL53LX_DISTANCEMODE_SHORT))
	{
		LEDs_SetLEDState(RED_LED, LED_ON);
	}

	if (VL53LX_StartMeasurement(&g_ToFSensorDriverData[eSensor]))
	{
		LEDs_SetLEDState(RED_LED, LED_ON);
	}
}

/* ======================================================*/
TOF_STATUS ToF_Measure(TOF_SUPPORTED_SENSORS eSensor)
/* ======================================================*/
{
	TOF_STATUS eStatus = TOF_STATUS_OK;

	uint8_t NewDataReady = 0;
	uint8_t status       = 0;

	if (g_eToFSensorState[eSensor] == STATE_INIT_IN_PROCESS)
	{
		while (!g_arrToFSensorInterruptStatus[eSensor])
		{}

		g_arrToFSensorInterruptStatus[eSensor] = 0;

		status = VL53LX_GetMultiRangingData(&g_ToFSensorDriverData[eSensor], &g_ToFSensorMeasurementData[eSensor]);

		if (!status)
		{
			g_eToFSensorState[eSensor] = STATE_IDLE;
		}
		else
		{
			g_eToFSensorState[eSensor] = STATE_ERROR;
			eStatus 				   = TOF_STATUS_ERROR;
		}
	}

	if (g_eToFSensorState[eSensor] == STATE_IDLE)
	{
		if (g_eTofMeasuringMode == TOF_MEASURING_MODE_INTERRUPT)
		{
			if (!VL53LX_ClearInterruptAndStartMeasurement(&g_ToFSensorDriverData[eSensor]))
			{
				g_eToFSensorState[eSensor] = STATE_MEASURING;

				while (!g_arrToFSensorInterruptStatus[eSensor])
				{}

				g_arrToFSensorInterruptStatus[eSensor] = 0;
				status = VL53LX_GetMultiRangingData(&g_ToFSensorDriverData[eSensor], &g_ToFSensorMeasurementData[eSensor]);

				if (!status)
				{
					g_eToFSensorState[eSensor] = STATE_IDLE;
				}
				else
				{
					eStatus 				   = TOF_STATUS_ERROR;
					g_eToFSensorState[eSensor] = STATE_ERROR;
				}
			}
			else
			{
				eStatus                    = TOF_STATUS_ERROR;
				g_eToFSensorState[eSensor] = STATE_ERROR;
			}
		}

		// TODO: Pooling mode is still not implemented!
		else
		{
			while (!NewDataReady)
			{
				status = VL53LX_GetMeasurementDataReady(&g_ToFSensorDriverData[eSensor], &NewDataReady);
				HAL_Delay(2);

				if ((!status) && (NewDataReady != 0))
				{
					status = VL53LX_GetMultiRangingData(&g_ToFSensorDriverData[eSensor], &g_ToFSensorMeasurementData[eSensor]);

					if (status == 0)
					{
						status = VL53LX_ClearInterruptAndStartMeasurement(&g_ToFSensorDriverData[eSensor]);
					}
					else
					{
						eStatus = TOF_STATUS_ERROR;
					}
				}
			}
		}
	}
	else
	{
		eStatus = TOF_STATUS_ERROR;
	}

	return eStatus;
}

/* ======================================================*/
VL53LX_MultiRangingData_t* ToF_GetDistance_mm(TOF_SUPPORTED_SENSORS eSensor)
/* ======================================================*/
{
	VL53LX_MultiRangingData_t* pData = NULL;

	if (g_eToFSensorState[eSensor] == STATE_IDLE)
	{
		pData = &g_ToFSensorMeasurementData[eSensor];
	}

	return pData;
}

/* Private function definitions  -----------------------------------------------*/

/**
 * @brief I2C1 Initialization Function
 * @param None
 * @retval None
 */
/* ======================================================*/
static void I2C_Init(void)
/* ======================================================*/
{
	hi2c1.Instance = I2C1;
	hi2c1.Init.Timing = 0x2050133E;
	hi2c1.Init.OwnAddress1 = 0;
	hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	hi2c1.Init.OwnAddress2 = 0;
	hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
	hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

	if (HAL_I2C_Init(&hi2c1) != HAL_OK)
	{
		LEDs_SetLEDState(RED_LED, LED_ON);
	}
	/** Configure Analogue filter
	 */
	if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
	{
		LEDs_SetLEDState(RED_LED, LED_ON);
	}

	if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
	{
		LEDs_SetLEDState(RED_LED, LED_ON);
	}
}

/* ======================================================*/
void GPIO_Init(TOF_SUPPORTED_SENSORS eSensor)
/* ======================================================*/
{
	HAL_GPIO_Init(g_arrToFPorts[eSensor], &g_arrToFGPIOs[eSensor]);

	// Configure EXT interrupt
	HAL_NVIC_SetPriority(g_arrToFGPIOInterruptSources[eSensor], g_arrToFGPIOInterruptPriority[eSensor], 0);
	HAL_NVIC_EnableIRQ(g_arrToFGPIOInterruptSources[eSensor]);
}

/* Interrupt callback definitions  -----------------------------------------------*/

// @brief VL53L3CX GPIO callback
/* ======================================================*/
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
/* ======================================================*/
{
	if (GPIO_Pin==ToF_IT_Pin)
	{
		g_arrToFSensorInterruptStatus[TOF_CENTRAL] ++;
	}
}

/**
 * @brief This function handles I2C1 event interrupt / I2C1 wake-up interrupt through EXTI line 23.
 */
/* ======================================================*/
void I2C1_EV_IRQHandler(void)
/* ======================================================*/
{
	/* USER CODE BEGIN I2C1_EV_IRQn 0 */

	/* USER CODE END I2C1_EV_IRQn 0 */
	HAL_I2C_EV_IRQHandler(&hi2c1);
	/* USER CODE BEGIN I2C1_EV_IRQn 1 */

	/* USER CODE END I2C1_EV_IRQn 1 */
}

/**
 * @brief This function handles I2C1 error interrupt.
 */
/* ======================================================*/
void I2C1_ER_IRQHandler(void)
/* ======================================================*/
{
	/* USER CODE BEGIN I2C1_ER_IRQn 0 */

	/* USER CODE END I2C1_ER_IRQn 0 */
	HAL_I2C_ER_IRQHandler(&hi2c1);
	/* USER CODE BEGIN I2C1_ER_IRQn 1 */

	/* USER CODE END I2C1_ER_IRQn 1 */
}

/* ======================================================*/
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
/* ======================================================*/
{
	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
}
/* ======================================================*/

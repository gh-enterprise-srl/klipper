/*
 * crc.c
 *
 *  Created on: 23 ago 2017
 *      Author: stefano
 */

#include "stm32h7xx_hal_crc.h"
#include "stm32h7xx_hal_rcc.h"

CRC_HandleTypeDef   CrcHandle;


/****************************************
 *
 * CRC Calculation Block
 *
 * ******************************************/

void HAL_CRC_MspInit(CRC_HandleTypeDef *hcrc){

	UNUSED(hcrc);
   /* CRC Peripheral clock enable */
  __HAL_RCC_CRC_CLK_ENABLE();
}

/**
  * @brief CRC MSP De-Initialization
  *        This function freeze the hardware resources used in this example:
  *          - Disable the Peripheral's clock
  * @param hcrc: CRC handle pointer
  * @retval None
  */
void HAL_CRC_MspDeInit(CRC_HandleTypeDef *hcrc)
{
	UNUSED(hcrc);
  /* Enable CRC reset state */
  __HAL_RCC_CRC_FORCE_RESET();

  /* Release CRC from reset state */
  __HAL_RCC_CRC_RELEASE_RESET();
}




void CRC_Reset( void) {
	__HAL_CRC_DR_RESET(&CrcHandle);
}


uint32_t SD_CRC_Calc( uint8_t * buf, uint32_t len ){

	uint32_t	i;
	uint32_t	j;
	uint32_t	k;
	uint32_t  	tmp32 = 0;
	uint32_t	res32 = 0;
	uint32_t	tot_len = len;

	if ( len % 4 ){
		tot_len = len + 4 - ( len % 4 );
	}

	for ( i = 0;  i < tot_len; i++ ){
		j = i & 0x00000003;
		k = j * 8;
		if ( i < len ){
			if ( j == 0 ){
				tmp32 = buf[i];
			} else if ( j == 3) {
				tmp32 |= buf[i] << 24;
				tmp32 = __RBIT(tmp32);
//				if ( i == 3 ){
//					res32 = HAL_CRC_Calculate( &CrcHandle, &tmp32, 1 );
//				} else {
					res32 = HAL_CRC_Accumulate( &CrcHandle, &tmp32, 1 );
//				}
			} else {
				tmp32 |= buf[i] << k;
			}
		} else {
			if ( j == 3) {
				tmp32 = __RBIT(tmp32);
//				if ( i == 3 ){
//					res32 = HAL_CRC_Calculate( &CrcHandle, &tmp32, 1 );
//				} else {
					res32 = HAL_CRC_Accumulate( &CrcHandle, &tmp32, 1 );
//				}
			}
		}
	}
	res32 = __RBIT(res32);
	res32 ^= 0xffffffff;
	return res32;
}




void CRC_Init_New( uint32_t mode ){

	CrcHandle.Instance = CRC;

	HAL_CRC_DeInit( &CrcHandle );

	/*##-1- Configure the CRC peripheral #######################################*/


	/* The default polynomial is used */
	CrcHandle.Init.DefaultPolynomialUse    = DEFAULT_POLYNOMIAL_ENABLE;

	/* The default init value is used */
	CrcHandle.Init.DefaultInitValueUse     = DEFAULT_INIT_VALUE_ENABLE;

//	/* The input data are not inverted */
//	CrcHandle.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_BYTE;

	/* The output data are not inverted */
	CrcHandle.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;

	/* The input data are 32-bit long words */
	CrcHandle.InputDataFormat              = mode;//CRC_INPUTDATA_FORMAT_WORDS;

	if ( mode == CRC_INPUTDATA_FORMAT_BYTES ){
		/* The input data are not inverted */
		CrcHandle.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_BYTE;
	}
	if ( mode == CRC_INPUTDATA_FORMAT_WORDS ){
		/* The input data are not inverted */
		CrcHandle.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_NONE;
	}

	HAL_CRC_Init( &CrcHandle );
	CRC_Reset();
}


#ifndef _CRC_H_
#define _CRC_H_
#include <stdint.h>
#include "stm32h7xx_hal_crc.h"
#include "stm32h7xx_hal_crc_ex.h"
#ifdef __cplusplus
extern "C" {
#endif

void		CRC_Init( uint32_t mode );
void 		CRC_Reset( void);
uint32_t 	SD_CRC_Calc( uint8_t * buf, uint32_t len );
uint32_t 	SD_CRC_Calc_new( uint8_t * buf, uint32_t len );
void CRC_Init_New( uint32_t mode );
#ifdef __cplusplus
}
#endif
#endif

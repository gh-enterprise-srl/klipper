/*
 * fwUpdateGCodeApi.h
 *
 *  Created on: 18 mag 2021
 *      Author: andreac
 */

#ifndef FWUPDATEGCODEAPI_H_
#define FWUPDATEGCODEAPI_H_
#include "fwUpdateCommon.h"
#include "stm32h7xx.h"
#include "core_cm7.h"

typedef enum {
	M997_CK_OK = 0,			/* (0) Succeeded */
	M997_CK_CKS_ERR,			/* (1) Checksum error */
	M997_CK_FOR_ERR,			/* (2) Command format error */
	M997_CK_FLASH1_ERR,		/* (3) Flash programmin error */
	M997_CK_FLASH2_ERR,		/* (3) Flash programmin error */
	M997_CK_FLASH3_ERR,		/* (3) Flash programmin error */
}M997_CHECK_RESULT;


void M997_systemResetStartTimer( void );
M997_CHECK_RESULT M997ProcessFWHexLine(const char *hexLine, uint8_t hexLineLen);
M997_CHECK_RESULT M997ProcessFWCommand(int8_t state, FlagStatus* respondeOk);
void M997HandleChecksumError();
void M997HandleGenericError(const char* err);
void M997_systemResetTimer(void);
bool IAP_FlagWrongFirmwarePacket();

typedef struct{
	int32_t		systemResetTimer;
	int8_t 		state;
	uint8_t		counterErrors;
	FlagStatus	systemResetEnabled;
	FlagStatus	transitional;
} M997_STRUCT;
extern M997_STRUCT M997;

#endif /* FWUPDATEGCODEAPI_H_ */

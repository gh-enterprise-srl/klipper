/*
 * fwUpdaterGCodeApi.cpp
 *
 *  Created on: 18 mag 2021
 *      Author: andreac
 */

#include "fwUpdateGCodeApi.h"
#include "polar_aes.h"

#include "crc.h"
#include "command.h" // DECL_COMMAND
#include "stm32h7xx_hal_iwdg.h"


#define IAP_BUFFER32_DIM	128
#define IAP_BUFFER8_DIM		512
#define IAP_BUFFER8_MASK	511
typedef enum{
	IAP_STATE_INIT,
	IAP_STATE_HEADER_DECODE,
	IAP_STATE_PROGRAMMING,
	IAP_STATE_SUCCESS,
	IAP_STATE_ACQUIRE_OVERRUN,
	IAP_STATE_HEADER_ERROR,
	IAP_STATE_FIRSTWORD_MISMATCH,
	IAP_STATE_WRITE_OVERRUN,
	IAP_STATE_WRITE_ERROR,
	IAP_STATE_CRC32_ERROR,
	IAP_STATE_ERROR,
}IAP_MANAGER_STATE_ENUM;


typedef struct{
	uint8_t					buffer[IAP_BUFFER8_DIM];
	int16_t					head;
	int16_t					tail;
	int16_t					byte_to_read;
	int16_t					byte_free;
	uint8_t 				tmp1[32];
	uint8_t 				tmp2[32];
	uint8_t					iv[16];
	uint32_t 	 			dest_address;
	int32_t 				file_size;
	int32_t 				file_index;
	int32_t					header_len;
	uint32_t 				crc32_acc;
	uint32_t 				crc32_red_crypted;
	uint32_t 				crc32_red_plain;
	int32_t					parser_index;
	int16_t					byte_expected;
#ifdef SD_READER_PRESENT
	FIL 					file;
#endif
	aes_context 			ctx;
	uint8_t					jump;
	IAP_MANAGER_STATE_ENUM 	state;
}IAP_MANAGE_STRUCT;


const uint8_t AES256_key[32] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

const uint8_t ID_CODE[4] = { 0x01, 0x80, 0x16, 0x1a };
static bool flagWrongFirmwarePacket = false;
static IAP_MANAGE_STRUCT IAP;




M997_STRUCT M997 = { .systemResetTimer = 0, .state = -1, .counterErrors = 0,
		.systemResetEnabled = RESET, .transitional = RESET};



static void IAP_Reset_ReadBuffer(void) {

	uint32_t *p32 = (uint32_t*) IAP.buffer;

	for (uint16_t i = 0; i < IAP_BUFFER32_DIM; i++) {
		p32[i] = 0;
	}
}

static void IAP_AES_Decrypt_BufferReset(void) {

	IAP_Reset_ReadBuffer();
	IAP.head = 0;
	IAP.tail = 0;
	IAP.byte_free = IAP_BUFFER8_DIM;
	IAP.byte_to_read = 0;
}

//static bool IAP_AES_Decrypt_BufferVoid(void) {
//
//	if ((IAP.head == IAP.tail) && (IAP.byte_free == IAP_BUFFER8_DIM) && (IAP.byte_to_read == 0)) {
//		return true;
//	}
//	return false;
//}
//
//static bool IAP_AES_Decrypt_BufferFull(void) {
//
//	if ((IAP.head == IAP.tail) && (IAP.byte_free == 0) && (IAP.byte_to_read == IAP_BUFFER8_DIM)) {
//		return true;
//	}
//	return false;
//}

static bool IAP_AES_Decrypt_MoveTail(int16_t len) {

	if (len > IAP.byte_to_read) {
		return false;
	}
	IAP.tail += len;
	IAP.tail &= IAP_BUFFER8_MASK;
	IAP.byte_to_read -= len;
	IAP.byte_free += len;
	return true;
}

static bool IAP_AES_Decrypt_MoveHead(int16_t len) {

	if (len > IAP.byte_free) {
		return false;
	}
	IAP.head += len;
	IAP.head &= IAP_BUFFER8_MASK;
	IAP.byte_to_read += len;
	IAP.byte_free -= len;
	return true;
}

static bool IAP_AES_Decrypt_AddData(uint8_t *inbuf, int16_t len) {

	int16_t index = IAP.head;
	int16_t i;

	if (len > IAP.byte_free) {
		return false;
	}
	for (i = 0; i < len; i++) {
		IAP.buffer[index] = inbuf[i];
		index++;
		index &= IAP_BUFFER8_MASK;
	}
	IAP.file_index += len;
	if (IAP_AES_Decrypt_MoveHead(len) == false) {
		return false;
	}
	return true;
}
static bool IAP_AES_Decrypt_GetByte(uint8_t *out_p) {

	if (IAP.byte_to_read <= 0) {
		return false;
	}
	*out_p = IAP.buffer[IAP.tail];
	if (IAP_AES_Decrypt_MoveTail(1) == false) {
		return false;
	}
	return true;
}




static bool IAP_AES_Decrypt_InitUSB(void) {

	CRC_Init_New( CRC_INPUTDATA_FORMAT_WORDS);
	IAP_AES_Decrypt_BufferReset();
	IAP.file_size = 128 * 1024 * 4;
	IAP.file_index = 0;
	IAP.dest_address = ADDR_FLASH_SECTOR_0_BANK2;
	IAP.parser_index = 0;
	IAP.state = IAP_STATE_INIT;
	IAP.byte_expected = IAP_BUFFER8_DIM;
	if (IAP_MassEraseBank2() == false) {
		return false;
	}
	//HAL_Delay(6000);
	return true;
}

bool IAP_AES_Decrypt_Check(void) {

	__IO uint32_t *last_addr;
	uint32_t decrypted_fw_len;

	if (M997.transitional == RESET) {
		decrypted_fw_len = IAP.file_index - IAP.header_len - 4;
	}
	else {
		decrypted_fw_len = IAP.file_index - IAP.header_len - 4 - 131072;
	}
	last_addr = (__IO uint32_t*) (ADDR_FLASH_SECTOR_0_BANK2 + decrypted_fw_len);
	IAP.crc32_red_plain = __REV(last_addr[0]);
	if (M997.transitional == RESET) {
		CRC_Init_New( CRC_INPUTDATA_FORMAT_WORDS);
	}
	IAP.crc32_acc = SD_CRC_Calc((uint8_t*) ADDR_FLASH_SECTOR_0_BANK2, decrypted_fw_len);
	if (IAP.crc32_acc != IAP.crc32_red_plain) {
		IAP.state = IAP_STATE_CRC32_ERROR;
		return false;
	}
	IAP.state = IAP_STATE_SUCCESS;
	return true;
}


static bool IAP_AES_Decrypt_Execute(void) {

	uint8_t *p;
	uint16_t i;
	uint16_t j;
	uint8_t byte;

	while (IAP.parser_index <= IAP.file_index) {
		switch (IAP.state) {
		case IAP_STATE_INIT:
			if (IAP.byte_to_read < IAP.byte_expected) {
				return true;
			}
			IAP.state = IAP_STATE_HEADER_DECODE;
			flagWrongFirmwarePacket = false;
			break;
		case IAP_STATE_HEADER_DECODE:

			// Salvo il valore di CRC32 calcolato su stream criptato + chiave dipendente da nome file
			p = (uint8_t*) &IAP.crc32_red_crypted;
			for (i = 0; i < 4; i++) {
				IAP_AES_Decrypt_GetByte(&byte);
				p[i] = byte;
			}
			// if (HAL_GetREVID() >= REV_ID_V) {
				if (( p[0] != ID_CODE[0] ) ||
						( p[1] != ID_CODE[1] ) ||
						( p[2] != ID_CODE[2] ) ||
						( p[3] != ID_CODE[3] ) ) {
					/// ID not valid, return false.
					flagWrongFirmwarePacket = true;
					return false;
				}
			// }

			// calcolo lunghezza rnd1
			if (IAP_AES_Decrypt_GetByte(&byte) == false) {
				return false;
			}
			IAP.jump = (byte & 0x3c) + 4;
			// skip rnd1
			for (i = 1; i < IAP.jump; i++) {
				IAP_AES_Decrypt_GetByte(&byte);
			}

			// Salvo IV
			for (i = 0; i < 16; i++) {
				IAP_AES_Decrypt_GetByte(&IAP.iv[i]);
			}
			IAP.parser_index = 4 + IAP.jump + 16;

			// calcolo lunghezza rnd2
			IAP_AES_Decrypt_GetByte(&byte);
			IAP.jump = (byte & 0x3c) + 4;
			IAP.parser_index += IAP.jump;
			// skip rnd2
			for (i = 1; i < IAP.jump; i++) {
				IAP_AES_Decrypt_GetByte(&byte);
			}

			// decripto usando AES256 + Key + IV la sezione che
			// tiene la lunghezza del Random code block
			for (i = 0; i < 16; i++) {
				IAP_AES_Decrypt_GetByte(&IAP.tmp1[i]);
			}
			IAP.parser_index += 16;
			aes_setkey_dec(&IAP.ctx, AES256_key, 256);
			aes_crypt_cbc(&IAP.ctx, AES_DECRYPT, 16, IAP.iv, IAP.tmp1, IAP.tmp2);
			// Estraggo la lunghezza del Random code block criptata
			IAP.jump = IAP.tmp2[0];

			// Skip random code block
			if ((IAP.jump >= 4) && (IAP.jump < 8)) {
				for (j = 0; j < IAP.jump - 1; j++) {
					for (i = 0; i < 16; i++) {
						IAP_AES_Decrypt_GetByte(&IAP.tmp1[i]);
					}
					IAP.parser_index += 16;
					aes_crypt_cbc(&IAP.ctx, AES_DECRYPT, 16, IAP.iv, IAP.tmp1, IAP.tmp2);
				}

				// Inizio a processoro
				IAP.state = IAP_STATE_PROGRAMMING;
				IAP.byte_expected = 32;
				IAP.header_len = IAP.parser_index;
				break;
			}
			else {
				IAP.state = IAP_STATE_HEADER_ERROR;
				return false;
			}
			break;
		case IAP_STATE_PROGRAMMING:
			if (IAP.byte_to_read < IAP.byte_expected) {
//				IAP.byte_expected -= IAP.byte_to_read;
				return true;
			}
			if (IAP.byte_to_read >= 16) {
				if (IAP.byte_to_read > 32) {
					j = 32;
				}
				else {
					j = IAP.byte_to_read;
				}
				for (i = 0; i < j; i++) {
					if (IAP_AES_Decrypt_GetByte(&IAP.tmp1[i]) == false) {
						return false;
					}
				}
			}
			IAP.parser_index += 32;
			aes_crypt_cbc(&IAP.ctx, AES_DECRYPT, 32, IAP.iv, IAP.tmp1, IAP.tmp2);
			if (M997.transitional == SET) {
				if ((IAP.parser_index - 32 - IAP.header_len) < 131072) {
					SD_CRC_Calc(IAP.tmp2, 32);
					return true;
				}
			}
			if (IAP.dest_address == ADDR_FLASH_SECTOR_0_BANK2) {
				uint32_t *tmp_p = (uint32_t*) IAP.tmp2;
				uint32_t data32 = *tmp_p;
				if (data32 != 0x20020000) {
					IAP.state = IAP_STATE_FIRSTWORD_MISMATCH;
					return false;
				}
			}
			if (IAP.dest_address >= MAX_BANK2_ADDR) {
				IAP.state = IAP_STATE_WRITE_OVERRUN;
				IAP.byte_expected = 0;
				return false;
			}
			if (ProgramFlashWord(ADDR_FLASH_SECTOR_0_BANK2,&IAP.dest_address,(uint32_t) IAP.tmp2) == false) {
				IAP.state = IAP_STATE_WRITE_ERROR;
				IAP.byte_expected = 0;
				return false;
			}
			if (IAP.file_index >= (IAP.file_size)) {
				IAP.byte_expected = 0;
				IAP.parser_index = IAP.file_size;
				return IAP_AES_Decrypt_Check();
			}
			IAP.byte_expected = 32;
			break;
		case IAP_STATE_SUCCESS:
			return true;
		default:
			break;
		}
	}
	return true;
}

static bool IAP_AES_Decrypt_EndUSB(void) {

	bool ret;

	IAP.file_size = IAP.file_index;
	IAP.byte_expected = IAP.byte_to_read;
	ret = IAP_AES_Decrypt_Execute();
	if (ret == false)
		return false;
	if (IAP.byte_expected != 0) {
		ret = IAP_AES_Decrypt_Execute();
		if (ret == false)
			return false;
	}
	return true;
}


void M997_systemResetStartTimer(void) {

	M997.systemResetTimer = 0;
	M997.systemResetEnabled = SET;
}


void command_fw_update(uint32_t *args)
{
	uint8_t state = args[0];
	uint8_t data_len = args[1];
	uint8_t *data = command_decode_ptr(args[2]);
	int8_t oldState = M997.state;
	M997_CHECK_RESULT result = M997_CK_OK;

	M997.state = state;
	if (M997.state == 0)
	{ // Start programming S=0. Data parameter ignored
		M997.systemResetEnabled = RESET;
		M997.transitional = RESET;
		IAP_AES_Decrypt_InitUSB();
	}
	else if ((oldState == 0 || oldState == 1) && M997.state == 1)
	{ // Receiving firmware line
		const char* pAfterColumn = (const char*) &data[1];
		data_len-=2; // remove ':' and '\n'
		result = M997ProcessFWHexLine(pAfterColumn, data_len);
		if (result == M997_CK_OK)
		{
			M997.counterErrors = 0;
		}
	}
	else if ((oldState == 1) && (M997.state == 2))
	{ // Close programming
		if (IAP_AES_Decrypt_EndUSB() == true)
		{
			M997.state = -1;
			M997_systemResetStartTimer();
		}
		else
		{
			IAP_EraseSector(FLASH_SECTOR_0, FLASH_BANK_2);
			result = M997_CK_FLASH3_ERR;
		}
	}
	else
	{
		result = M997_CK_FOR_ERR;
	}

	sendf("fw_update_response err=%c", result);

}
DECL_COMMAND(command_fw_update, "fw_update state=%c data=%*s");


static int16_t ConvertASCII2Number(uint8_t c){
	int16_t in;
	int16_t out = 0;

	in = ( int16_t )c;
	if (( in > 0x002F ) && ( in < 0x003A )) {
		out = in - 0x0030;
	} else if (( in > 0x0040 ) && ( in < 0x0047 )) {
		out = in - 0x0037;
	} else if (( in > 0x0060 ) && ( in < 0x0067 )){
		out = in - 0x0057;
	} else {
		return(-1);
	}
	out &= 0x00FF;
	return ((uint8_t)out);
}

static void ConvertStr2Num(const uint8_t * in, uint8_t * out, uint32_t n){
	uint32_t i;
	uint32_t j;

	for ( i = 0; i < n ; i++){
		j = 2 * i;
		out[i] = ConvertASCII2Number( in[j] )*16;
		out[i] += ( ConvertASCII2Number( in[j + 1] ));
	}
}

M997_CHECK_RESULT M997ProcessFWHexLine(const char *hexLine, uint8_t hexLineLen) {
	uint8_t M997_data[16];
	uint16_t intelhex_addr_num;
	uint8_t payloadHexValue;
	uint8_t i = 1;
	uint8_t j = 0;
	uint8_t payloadChecksum = 0x00;
	uint8_t intelhex_byte_num = 0x00;
	uint8_t intelhex_record_type = 0xff;
	if(hexLineLen < 38){
		i++;
	}
	//sendf("HEX LINE=%*s L=%u",hexLineLen,hexLine,hexLineLen);
	for (i = 0; i < hexLineLen; i++) {
		ConvertStr2Num((const uint8_t*)&hexLine[i], &payloadHexValue, 1);
		//sendf("PAYLOAD LINE=%u",payloadHexValue);
		if (i == 0) {
			intelhex_byte_num = payloadHexValue;
			payloadChecksum = 0;
		}
		else if (i == 2) {
			intelhex_addr_num = payloadHexValue;
			intelhex_addr_num <<= 8;
		}
		else if (i == 4) {
			intelhex_addr_num += payloadHexValue;
		}
		else if (i == 6) {
			intelhex_record_type = payloadHexValue;
		}
		else if (j < intelhex_byte_num) {
			if (intelhex_record_type == 0) {
				M997_data[j++] = payloadHexValue;
			}
			if (intelhex_record_type == 4) {
				M997_data[j++] = payloadHexValue;
			}
		}
		payloadChecksum += payloadHexValue;
		i++;
	}

	if ((payloadChecksum) == 0x00) {
		if (intelhex_record_type == 0x00) {
			// La linea del file Hex non e' corrotta
			// salvo i dati binari decodificati nei buffer interni
			// dedicati alla decodifica del firmware
			if (IAP_AES_Decrypt_AddData(M997_data, intelhex_byte_num) == true) {
				// Eseguo la macchina a stati dell'aggiornamento firmware
				if (IAP_AES_Decrypt_Execute() == true) {
					return M997_CK_OK;
				}
				else {
					return M997_CK_FLASH1_ERR;
				}
			}
			return M997_CK_FLASH2_ERR;
		}
		else if (intelhex_record_type == 0x04) {
			if (M997_data[0] == 0x12) {
				if (M997_data[1] == 0x34) {
					M997.transitional = SET;
				} /*else if ( M997_data[1] == 0x36 ){
				 M997.transitional = RESET;
				 }*/
			}
		}
		return M997_CK_OK;
	}
	else {
		return M997_CK_CKS_ERR;
	}

}

void M997HandleChecksumError(void) {
	M997.counterErrors++;
	if (M997.counterErrors >= 10) {
		M997.state = -1;
		M997.counterErrors = 0;
	}
}
void M997HandleGenericError(const char* err)
{
	M997.state = -1;
}

void M997_systemResetTimer(void) {
	IWDG_HandleTypeDef IwdgHandle;
	IwdgHandle.Instance = IWDG1;
	IwdgHandle.Init.Prescaler = IWDG_PRESCALER_32;
	IwdgHandle.Init.Reload = 1000; //ms
	IwdgHandle.Init.Window = 1000;
	HAL_IWDG_Init(&IwdgHandle);
}

// bool IAP_FlagWrongFirmwarePacket()
// {
// 	return flagWrongFirmwarePacket;
// }

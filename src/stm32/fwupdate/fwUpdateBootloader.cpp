/*
 * fwUpdateBootloader.cpp
 *
 *  Created on: 18 mag 2021
 *      Author: andreac
 */
#include "speedy_hal.h"
#include "fwUpdateCommon.h"

#define MAX_BANK			6
#define MAX_IMAGE_DIM		(MAX_BANK*128*1024)


static bool IAP_IsResetFrom_WWDG(void) {
	/*##-1- Check if the system has resumed from IWDG reset ####################*/
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDG1RST ) != RESET) {
		__HAL_RCC_CLEAR_RESET_FLAGS();
		return true;
	}
	return false;
}

static bool IAP_IsResetFrom_IWDG(void) {
	/*##-1- Check if the system has resumed from IWDG reset ####################*/
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST) != RESET) {
		__HAL_RCC_CLEAR_RESET_FLAGS();
		return true;
	}
	return false;
}


static void IAP_Boot_ApplicationImage(uint32_t addr_pos) {

	// Typedef
	typedef void (*pFunction)(void);
	// Function Pointer
	pFunction Jump_To_Application;
	// Jump Address
	uint32_t JumpAddress;

	__HAL_RCC_CLEAR_RESET_FLAGS();
	/* Jump to user application */
	JumpAddress = *(__IO uint32_t*) (addr_pos + 4);
	Jump_To_Application = (pFunction) JumpAddress;
	/* Initialize user application's Stack Pointer */
	__DMB();
	SCB->VTOR = addr_pos;
	__DMB();
	__DSB();
	__set_MSP(*(__IO uint32_t*) addr_pos);
	__DMB();
	__DSB();
	SCB_InvalidateICache();
	Jump_To_Application();
	__DSB();
	while (1) {
		__NOP();
	}
}
static uint32_t FLASH_Read(uint32_t addr, uint8_t *p, uint32_t len) {

	__IO uint8_t *pin = (__IO uint8_t*) addr;

	for (uint16_t i = 0; i < len; i++) {
		p[i] = pin[i];
	}
	return len;
}


static bool IAP_FW_IsPresent(uint32_t addr) {

	uint32_t tmp32;

	FLASH_Read(addr, (uint8_t*) &tmp32, 4);
	if (tmp32 != 0xFFFFFFFF) {
		return true;
	}
	else {
		return false;
	}
}



static bool IAP_CopyFW(uint32_t from, uint32_t to, uint32_t dim) {

	uint32_t flash_word_index;
	uint32_t word_index;
	uint32_t sector;
	__IO uint32_t *from_p;
	__IO uint32_t *to_p;
	uint8_t i;
	uint8_t bank;
	uint8_t tmp2[32];


	if (dim > MAX_IMAGE_DIM) {
		return false;
	}
	if (to & 0x0001FFFF) {  //sector aligned check
		return false;
	}
	if (to >= MAX_BANK2_ADDR /*ADDR_FLASH_SECTOR_5_BANK2*/) {
		return false;
	}
	sector = IAP_GetSector(to);
	if (to >= ADDR_FLASH_SECTOR_0_BANK2) {
		bank = FLASH_BANK_2;
	}
	else {
		bank = FLASH_BANK_1;
	}

	for (i = 0; i < MAX_BANK; i++) {
		if (IAP_EraseSector(sector, bank) == false) {
			return false;
		}
		sector++;
	}


	from_p = (__IO uint32_t*) from;
	uint32_t dest_address = to;
	to_p = (uint32_t*) tmp2;
	for (i = 0; i < MAX_BANK; i++) {
		for (flash_word_index = 0; flash_word_index < 4096; flash_word_index++) {
			for (word_index = 0; word_index < 8; word_index++) {
				to_p[word_index] = from_p[word_index];
			}
			if (ProgramFlashWord(to,dest_address,(uint32_t)tmp2) == false) {
				return false;
			}
			from_p += 8;
		}
	}
	CRC_Init_New( CRC_INPUTDATA_FORMAT_WORDS);
	uint32_t crc32_acc = SD_CRC_Calc((uint8_t*) from, MAX_IMAGE_DIM);
	CRC_Init_New( CRC_INPUTDATA_FORMAT_WORDS);
	uint32_t crc32_red_plain = SD_CRC_Calc((uint8_t*) to, MAX_IMAGE_DIM);
	if (crc32_acc != crc32_red_plain) {
		return false;
	}


	return true;
}



void boot(void) {

	// Se c'e' stato un hard fault causato da una scrittura  della flash sul banco 2
	// rimuovo aggiornamento e continuo boot da partizione 1
	if (IAP_IsResetFrom_WWDG()) {
		if (IAP_MassEraseBank2() == false) {
			while (1) {
				__NOP();
			}
		}
		IAP_Boot_ApplicationImage( ADDR_FLASH_SECTOR_1_BANK1);
	}

	// Se e' avvenuta la copia correttamente copio banco 2 su  banco 1
	if (IAP_IsResetFrom_IWDG()) {
		if (IAP_FW_IsPresent( ADDR_FLASH_SECTOR_0_BANK2)) {
			if (IAP_CopyFW( ADDR_FLASH_SECTOR_0_BANK2, ADDR_FLASH_SECTOR_1_BANK1, MAX_IMAGE_DIM)
					== false) {
				while (1) {
					__NOP();
				}
			}
		}
	}

	// Boot Standard..
	if (IAP_FW_IsPresent(ADDR_FLASH_SECTOR_1_BANK1)) {
		IAP_Boot_ApplicationImage( ADDR_FLASH_SECTOR_1_BANK1);
	}
	while (1) {
		__NOP();
	}
}

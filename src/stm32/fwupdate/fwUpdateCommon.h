/*
 * fwUpdate.h
 *
 *  Created on: 18 mag 2021
 *      Author: andreac
 */

#ifndef FWUPDATECOMMON_H_
#define FWUPDATECOMMON_H_
#include "SectorMap.h"
#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx_hal_flash.h"
#include "stm32h7xx_hal_flash_ex.h"

#define FLASH_USER_START_ADDR   	ADDR_FLASH_SECTOR_0_BANK1      /* Start @ of user Flash area Bank1 */
#define FLASH_USER_END_ADDR     	(ADDR_FLASH_SECTOR_7_BANK2 - 1)  /* End @ of user Flash area Bank2*/
#define MAX_BANK2_ADDR		ADDR_FLASH_SECTOR_6_BANK2


uint32_t IAP_GetSector(uint32_t Address);
bool IAP_EraseSector(uint8_t sector, uint8_t bank);
bool IAP_MassEraseBank2();
bool ProgramFlashWord(uint32_t min_address, uint32_t* dest_address, uint32_t data_address);

#endif /* FWUPDATECOMMON_H_ */

/*
 * fwUpdateCommon.cpp
 *
 *  Created on: 18 mag 2021
 *      Author: andreac
 */
#include "fwUpdateCommon.h"

uint32_t IAP_GetSector(uint32_t Address) {

	uint32_t sector = 0;

	if (((Address < ADDR_FLASH_SECTOR_1_BANK1) && (Address >= ADDR_FLASH_SECTOR_0_BANK1))
			|| ((Address < ADDR_FLASH_SECTOR_1_BANK2) && (Address >= ADDR_FLASH_SECTOR_0_BANK2))) {
		sector = FLASH_SECTOR_0;
	}
	else if (((Address < ADDR_FLASH_SECTOR_2_BANK1) && (Address >= ADDR_FLASH_SECTOR_1_BANK1))
			|| ((Address < ADDR_FLASH_SECTOR_2_BANK2) && (Address >= ADDR_FLASH_SECTOR_1_BANK2))) {
		sector = FLASH_SECTOR_1;
	}
	else if (((Address < ADDR_FLASH_SECTOR_3_BANK1) && (Address >= ADDR_FLASH_SECTOR_2_BANK1))
			|| ((Address < ADDR_FLASH_SECTOR_3_BANK2) && (Address >= ADDR_FLASH_SECTOR_2_BANK2))) {
		sector = FLASH_SECTOR_2;
	}
	else if (((Address < ADDR_FLASH_SECTOR_4_BANK1) && (Address >= ADDR_FLASH_SECTOR_3_BANK1))
			|| ((Address < ADDR_FLASH_SECTOR_4_BANK2) && (Address >= ADDR_FLASH_SECTOR_3_BANK2))) {
		sector = FLASH_SECTOR_3;
	}
	else if (((Address < ADDR_FLASH_SECTOR_5_BANK1) && (Address >= ADDR_FLASH_SECTOR_4_BANK1))
			|| ((Address < ADDR_FLASH_SECTOR_5_BANK2) && (Address >= ADDR_FLASH_SECTOR_4_BANK2))) {
		sector = FLASH_SECTOR_4;
	}
	else if (((Address < ADDR_FLASH_SECTOR_6_BANK1) && (Address >= ADDR_FLASH_SECTOR_5_BANK1))
			|| ((Address < ADDR_FLASH_SECTOR_6_BANK2) && (Address >= ADDR_FLASH_SECTOR_5_BANK2))) {
		sector = FLASH_SECTOR_5;
	}
	else if (((Address < ADDR_FLASH_SECTOR_7_BANK1) && (Address >= ADDR_FLASH_SECTOR_6_BANK1))
			|| ((Address < ADDR_FLASH_SECTOR_7_BANK2) && (Address >= ADDR_FLASH_SECTOR_6_BANK2))) {
		sector = FLASH_SECTOR_6;
	}
	else if (((Address < ADDR_FLASH_SECTOR_0_BANK2) && (Address >= ADDR_FLASH_SECTOR_7_BANK1))
			|| ((Address < FLASH_USER_END_ADDR) && (Address >= ADDR_FLASH_SECTOR_7_BANK2))) {
		sector = FLASH_SECTOR_7;
	}
	else {
		sector = FLASH_SECTOR_7;
	}

	return sector;
}



bool IAP_EraseSector(uint8_t sector, uint8_t bank) {

	FLASH_EraseInitTypeDef EraseInitStruct;
	uint32_t SectorError = 0;

	/* Fill EraseInit structure*/
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
	EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
	EraseInitStruct.Banks = bank;
	EraseInitStruct.Sector = sector;
	EraseInitStruct.NbSectors = 1;

	__disable_irq();
	if (bank == FLASH_BANK_1) {
		HAL_FLASHEx_Unlock_Bank1();
	}
	else {
		HAL_FLASHEx_Unlock_Bank2();
	}
	if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK) {
		return false;
	}
	__enable_irq();
	HAL_FLASHEx_Lock_Bank1();
	HAL_FLASHEx_Lock_Bank2();
	return true;
}


bool IAP_MassEraseBank2() {

	FLASH_EraseInitTypeDef EraseInitStruct;
	uint32_t SectorError = 0;

	/* Fill EraseInit structure*/
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_MASSERASE;
	EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
	EraseInitStruct.Banks = FLASH_BANK_2;

//	HAL_FLASH_Unlock();
	__disable_irq();
	HAL_FLASHEx_Unlock_Bank2();
	if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK) {
		return false;
	}
	__enable_irq();
	HAL_FLASHEx_Lock_Bank2();
	return true;
}






bool ProgramFlashWord(uint32_t min_address, uint32_t* dest_address, uint32_t data_address) {

	HAL_StatusTypeDef ret = HAL_ERROR;
	int8_t retry = 8;
	int32_t timeout = 100000;

	if (*dest_address >= ADDR_FLASH_SECTOR_OUT) {
		return false;
	}
	if (*dest_address < min_address) {
		return false;
	}
	__disable_irq();
	if (*dest_address < ADDR_FLASH_SECTOR_0_BANK2) {
		HAL_FLASHEx_Unlock_Bank1();
		HAL_FLASHEx_Lock_Bank2();
	}
	else {
		HAL_FLASHEx_Unlock_Bank2();
		HAL_FLASHEx_Lock_Bank1();
	}
	while (ret != HAL_OK) {
		// Programmazione di 32 Byte
		ret = HAL_FLASH_Program( FLASH_TYPEPROGRAM_FLASHWORD, *dest_address,data_address);
		if (ret != HAL_OK) {
			//HAL_Delay(3);
			timeout = 10000;
			while (timeout > 0) {
				timeout--;
			}
			retry--;
			if (retry < 0) {
				HAL_FLASHEx_Lock_Bank1();
				HAL_FLASHEx_Lock_Bank2();
				__enable_irq();
				return false;
			}
		}

	}
	HAL_FLASHEx_Lock_Bank1();
	HAL_FLASHEx_Lock_Bank2();
	__enable_irq();
	*dest_address += 32;
	return true;
}




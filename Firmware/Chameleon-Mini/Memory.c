/*
 * Flash.c
 *
 *  Created on: 20.03.2013
 *      Author: skuser
 */

#include "Memory.h"
#include "Configuration.h"
#include "Common.h"
#include "Settings.h"
#include "LED.h"

#define FLASH_CMD_STATUS_REG_READ		0xD7
#define FLASH_CMD_MEM_TO_BUF1			0x53
#define FLASH_CMD_MEM_TO_BUF2			0x55
#define FLASH_CMD_BUF1_WRITE			0x84
#define FLASH_CMD_BUF2_WRITE			0x87
#define FLASH_CMD_BUF1_TO_MEM_ERASE		0x83
#define FLASH_CMD_BUF2_TO_MEM_ERASE		0x86
#define FLASH_CMD_PAGE_ERASE			0x81

#define FLASH_STATUS_REG_READY_BIT		(1<<7)
#define FLASH_STATUS_REG_COMP_BIT		(1<<6)
#define FLASH_STATUS_REG_PROTECT_BIT	(1<<1)
#define FLASH_STATUS_REG_PAGESIZE_BIT	(1<<0)

INLINE uint8_t SPITransferByte(uint8_t Data)
{
	MEMORY_FLASH_USART.DATA = Data;

	while (!(MEMORY_FLASH_USART.STATUS & USART_TXCIF_bm));

	MEMORY_FLASH_USART.STATUS = USART_TXCIF_bm;

	return MEMORY_FLASH_USART.DATA;
}

INLINE void SPIReadBlock(void* Buffer, uint16_t ByteCount)
{
	uint8_t* ByteBuffer = (uint8_t*) Buffer;

	while(ByteCount-- > 0) {
		MEMORY_FLASH_USART.DATA = 0;
		while (!(MEMORY_FLASH_USART.STATUS & USART_TXCIF_bm));
		MEMORY_FLASH_USART.STATUS = USART_TXCIF_bm;
		*ByteBuffer++ = MEMORY_FLASH_USART.DATA;
	}
}

INLINE void SPIWriteBlock(const void* Buffer, uint16_t ByteCount)
{
	uint8_t* ByteBuffer = (uint8_t*) Buffer;

	while(ByteCount-- > 0) {
		MEMORY_FLASH_USART.DATA = *ByteBuffer++;
		while (!(MEMORY_FLASH_USART.STATUS & USART_TXCIF_bm));
		MEMORY_FLASH_USART.STATUS = USART_TXCIF_bm;
		MEMORY_FLASH_USART.DATA; /* Flush Buffer */
	}
}

INLINE uint8_t FlashReadStatusRegister(void)
{
	uint8_t Register;

	MEMORY_FLASH_PORT.OUTCLR = MEMORY_FLASH_CS;
	SPITransferByte(FLASH_CMD_STATUS_REG_READ);
	Register = SPITransferByte(0);
	MEMORY_FLASH_PORT.OUTSET = MEMORY_FLASH_CS;

	return Register;
}

INLINE bool FlashIsBusy(void)
{
	return !(FlashReadStatusRegister() & FLASH_STATUS_REG_READY_BIT);
}

INLINE void FlashConfigurePageSize(void)
{
	uint8_t Sequence[] = {0x3D, 0x2A, 0x80, 0xA6};

	while(FlashIsBusy());

	MEMORY_FLASH_PORT.OUTCLR = MEMORY_FLASH_CS;
	SPIWriteBlock(Sequence, sizeof(Sequence));
	MEMORY_FLASH_PORT.OUTSET = MEMORY_FLASH_CS;
}

INLINE void FlashMemoryToBuffer(uint16_t PageAddress)
{
	while(FlashIsBusy());

	MEMORY_FLASH_PORT.OUTCLR = MEMORY_FLASH_CS;
	SPITransferByte(FLASH_CMD_MEM_TO_BUF1);
	SPITransferByte( (PageAddress >> 8) & 0xFF );
	SPITransferByte( (PageAddress >> 0) & 0xFF );
	SPITransferByte( 0 );
	MEMORY_FLASH_PORT.OUTSET = MEMORY_FLASH_CS;
}

INLINE void FlashWriteBuffer(const void* Buffer, uint8_t Address, uint16_t ByteCount)
{
	while(FlashIsBusy());

	MEMORY_FLASH_PORT.OUTCLR = MEMORY_FLASH_CS;
	SPITransferByte(FLASH_CMD_BUF1_WRITE);
	SPITransferByte( 0 );
	SPITransferByte( 0 );
	SPITransferByte( Address );
	SPIWriteBlock(Buffer, ByteCount);
	MEMORY_FLASH_PORT.OUTSET = MEMORY_FLASH_CS;
}

INLINE void FlashBufferToMemory(uint16_t PageAddress)
{
	while(FlashIsBusy());

	MEMORY_FLASH_PORT.OUTCLR = MEMORY_FLASH_CS;
	SPITransferByte(FLASH_CMD_BUF1_TO_MEM_ERASE);
	SPITransferByte( (PageAddress >> 8) & 0xFF );
	SPITransferByte( (PageAddress >> 0) & 0xFF );
	SPITransferByte( 0 );
	MEMORY_FLASH_PORT.OUTSET = MEMORY_FLASH_CS;
}

INLINE void FlashRead(void* Buffer, uint32_t Address, uint16_t ByteCount)
{
	while(FlashIsBusy());

	MEMORY_FLASH_PORT.OUTCLR = MEMORY_FLASH_CS;
	SPITransferByte(0x03);
	SPITransferByte( (Address >> 16) & 0xFF );
	SPITransferByte( (Address >> 8) & 0xFF );
	SPITransferByte( (Address >> 0) & 0xFF );
	SPIReadBlock(Buffer, ByteCount);
	MEMORY_FLASH_PORT.OUTSET = MEMORY_FLASH_CS;
}

INLINE void FlashWrite(const void* Buffer, uint32_t Address, uint16_t ByteCount)
{
	while(ByteCount > 0) {
		uint16_t PageAddress = Address / MEMORY_PAGE_SIZE;
		uint8_t ByteAddress = Address % MEMORY_PAGE_SIZE;
		uint16_t PageBytes = MIN(MEMORY_PAGE_SIZE - ByteAddress, ByteCount);

		FlashMemoryToBuffer(PageAddress);
		FlashWriteBuffer(Buffer, ByteAddress, PageBytes);
		FlashBufferToMemory(PageAddress);

		ByteCount -= PageBytes;
		Address += PageBytes;
	}
}

INLINE void FlashClearPage(uint16_t PageAddress)
{
	while(FlashIsBusy());

	MEMORY_FLASH_PORT.OUTCLR = MEMORY_FLASH_CS;
	SPITransferByte(FLASH_CMD_PAGE_ERASE);
	SPITransferByte( (PageAddress >> 8) & 0xFF );
	SPITransferByte( (PageAddress >> 0) & 0xFF );
	SPITransferByte( 0 );
	MEMORY_FLASH_PORT.OUTSET = MEMORY_FLASH_CS;
}

int MemoryInit(void)
{
	/* Configure MEMORY_FLASH_USART for SPI master mode 0 with maximum clock frequency */
	MEMORY_FLASH_PORT.OUTSET = MEMORY_FLASH_CS;
	
	MEMORY_FLASH_PORT.OUTCLR = MEMORY_FLASH_SCK;
	MEMORY_FLASH_PORT.OUTSET = MEMORY_FLASH_MOSI;
	
	MEMORY_FLASH_PORT.DIRSET = MEMORY_FLASH_SCK | MEMORY_FLASH_MOSI | MEMORY_FLASH_CS;

    MEMORY_FLASH_USART.BAUDCTRLA = 0;
    MEMORY_FLASH_USART.BAUDCTRLB = 0;
	MEMORY_FLASH_USART.CTRLC = USART_CMODE_MSPI_gc;
	MEMORY_FLASH_USART.CTRLB = USART_RXEN_bm | USART_TXEN_bm;


	if ( !(FlashReadStatusRegister() & FLASH_STATUS_REG_PAGESIZE_BIT) ) {
		/* Configure for 256 byte Dataflash if not already done. */
		FlashConfigurePageSize();
		//return 0;
	}
	return 1;
}

void Read_Save(void* Buffer, uint32_t Address, uint16_t ByteCount)
{
	FlashRead(Buffer, Address, ByteCount);
}

void Write_Save(void* Buffer, uint32_t Address, uint16_t ByteCount)
{
	FlashWrite(Buffer, Address, ByteCount);
}

void MemoryReadBlock(void* Buffer, uint16_t Address, uint16_t ByteCount)
{
	uint32_t FlashAddress = (uint32_t) Address + (uint32_t) GlobalSettings.ActiveSetting * MEMORY_SIZE_PER_SETTING;
	FlashRead(Buffer, FlashAddress, ByteCount);
}

void MemoryWriteBlock(const void* Buffer, uint16_t Address, uint16_t ByteCount)
{
	uint32_t FlashAddress = (uint32_t) Address + (uint32_t) GlobalSettings.ActiveSetting * MEMORY_SIZE_PER_SETTING;
	FlashWrite(Buffer, FlashAddress, ByteCount);
}

void MemoryClear(void)
{
	uint32_t PageAddress = ((uint32_t) GlobalSettings.ActiveSetting * MEMORY_SIZE_PER_SETTING) / MEMORY_PAGE_SIZE;
	uint16_t PageCount = MEMORY_SIZE_PER_SETTING / MEMORY_PAGE_SIZE;

	while(PageCount > 0) {
		FlashClearPage(PageAddress);
		PageCount--;
		PageAddress++;
	}
}
void MemoryRecall(void)
{
	/* Recall memory from permanent flash */
	//FlashRead(Memory, (uint32_t) GlobalSettings.ActiveSettingIdx * MEMORY_SIZE_PER_SETTING, MEMORY_SIZE_PER_SETTING);
}

void MemoryStore(void)
{
	/* Store current memory into permanent flash */
	// FlashWrite(Memory, (uint32_t) GlobalSettings.ActiveSettingIdx * MEMORY_SIZE_PER_SETTING, MEMORY_SIZE_PER_SETTING);

	// LEDTrigger(LED_MEMORY_CHANGED, LED_OFF);
	// LEDTrigger(LED_MEMORY_STORED, LED_PULSE);
}

bool MemoryUploadBlock(void* Buffer, uint32_t BlockAddress, uint16_t ByteCount)
{
    if (BlockAddress >= MEMORY_SIZE_PER_SETTING) {
        /* Prevent writing out of bounds by silently ignoring it */
        return true;
    } else {
    	/* Calculate bytes left in memory and start writing */
    	uint32_t BytesLeft = MEMORY_SIZE_PER_SETTING - BlockAddress;
		ByteCount = MIN(ByteCount, BytesLeft);
		MemoryWriteBlock(Buffer, BlockAddress, ByteCount);
		return true;
    }
}

bool MemoryDownloadBlock(void* Buffer, uint32_t BlockAddress, uint16_t ByteCount)
{
    if (BlockAddress >= MEMORY_SIZE_PER_SETTING) {
        /* There are bytes out of bounds to be read. Notify that we are done. */
        return false;
    } else {
    	/* Calculate bytes left in memory and issue reading */
    	uint32_t BytesLeft = MEMORY_SIZE_PER_SETTING - BlockAddress;
		ByteCount = MIN(ByteCount, BytesLeft);
    	MemoryReadBlock(Buffer, BlockAddress, ByteCount);
        return true;
    }
}


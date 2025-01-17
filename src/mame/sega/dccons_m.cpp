// license:LGPL-2.1+
// copyright-holders:Angelo Salese, R. Belmont
/*

    dc.c - Sega Dreamcast hardware

    DC home console hardware overrides (GD-ROM drive etc)

    TODO:
    - Convert to actual G1 I/F;
    - gdrom_alt_status is identical to normal status except that "but it does not clear DMA status information when it is accessed";
    - Verify unimplemented behaviours via tests;

    Old notes, consultation only:
    c230048 - 5 is written, want 6
    c0d9d9e - where bad happens, from routine @ c0da260

    c0d9d8e - R0 on return is the value to put in

    cfffee0 - stack location when bad happens

*/

#include "emu.h"
#include "dccons.h"

#include "cdrom.h"
//#include "debugger.h"
#include "cpu/sh/sh4.h"
#include "sound/aica.h"

// 12x disc drive * 75 Hz = 0,00(1) secs per sector, very optimistic
// Estimate Sega benchmarks:
// - 14.4 MBytes/sec for system/texture/G2 external area,
// - 11.3 for AICA RAM (likely bus contention with audio CPU)
#define ATAPI_SINGLE_XFER_TIME (1111)

#define LOG_WARN    (1U << 1)
#define LOG_XFER    (1U << 2) // log ATAPI transfers

#define VERBOSE (LOG_WARN)
//#define LOG_OUTPUT_STREAM std::cout
#include "logmacro.h"

#define LOGWARN(...)      LOGMASKED(LOG_WARN, __VA_ARGS__)
#define LOGXFER(...)      LOGMASKED(LOG_XFER, __VA_ARGS__)

WRITE_LINE_MEMBER(dc_cons_state::ata_interrupt)
{
	if (state)
		dc_sysctrl_regs[SB_ISTEXT] |= IST_EXT_GDROM;
	else
		dc_sysctrl_regs[SB_ISTEXT] &= ~IST_EXT_GDROM;

	dc_update_interrupt_status();
}

TIMER_CALLBACK_MEMBER(dc_cons_state::atapi_xfer_end )
{
	uint8_t sector_buffer[ 4096 ];

	assert(atapi_xferlen >= 0);

	if (atapi_xferlen == 0)
	{
		LOGXFER("atapi_xfer_end\n");
		atapi_timer->adjust(attotime::never);
		g1bus_regs[SB_GDST] = 0;
		dc_sysctrl_regs[SB_ISTNRM] |= IST_DMA_GDROM;
		dc_update_interrupt_status();
		m_ata->write_dmack(0);
		return;
	}

	m_ata->write_dmack(1);

	//while (atapi_xferlen > 0 )
	{
		struct sh4_ddt_dma ddtdata;

		// get a sector from the SCSI device
		for (int i = 0; i < 2048 / 2; i++)
		{
			int d = m_ata->read_dma();
			sector_buffer[ i*2 ] = d & 0xff;
			sector_buffer[ (i*2)+1 ] = d >> 8;
		}

		atapi_xfercomplete += 2048;

		// perform the DMA
		ddtdata.destination = atapi_xferbase;   // destination address
		ddtdata.length = 2048 / 4;
		ddtdata.size = 4;
		ddtdata.buffer = sector_buffer;
		ddtdata.direction = 1;    // 0 source to buffer, 1 buffer to destination
		ddtdata.channel = 0;
		ddtdata.mode = -1;       // copy from/to buffer
		LOGXFER("G1 I/F ATAPI: DMA one sector to %x, %x remaining\n",
			atapi_xferbase, atapi_xferlen
		);
		m_maincpu->sh4_dma_ddt(&ddtdata);

		atapi_xferlen -= 2048;
		atapi_xferbase += 2048;
	}

	// set the next transfer, or a transfer end event.
	atapi_timer->adjust(attotime::from_usec(ATAPI_SINGLE_XFER_TIME), atapi_xferlen);
}

void dc_cons_state::dreamcast_atapi_init()
{
	atapi_timer = timer_alloc(FUNC(dc_cons_state::atapi_xfer_end), this);
	atapi_timer->adjust(attotime::never);
	save_item(NAME(atapi_xferlen));
	save_item(NAME(atapi_xferbase));
}

/*

 GDROM regsters:

 5f7018: alternate status/device control
 5f7080: data
 5f7084: error/features
 5f7088: interrupt reason/sector count
 5f708c: sector number
 5f7090: byte control low
 5f7094: byte control high
 5f7098: drive select
 5f709c: status/command

c002910 - ATAPI packet writes
c002796 - aux status read after that
c000776 - DMA triggered to c008000

*/

uint32_t dc_cons_state::dc_mess_g1_ctrl_r(offs_t offset)
{
	switch(offset)
	{
		case SB_GDSTARD:
			// TODO: one of the Hello Kitty (identify which) reads there
			logerror("G1CTRL: GDSTARD %08x\n", atapi_xferbase);
			//machine().debug_break();
			return atapi_xferbase;
		case SB_GDST:
			break;
		case SB_GDLEND:
			//machine().debug_break();
			return atapi_xfercomplete;
		case SB_SECUR_EADR:     // always read 0xFF on hardware
			return 0x000000ff;
		case SB_SECUR_STATE:    // state of BIOS checksum security system (R/O):
								// 3 - check passed OK, G1 ATA (5F70xx) registers area accessible
								// 2 - check failed, G1 ATA area blocked (read FFFFFFFFh)
								// 0 - check in progress, BIOS data summed, G1 ATA area blocked (read FFFFFFFFh)
			return 3;
		default:
			LOGWARN("G1CTRL:  Unmapped read %08x\n", 0x5f7400 + offset * 4);
			//machine().debug_break();
			break;
	}
	return g1bus_regs[offset];
}

void dc_cons_state::dc_mess_g1_ctrl_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	g1bus_regs[offset] = data; // 5f7400+reg*4=dat
//  osd_printf_verbose("G1CTRL: [%08x=%x] write %x to %x, mask %x\n", 0x5f7400+reg*4, dat, data, offset, mem_mask);
	switch (offset)
	{
		case SB_GDST:
			if (data & 1 && g1bus_regs[SB_GDEN] == 1) // 0 -> 1
			{
				if (g1bus_regs[SB_GDDIR] == 0)
				{
					// TODO: write to GD-ROM, shouldn't happen unless "special" condition occurs
					// (implies a debug/development device?)
					LOGWARN("%s: G1 I/F illegal direction transfer\n", machine().describe_context());
					return;
				}

				atapi_xferbase = g1bus_regs[SB_GDSTAR];
				atapi_xfercomplete = 0;
				atapi_timer->adjust(attotime::from_usec(ATAPI_SINGLE_XFER_TIME), atapi_xferlen);
//              atapi_regs[ATAPI_REG_SAMTAG] = GDROM_PAUSE_STATE | 0x80;
			}
			break;

		case SB_GDLEN:
			atapi_xferlen = data;
			break;

/*
    The following register is involved in BIOS checksum protection system.
    current understanding of its functioning based on several hardware tests:

    after power on security system is in state 0 (check in progress):
    - access to G1 ATA register area (5F70XX) is blocked, ie GD-ROM in Dreamcast or cartridge/DIMM in arcade systems is not accessible;
    - *any* data readed via G1 data bus (i.e. BIOS) is summed internally by chipset;
    - write to SB_SECUR_EADR register set last address of checksummed area;

    then read address will match SB_SECUR_EADR - calculated summ compared with some hardcoded value
    if values match - security system becomes in state 3 (check OK):
    - G1 ATA registers area unlocked;
    - can be switched back to state 0 by write to SB_SECUR_EADR register, Dreamcast BIOS write 42FEh before jump into Mil-CD executables

    if values doesn't match - security system switch to state 2 (check fail):
    - G1 ATA locked
    - can be switched to state 0 by write to SB_SECUR_EADR register, however passing valid data block through security system set it back to state 2
    - the only exit from this state - power off/on or reset;

    current state can be read from SB_SECUR_STATE register
    actual checksum algorithm is unknown, but its supposed to be simple and weak,
    known few modded BIOSes which succesfully passes this CRC check, because of good luck

    all described above works the same way in all HOLLY/CLX2-based systems - Dreamcast, Naomi 1/2, Atomiswave, SystemSP
*/
		case SB_SECUR_EADR:
			if (data==0 || data==0x001fffff || data==0x42fe)
			{
	//          atapi_regs[ATAPI_REG_SAMTAG] = GDROM_PAUSE_STATE | 0x80;
				logerror("%s: Unlocking GD-ROM %x\n", machine().describe_context(), data);
			}
			break;
	}
}

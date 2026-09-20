// license:BSD-3-Clause
// copyright-holders:
/***************************************************************************

    Sun386i ("Roadrunner")

    Reconstructed from the SunOS 4.0.1/4.0.2 sun386 kernels, since no boot
    PROM dump is known. The physical map comes from the kernel's own
    kvm_init() page-table loads and from its mbcinit[]/mbdinit[] tables.

        0x00000000  RAM
        0xa0000000  board space: bwtwo 0xa0200000, cgthree 0xa0400000,
                    keyboard/mouse SCC 0xa0000020
        0xb0000000  DMA and interrupt block, AT register offsets
        0xc0000000  Weitek 1167
        0xd0000000  Intel 82586
        0xf0000000  slot config, then 0xf2000000, 0xf4000000, 0xf6000000
        0xf8000000  system enable register
        0xf9000000  diagnostic LED
        0xfb000000  WD SCSI host adapter
        0xfc000000  on-board SCC
        0xfd000000  NVRAM and TOD
        0xfe000000  IDPROM
        0xfffe0000  boot PROM

    The kernel is entered in 32-bit protected mode with paging off, loaded at
    physical (virtual - 0xfc000000), with CR3 pointing at a page directory
    that describes the PROM window: locore copies those entries into its own
    directory. The stub PROM supplies that directory and the vector slots the
    kernel reads: +0x10 and +0xb8 memory size, +0xa4 revision, +0xe4 vectors.

***************************************************************************/

#include "emu.h"

#include "cpu/i386/i386.h"
#include "imagedev/floppy.h"
#include "imagedev/snapquik.h"
#include "machine/am9517a.h"
#include "machine/nscsi_bus.h"
#include "machine/pic8259.h"
#include "machine/pit8253.h"
#include "machine/timekpr.h"
#include "machine/upd765.h"
#include "machine/wd33c9x.h"
#include "machine/z80scc.h"
#include "bus/nscsi/hd.h"
#include "bus/rs232/rs232.h"
#include "bus/rs232/pty.h"
#include "bus/sunkbd/sunkbd.h"
#include "bus/sunmouse/sunmouse.h"

#include "screen.h"

#include <fstream>
#include <map>


namespace {

#include "sun386i_prom.hxx"
#include "sun386i_font.hxx"

// the boot parameter block inside the stub prom image
static constexpr uint32_t BOOTPARAM = 0x0c00;
static constexpr uint32_t PROM_BASE = 0xfffe0000;
static constexpr uint32_t OFF_ONE = 0x03a0;   // a byte holding 1

static constexpr uint32_t KERNEL_VIRTUAL_BASE = 0xfc000000;
static constexpr uint32_t ENTRY_SLOT = 0x3f4000;
static constexpr uint32_t PAGING_SLOT = 0x3f4004;
static constexpr uint32_t TABLE_SLOT  = 0x3f4008;

class sun386i_state : public driver_device
{
public:
	sun386i_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_dma(*this, "dma%u", 1U)
		, m_pic(*this, "pic%u", 1U)
		, m_pit(*this, "pit")
		, m_pit3(*this, "pit3")
		, m_scc(*this, "scc")
		, m_kbdscc(*this, "kbdscc")
		, m_fdc(*this, "fdc")
		, m_floppy(*this, "fdc:0")
		, m_scsi(*this, "wds")
		, m_timekpr(*this, "timekpr")
		, m_screen(*this, "screen")
		, m_ttya(*this, "ttya")
		, m_vram(*this, "vram")
		, m_cvram(*this, "cvram")
		, m_cscreen(*this, "cscreen")
		, m_prom(*this, "prom")
		, m_idprom(*this, "idprom")
	{ }

	void sun386i(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	required_device<i386_device> m_maincpu;
	required_device_array<am9517a_device, 2> m_dma;
	required_device_array<pic8259_device, 3> m_pic;
	required_device<pit8254_device> m_pit;
	required_device<pit8254_device> m_pit3;
	required_device<scc8530_device> m_scc;
	required_device<scc8530_device> m_kbdscc;
	required_device<upd765a_device> m_fdc;
	required_device<floppy_connector> m_floppy;
	required_device<wd33c93_device> m_scsi;
	required_device<m48t02_device> m_timekpr;
	required_device<screen_device> m_screen;
	required_device<rs232_port_device> m_ttya;
	required_shared_ptr<uint32_t> m_vram;
	required_shared_ptr<uint32_t> m_cvram;
	required_device<screen_device> m_cscreen;
	required_memory_region m_prom;
	required_memory_region m_idprom;

	void mem_map(address_map &map) ATTR_COLD;
	void io_map(address_map &map) ATTR_COLD;

	uint8_t atblock_r(offs_t offset);
	void atblock_w(offs_t offset, uint8_t data);
	uint8_t idprom_r(offs_t offset);
	uint16_t enable_r();
	void enable_w(offs_t offset, uint16_t data, uint16_t mem_mask);
	void led_w(uint8_t data);
	uint8_t slot_r(offs_t offset);
	void icu_w(int bank, offs_t offset, uint8_t data);
	void icu_int_w(int bank, int state);
	IRQ_CALLBACK_MEMBER(icu_ack);
	void fdc_dor_w(uint8_t data);
	uint8_t fdc_dir_r();
	void fdc_cmd_byte(uint8_t data);
	void dma_hrq_w(int state);
	void dma_eop_w(int state);
	void dma2_eop_w(int state);
	void dma_tc_w(int group, int channel, int state);
	uint8_t dma_read(offs_t offset);
	void dma_write(offs_t offset, uint8_t data);
	uint8_t dma2_read(offs_t offset);
	void dma2_write(offs_t offset, uint8_t data);
	void dma2_hrq_w(int state);
	uint8_t stub_r(offs_t offset);
	void stub_w(offs_t offset, uint8_t data);
	void prom_console_w(uint8_t data);
	void console_glyph(uint8_t c);
	uint32_t screen_update_cg3(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	void cg3_addr_w(uint32_t data);
	void cg3_data_w(uint32_t data);
	uint8_t prom_keyboard_r();
	uint32_t screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);

	DECLARE_QUICKLOAD_LOAD_MEMBER(quickload_kernel);

	emu_timer *m_lines = nullptr;
	TIMER_CALLBACK_MEMBER(hold_lines);
	std::string m_conline;
	std::string m_input;
	std::vector<uint8_t> m_kernel;
	void place_kernel();
	size_t m_inpos = 0;
	int m_empty_polls = 0;
	attotime m_last_key = attotime::zero;
	int m_icw_state[3]{};
	uint8_t m_icu_int = 0;
	uint8_t m_atblock[0x100]{};
	uint16_t m_enable = 0;
	uint8_t m_led = 0;
	uint8_t m_dmaint[2]{};
	uint8_t m_dmaen[2]{};
	uint8_t m_dmafilled[2]{};
	uint32_t m_fddma = 0;
	uint8_t m_fdcmd[9]{};
	unsigned m_fdcmdpos = 0;
	unsigned m_fdcmdlen = 1;
	unsigned m_fdmax = 0;
	unsigned m_fdcount = 0;
	uint32_t m_fdfirst = 0;
	unsigned m_conx = 0;
	unsigned m_cony = 0;
	uint8_t m_cgmap[256][3]{};
	unsigned m_cgidx = 0;
	unsigned m_cgphase = 0;
	uint8_t m_cgctrl = 0;
	unsigned m_fbtype = 2;
	uint32_t m_fdlast = 0xffffffff;
};


// 23 and 22 are esp and ebp in the cpu's state list

uint8_t sun386i_state::atblock_r(offs_t offset)
{
	if (offset < 0x10)
		return m_dma[0]->read(offset);
	if (offset >= 0x20 && offset <= 0x21)
		return m_pic[0]->read(offset & 1);
	if (offset >= 0x30 && offset <= 0x31)
		return m_pic[1]->read(offset & 1);
	if (offset >= 0x40 && offset <= 0x43)
		return m_pit->read(offset & 3);
	if (offset >= 0x44 && offset <= 0x47)
		return m_pit3->read(offset & 3);
	if (offset >= 0xa0 && offset <= 0xa1)
		return m_pic[2]->read(offset & 1);
	if (offset == 0x19)
		return m_dmaint[0];
	if (offset == 0xd9)
		return m_dmaint[1];
	// the second controller is byte addressed like the first, not word
	// addressed as on an AT: the kernel's register table puts channel 7 at
	// 0xc6 and 0xc7, one after the other
	if (offset >= 0xc0 && offset <= 0xcf)
		return m_dma[1]->read(offset - 0xc0);
	return m_atblock[offset];
}

void sun386i_state::atblock_w(offs_t offset, uint8_t data)
{
	// writing an address or a count refills the base registers of a channel
	if (offset < 0x08)
		m_dmafilled[0] |= 1 << (offset >> 1);
	else if (offset >= 0xc0 && offset <= 0xc7)
		m_dmafilled[1] |= 1 << ((offset - 0xc0) >> 1);
	m_atblock[offset] = data;
	if (offset < 0x10)
		m_dma[0]->write(offset, data);
	else if (offset >= 0x20 && offset <= 0x21)
		icu_w(0, offset & 1, data);
	else if (offset >= 0x30 && offset <= 0x31)
		icu_w(1, offset & 1, data);
	else if (offset >= 0x40 && offset <= 0x43)
		m_pit->write(offset & 3, data);
	else if (offset >= 0x44 && offset <= 0x47)
	{
		m_pit3->write(offset & 3, data);
	}
	else if (offset >= 0xa0 && offset <= 0xa1)
		icu_w(2, offset & 1, data);
	else if (offset >= 0xc0 && offset <= 0xcf)
		m_dma[1]->write(offset - 0xc0, data);
	else if (offset == 0x19 || offset == 0xd9)
	{
		// bit 2 arms the channel's chaining interrupt, and dma_setup only
		// arms it when the buffer spans more than one page; without it a
		// transfer that fits in one page must not interrupt at all
		const int group = (offset == 0xd9);
		const int channel = data & 3;
		if (BIT(data, 2))
		{
			m_dmaen[group] |= 1 << channel;
		}
		else
		{
			// the kernel disarms around every page it loads, so a disarm is
			// only the end of the transfer when no further page was written:
			// the base registers are still empty from the last one
			const bool ending = BIT(m_dmaint[group], channel)
					&& !BIT(m_dmafilled[group], channel);
			m_dmaen[group] &= ~(1 << channel);
			m_dmaint[group] &= ~(1 << channel);
			if (!m_dmaint[0] && !m_dmaint[1])
				m_pic[1]->ir1_w(0);
			if (ending && group == 0 && channel == 2)
			{
				m_fdc->tc_w(1);
				m_fdc->tc_w(0);
			}
		}
	}
}

uint8_t sun386i_state::idprom_r(offs_t offset)
{
	return m_idprom->base()[offset & 0x1f];
}

uint16_t sun386i_state::enable_r()
{
	return m_enable;
}

void sun386i_state::enable_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	COMBINE_DATA(&m_enable);
	logerror("enable %04x\n", m_enable);
}

void sun386i_state::led_w(uint8_t data)
{
	if (data != m_led)
		logerror("led %02x\n", data);
	m_led = data;
}

uint8_t sun386i_state::slot_r(offs_t offset)
{
	return 0xff;                    // empty slot
}

// The 82380's interrupt controller is three independent 8259 banks, each with
// a vector register per request rather than one base. The kernel writes those
// vectors first and then a zero as ICW2, so the base is taken from the vector
// register of request 0 of the bank.
void sun386i_state::icu_w(int bank, offs_t offset, uint8_t data)
{
	static const uint16_t vecbase[3] = { 0x28, 0x38, 0xa8 };
	if (offset == 0)
	{
		if (BIT(data, 4))                       // ICW1
			m_icw_state[bank] = 1;
		m_pic[bank]->write(0, data);
		return;
	}

	switch (m_icw_state[bank])
	{
	case 1:                                     // ICW2: the base comes from the
		data = m_atblock[vecbase[bank]];        // per-request vector registers
		m_icw_state[bank] = 2;
		logerror("icu %d: vector base %02x\n", bank, data);
		break;
	case 2:                                     // ICW4: this controller is always
		data |= 0x01;                           // in 8086 mode, where the 8259
		m_icw_state[bank] = 0;                  // has that bit for 8085 mode
		break;
	default:
		break;
	}
	m_pic[bank]->write(1, data);
}

void sun386i_state::icu_int_w(int bank, int state)
{
	if (state)
		m_icu_int |= 1 << bank;
	else
		m_icu_int &= ~(1 << bank);
	m_maincpu->set_input_line(INPUT_LINE_IRQ0, m_icu_int ? ASSERT_LINE : CLEAR_LINE);
}

IRQ_CALLBACK_MEMBER(sun386i_state::icu_ack)
{
	for (int bank = 0; bank < 3; bank++)
	{
		if (BIT(m_icu_int, bank))
		{
			const uint32_t v = m_pic[bank]->acknowledge();
			return v;
		}
	}
	return 0;
}

// how many bytes each command takes, so the read parameters can be read off
static unsigned fdc_cmd_len(uint8_t c)
{
	switch (c & 0x1f)
	{
	case 0x05: case 0x06: case 0x09: case 0x0c: case 0x11: case 0x16: case 0x19:
		return 9;
	case 0x0d:
		return 6;
	case 0x13:
		return 4;
	case 0x03: case 0x0f:
		return 3;
	case 0x04: case 0x07: case 0x0a: case 0x12:
		return 2;
	default:
		return 1;
	}
}

uint8_t sun386i_state::fdc_dir_r()
{
	// The port reads as on a PC: bit 7 set means the diskette was changed.
	// The kernel inverts it only when it decides the line is broken, and it
	// decides that from the NVRAM: with the real 386i/150 contents it reads
	// artwork 3 and leaves change_line_broke at 0, so nothing is inverted
	// here. With a blank NVRAM it reads 255, sets the flag and inverts, and
	// then this port has to be inverted too or the kernel never reads.
	const uint8_t raw = m_fdc->dir_r();
	return raw;
}

void sun386i_state::fdc_dor_w(uint8_t data)
{
	// the controller keeps its own copy: drive select, motors and reset, and
	// it needs it to answer the disk change line
	m_fdc->dor_w(data);
}

void sun386i_state::dma_hrq_w(int state)
{
	m_maincpu->set_input_line(INPUT_LINE_HALT, state ? ASSERT_LINE : CLEAR_LINE);
	m_dma[0]->hack_w(state);
}

// A channel that reaches its terminal count raises interrupt 1, the line the
// kernel claims in dma_init with addintr(1, ...). The handler reads a nibble
// per group, one bit per channel, at 0x19 for channels 0 to 3 and 0xd9 for 4
// to 7, checks the terminal count bit of the 8237 status, reloads the next
// page of the transfer and clears the bit by writing the channel number back.
// Only the floppy (channel 2) and the SCSI (channel 7) are wired here, which
// is all the kernel's own tables assign.
void sun386i_state::dma_tc_w(int group, int channel, int state)
{
	if (state != ASSERT_LINE || !BIT(m_dmaen[group], channel))
		return;
	m_dmaint[group] |= 1 << channel;
	m_dmafilled[group] &= ~(1 << channel);
	m_pic[1]->ir1_w(1);
}

void sun386i_state::dma_eop_w(int state)
{
	// With chaining armed this is the end of a page, not of the transfer, and
	// the real controller does not terminate the device there: the kernel
	// reloads the next page and the read goes on. Passing it through cuts the
	// track short and leaves the rest of the buffer at zero, which bar reads
	// as the end of the archive and calls a success.
	if (!BIT(m_dmaen[0], 2))
		m_fdc->tc_w(state == ASSERT_LINE);
	dma_tc_w(0, 2, state);
}

void sun386i_state::dma2_eop_w(int state)
{
	dma_tc_w(1, 3, state);
}

uint8_t sun386i_state::dma_read(offs_t offset)
{
	// the AT page register for channel 2 supplies the address above 64K
	const uint32_t page = uint32_t(m_atblock[0x81]) << 16;
	return m_maincpu->space(AS_PROGRAM).read_byte(page | offset);
}

void sun386i_state::fdc_cmd_byte(uint8_t data)
{
	if (m_fdcmdpos == 0)
		m_fdcmdlen = fdc_cmd_len(data);
	if (m_fdcmdpos < sizeof(m_fdcmd))
		m_fdcmd[m_fdcmdpos] = data;
	if (++m_fdcmdpos >= m_fdcmdlen)
	{
		m_fdcmdpos = 0;
		m_fdmax = 0;
		m_fdcount = 0;
		// a read can only give what lies between the first sector and the
		// last of the track, and the transfer has to end on that byte: the
		// kernel's own end of count arrives through an interrupt, too late
		if (m_fdcmdlen == 9 && (m_fdcmd[0] & 0x1f) == 0x06 && m_fdcmd[6] >= m_fdcmd[4])
			m_fdmax = unsigned(m_fdcmd[6] - m_fdcmd[4] + 1) << (7 + (m_fdcmd[5] & 7));
	}
}

void sun386i_state::dma_write(offs_t offset, uint8_t data)
{
	const uint32_t page = uint32_t(m_atblock[0x81]) << 16;
	const uint32_t addr = page | offset;
	if (addr != m_fdlast + 1)
	{
		m_fdfirst = addr;
		m_fddma = 0;
	}
	m_fdlast = addr;
	m_fddma++;
	m_maincpu->space(AS_PROGRAM).write_byte(addr, data);
	if (m_fdmax && ++m_fdcount >= m_fdmax)
	{
		m_fdmax = 0;
		m_fdc->tc_w(1);
		m_fdc->tc_w(0);
	}
}

uint8_t sun386i_state::dma2_read(offs_t offset)
{
	const uint32_t page = uint32_t(m_atblock[0x8a]) << 16;
	return m_maincpu->space(AS_PROGRAM).read_byte(page | offset);
}

void sun386i_state::dma2_write(offs_t offset, uint8_t data)
{
	const uint32_t page = uint32_t(m_atblock[0x8a]) << 16;
	m_maincpu->space(AS_PROGRAM).write_byte(page | offset, data);
}

void sun386i_state::dma2_hrq_w(int state)
{
	m_maincpu->set_input_line(INPUT_LINE_HALT, state ? ASSERT_LINE : CLEAR_LINE);
	m_dma[1]->hack_w(state);
}

uint8_t sun386i_state::stub_r(offs_t offset)
{
	return 0xff;
}

void sun386i_state::stub_w(offs_t offset, uint8_t data)
{
}

// The colour board is a cgthree at 0xa0400000 with a Brooktree style palette
// in the onboard page: the index goes to 0xa0000010 and then three bytes of
// red, green and blue to 0xa0000014, which steps the index on by itself. The
// kernel finds the board by reading a byte at 0xf7000000: 0x80 says 1152x900
// and 0x81 says 1024x768.
void sun386i_state::cg3_addr_w(uint32_t data)
{
	m_cgidx = data & 0xff;
	m_cgphase = 0;
}

void sun386i_state::cg3_data_w(uint32_t data)
{
	m_cgmap[m_cgidx][m_cgphase] = data & 0xff;
	if (++m_cgphase >= 3)
	{
		m_cgphase = 0;
		m_cgidx = (m_cgidx + 1) & 0xff;
	}
}

uint32_t sun386i_state::screen_update_cg3(screen_device &screen, bitmap_rgb32 &bitmap,
		const rectangle &cliprect)
{
	uint8_t const *const vram = reinterpret_cast<uint8_t const *>(m_cvram.target());
	for (int y = 0; y < 900; y++)
	{
		uint32_t *scanline = &bitmap.pix(y);
		for (int x = 0; x < 1152; x++)
		{
			const uint8_t p = vram[y * 1152 + x];
			*scanline++ = rgb_t(m_cgmap[p][0], m_cgmap[p][1], m_cgmap[p][2]);
		}
	}
	return 0;
}

uint32_t sun386i_state::screen_update(screen_device &screen, bitmap_rgb32 &bitmap,
		const rectangle &cliprect)
{
	uint8_t const *const vram = reinterpret_cast<uint8_t const *>(m_vram.target());
	for (int y = 0; y < 900; y++)
	{
		uint32_t *scanline = &bitmap.pix(y);
		for (int x = 0; x < 1152 / 8; x++)
		{
			const uint8_t bits = vram[y * (1152 / 8) + x];
			// the window system draws with the leftmost pixel in the low bit
			// and a set bit is ink, which is the other way round from a Sun
			for (int b = 0; b < 8; b++)
				*scanline++ = BIT(bits, b) ? 0x000000 : 0xffffff;
		}
	}
	return 0;
}

uint8_t sun386i_state::prom_keyboard_r()
{
	// Type like a person. gets() starts by draining whatever was typed ahead:
	// it polls this until it answers "nothing", so a character must only turn
	// up once the machine is really waiting for one, which shows as a long run
	// of empty polls. The drain is a couple of polls, the read loop thousands.
	if (m_inpos >= m_input.size())
		return 0;
	const attotime now = machine().time();
	if (++m_empty_polls < 400 || now - m_last_key < attotime::from_msec(20))
		return 0;
	m_empty_polls = 0;
	m_last_key = now;
	const uint8_t c = uint8_t(m_input[m_inpos++]);
	return c;
}

// A Sun PROM draws the console on the screen itself and the kernel leans on
// it: with the console set to the frame buffer, every character it prints
// comes through this entry point. The stub has to put it on the glass or the
// machine looks dead. Black letters on a light background, which is what the
// bit polarity of this frame buffer says.
void sun386i_state::console_glyph(uint8_t c)
{
	const unsigned cols = 1152 / 8, rows = 900 / 16;
	const bool colour = m_fbtype == 6;
	uint8_t *const fb = reinterpret_cast<uint8_t *>(
			colour ? m_cvram.target() : m_vram.target());
	const unsigned stride = colour ? 1152 : 1152 / 8;

	if (c == '\r')
	{
		m_conx = 0;
		return;
	}
	if (c == '\b')
	{
		if (m_conx)
			m_conx--;
		return;
	}
	if (c != '\n')
	{
		if (c < 0x20 || c > 0x7e)
			return;
		uint8_t const *const glyph = s_font + (c - 0x20) * 16;
		for (unsigned y = 0; y < 16; y++)
		{
			if (colour)
			{
				// a byte a pixel here: ink is the last entry of the map
				for (unsigned x = 0; x < 8; x++)
					fb[(m_cony * 16 + y) * stride + m_conx * 8 + x] =
							BIT(glyph[y], x) ? 0xff : 0x00;
			}
			else
			{
				fb[(m_cony * 16 + y) * stride + m_conx] = glyph[y];
			}
		}
		m_conx++;
		if (m_conx < cols)
			return;
	}
	m_conx = 0;
	if (++m_cony < rows)
		return;
	m_cony = rows - 1;
	std::memmove(fb, fb + 16 * stride, (rows - 1) * 16 * stride);
	std::memset(fb + (rows - 1) * 16 * stride, 0x00, 16 * stride);
	(void)cols;
}

void sun386i_state::prom_console_w(uint8_t data)
{
	console_glyph(data);
	if (data == '\n' || data == '\r')
	{
		if (!m_conline.empty())
			logerror("console: %s\n", m_conline);
		m_conline.clear();
	}
	else if (data >= 0x20 && data < 0x7f)
	{
		m_conline += char(data);
	}
}


void sun386i_state::place_kernel()
{
	if (m_kernel.size() < 0x40 || m_kernel[0] != 0x4c || m_kernel[1] != 0x01)
		return;
	address_space &space = m_maincpu->space(AS_PROGRAM);
	// a reboot starts from clean memory, the way a real reset does: without
	// this the kernel finds its own stale page tables and panics in hat_pmgfree
	for (uint32_t a = 0; a < 0x800000; a += 4)
		space.write_dword(a, 0);
	auto word = [this] (unsigned off) -> uint32_t {
		return uint32_t(m_kernel[off]) | (uint32_t(m_kernel[off + 1]) << 8)
			| (uint32_t(m_kernel[off + 2]) << 16) | (uint32_t(m_kernel[off + 3]) << 24);
	};
	const unsigned nscns = m_kernel[2] | (m_kernel[3] << 8);
	const unsigned opthdr = m_kernel[16] | (m_kernel[17] << 8);
	for (unsigned i = 0; i < nscns; i++)
	{
		const unsigned h = 20 + opthdr + i * 40;
		if (h + 40 > m_kernel.size())
			break;
		const uint32_t vaddr = word(h + 12);
		const uint32_t size = word(h + 16);
		const uint32_t scnptr = word(h + 20);
		if (!size || vaddr < KERNEL_VIRTUAL_BASE)
			continue;
		const uint32_t phys = vaddr - KERNEL_VIRTUAL_BASE;
		for (uint32_t a = 0; a < size; a++)
			space.write_byte(phys + a, scnptr ? m_kernel[scnptr + a] : 0);
	}
	logerror("sun386i: kernel put back for the reboot\n");
}

QUICKLOAD_LOAD_MEMBER(sun386i_state::quickload_kernel)
{
	const uint64_t len = image.length();
	if (len < 0x40)
		return std::make_pair(image_error::INVALIDLENGTH, std::string());

	std::vector<uint8_t> buf(len);
	if (image.fread(&buf[0], len) != len)
		return std::make_pair(image_error::UNSPECIFIED, std::string("cannot read image"));
	m_kernel = buf;

	address_space &space = m_maincpu->space(AS_PROGRAM);

	// a raw floppy image holds a standalone program from sector 1, linked at 0,
	// which is what the boot PROM would have loaded
	if (len == 80 * 2 * 18 * 512)
	{
		for (uint32_t a = 0x200; a < len; a++)
			space.write_byte(a - 0x200, buf[a]);
		space.write_dword(ENTRY_SLOT, 0);
		space.write_dword(PAGING_SLOT, 1);
		space.write_dword(TABLE_SLOT, 0x80000);
		logerror("sun386i: standalone loaded from sector 1, entry 0\n");
		machine().schedule_soft_reset();
		return std::make_pair(std::error_condition(), std::string());
	}

	if (buf[0] != 0x4c || buf[1] != 0x01)
		return std::make_pair(image_error::INVALIDIMAGE, std::string("not an i386 COFF kernel"));

	auto word = [&buf] (unsigned off) -> uint32_t {
		return uint32_t(buf[off]) | (uint32_t(buf[off + 1]) << 8)
			| (uint32_t(buf[off + 2]) << 16) | (uint32_t(buf[off + 3]) << 24);
	};

	const unsigned nscns = buf[2] | (buf[3] << 8);
	const unsigned opthdr = buf[16] | (buf[17] << 8);
	const uint32_t entry = (opthdr >= 28) ? word(36) : 0;

	for (unsigned i = 0; i < nscns; i++)
	{
		const unsigned h = 20 + opthdr + i * 40;
		if (h + 40 > len)
			break;
		const uint32_t vaddr = word(h + 12);
		const uint32_t size = word(h + 16);
		const uint32_t scnptr = word(h + 20);
		if (!size || vaddr < KERNEL_VIRTUAL_BASE)
			continue;
		const uint32_t phys = vaddr - KERNEL_VIRTUAL_BASE;
		if (!scnptr)
		{
			for (uint32_t a = 0; a < size; a++)
				space.write_byte(phys + a, 0);
			continue;
		}
		if (uint64_t(scnptr) + size > len)
			return std::make_pair(image_error::INVALIDIMAGE, std::string("truncated section"));
		for (uint32_t a = 0; a < size; a++)
			space.write_byte(phys + a, buf[scnptr + a]);
	}

	space.write_dword(ENTRY_SLOT, entry - KERNEL_VIRTUAL_BASE);
	space.write_dword(PAGING_SLOT, 0);          // the kernel enables paging itself
	space.write_dword(TABLE_SLOT, 0x8000);      // clear of the kernel image
	logerror("sun386i: kernel loaded, entry %08x at physical %08x\n",
			entry, entry - KERNEL_VIRTUAL_BASE);
	machine().schedule_soft_reset();
	return std::make_pair(std::error_condition(), std::string());
}


static void sun386i_scsi_devices(device_slot_interface &device)
{
	device.option_add("harddisk", NSCSI_HARDDISK);
}


void sun386i_state::mem_map(address_map &map)
{
	map.unmap_value_high();
	map(0x00000000, 0x007fffff).ram();
	map(0xa0000000, 0xa000000f).rw(FUNC(sun386i_state::stub_r), FUNC(sun386i_state::stub_w));
	map(0xa0000010, 0xa0000013).w(FUNC(sun386i_state::cg3_addr_w));
	map(0xa0000014, 0xa0000017).w(FUNC(sun386i_state::cg3_data_w));
	map(0xa0000018, 0xa000001b).lw32(NAME([this] (uint32_t data) { m_cgctrl = data & 0xff; }));
	map(0xa000001c, 0xa000001f).rw(FUNC(sun386i_state::stub_r), FUNC(sun386i_state::stub_w));
	map(0xa0000020, 0xa0000023).lrw8(
			NAME([this] () { return m_kbdscc->cb_r(0); }),
			NAME([this] (uint8_t data) { m_kbdscc->cb_w(0, data); })).umask32(0x000000ff);
	map(0xa0000024, 0xa0000027).lrw8(
			NAME([this] () { return m_kbdscc->db_r(0); }),
			NAME([this] (uint8_t data) { m_kbdscc->db_w(0, data); })).umask32(0x000000ff);
	map(0xa0000028, 0xa000002b).lrw8(
			NAME([this] () { return m_kbdscc->ca_r(0); }),
			NAME([this] (uint8_t data) { m_kbdscc->ca_w(0, data); })).umask32(0x000000ff);
	map(0xa000002c, 0xa000002f).lrw8(
			NAME([this] () { return m_kbdscc->da_r(0); }),
			NAME([this] (uint8_t data) { m_kbdscc->da_w(0, data); })).umask32(0x000000ff);
	map(0xa0000030, 0xa01fffff).rw(FUNC(sun386i_state::stub_r), FUNC(sun386i_state::stub_w));
	map(0xa0200000, 0xa021ffff).ram().share("vram");
	map(0xa0240000, 0xa0241fff).ram();
	// the catch all has to go round the colour frame buffer, not over it:
	// in mame the entry declared later wins, and this one was swallowing
	// every write the machine made to the colour board
	map(0xa0242000, 0xa03fffff).rw(FUNC(sun386i_state::stub_r), FUNC(sun386i_state::stub_w));
	map(0xa0500000, 0xa07fffff).rw(FUNC(sun386i_state::stub_r), FUNC(sun386i_state::stub_w));
	map(0xa0400000, 0xa04fffff).ram().share("cvram");
	map(0xb0000000, 0xb00000ff).rw(FUNC(sun386i_state::atblock_r), FUNC(sun386i_state::atblock_w));
	map(0xc0000000, 0xc001ffff).rw(FUNC(sun386i_state::stub_r), FUNC(sun386i_state::stub_w));
	map(0xd0000000, 0xd000ffff).rw(FUNC(sun386i_state::stub_r), FUNC(sun386i_state::stub_w));
	map(0xf0000000, 0xf0000fff).r(FUNC(sun386i_state::slot_r));
	map(0xf2000000, 0xf2000fff).r(FUNC(sun386i_state::slot_r));
	map(0xf4000000, 0xf4000fff).r(FUNC(sun386i_state::slot_r));
	map(0xf6000000, 0xf6000fff).r(FUNC(sun386i_state::slot_r));
	// the colour board says which one it is: 0x80 is the 1152 by 900 one
	map(0xf7000000, 0xf7000003).lr8(NAME([] () { return 0x80; })).umask32(0x000000ff);
	map(0xf8000000, 0xf8000001).rw(FUNC(sun386i_state::enable_r), FUNC(sun386i_state::enable_w));
	map(0xf9000000, 0xf9000000).w(FUNC(sun386i_state::led_w));
	// wds_wait polls the auxiliary status at offset 0, so that read has to be
	// status_r(): indir_addr_r() used to hide the interrupt bit
	map(0xfb000000, 0xfb000003).r(m_scsi, FUNC(wd33c93_device::status_r)).umask32(0x000000ff);
	map(0xfb000000, 0xfb000003).w(m_scsi, FUNC(wd33c93_device::indir_addr_w)).umask32(0x000000ff);
	map(0xfb000004, 0xfb000007).rw(m_scsi, FUNC(wd33c93_device::indir_reg_r), FUNC(wd33c93_device::indir_reg_w)).umask32(0x000000ff);
	map(0xfb000008, 0xfb000fff).rw(FUNC(sun386i_state::stub_r), FUNC(sun386i_state::stub_w));
	map(0xfc000000, 0xfc000003).rw(m_scc, FUNC(scc8530_device::cb_r), FUNC(scc8530_device::cb_w)).umask32(0x000000ff);
	map(0xfc000004, 0xfc000007).rw(m_scc, FUNC(scc8530_device::db_r), FUNC(scc8530_device::db_w)).umask32(0x000000ff);
	map(0xfc000008, 0xfc00000b).lrw8(
			NAME([this] () { return m_scc->ca_r(0); }),
			NAME([this] (uint8_t data) { m_scc->ca_w(0, data); })).umask32(0x000000ff);
	map(0xfc00000c, 0xfc00000f).lrw8(
			NAME([this] () { return m_scc->da_r(0); }),
			NAME([this] (uint8_t data) { m_scc->da_w(0, data); })).umask32(0x000000ff);
	map(0xfd000000, 0xfd0007ff).rw(m_timekpr, FUNC(m48t02_device::read), FUNC(m48t02_device::write));
	map(0xfe000000, 0xfe00001f).r(FUNC(sun386i_state::idprom_r));
	map(0xfffe0000, 0xffffffff).rom().region("prom", 0);
}

void sun386i_state::io_map(address_map &map)
{
	map.unmap_value_high();
	map(0x0378, 0x037b).nopw();
	map(0x8000, 0x8003).w(FUNC(sun386i_state::prom_console_w)).umask32(0x000000ff);
	map(0x8010, 0x8013).r(FUNC(sun386i_state::prom_keyboard_r)).umask32(0x000000ff);
	map(0x8014, 0x8017).lr8(NAME([this] () { return prom_keyboard_r(); })).umask32(0x000000ff);
	map(0x8004, 0x8007).lw32(NAME([this] (uint32_t data) { logerror("stub trap %u\n", data); if (data == 1) { place_kernel(); machine().schedule_soft_reset(); } }));
	map(0x8008, 0x800b).lw32(NAME([this] (uint32_t data) { logerror("stub trap word %08x\n", data); }));
	map(0x800c, 0x800f).lw32(NAME([this] (uint32_t data) { logerror("stub trap cr2 %08x\n", data); }));
	map(0x03f0, 0x03f3).w(FUNC(sun386i_state::fdc_dor_w)).umask32(0x00ff0000);
	map(0x03f4, 0x03f7).r(m_fdc, FUNC(upd765a_device::msr_r)).umask32(0x000000ff);
	map(0x03f4, 0x03f7).w(m_fdc, FUNC(upd765a_device::dsr_w)).umask32(0x000000ff);
	map(0x03f4, 0x03f7).lrw8(
			NAME([this] () { return m_fdc->fifo_r(); }),
			NAME([this] (uint8_t data) { fdc_cmd_byte(data); m_fdc->fifo_w(data); })).umask32(0x0000ff00);
	map(0x03f4, 0x03f7).r(FUNC(sun386i_state::fdc_dir_r)).umask32(0xff000000);
	map(0x03f4, 0x03f7).w(m_fdc, FUNC(upd765a_device::ccr_w)).umask32(0xff000000);
}


void sun386i_state::machine_start()
{
	m_lines = timer_alloc(FUNC(sun386i_state::hold_lines), this);

	uint8_t *const prom = m_prom->base();
	std::fill(prom, prom + m_prom->bytes(), 0xff);
	for (auto &run : s_prom_runs)
		std::copy(run.data, run.data + run.size, prom + run.offset);
	std::copy(std::begin(s_idprom), std::end(s_idprom), m_idprom->base());
	// which frame buffer the stub says is the console, so the glyphs go there
	m_fbtype = prom[0x220];

	{
		std::ifstream f("sun386i.in", std::ios::binary);
		if (f)
			m_input.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		logerror("sun386i: %u bytes of canned input\n", unsigned(m_input.size()));
	}

	if (auto *const pty = dynamic_cast<pseudo_terminal_device *>(m_ttya->get_card_device()))
		logerror("sun386i: ttya on %s\n", pty->slave_name());

	save_item(NAME(m_atblock));
	save_item(NAME(m_enable));
	save_item(NAME(m_led));
}

// The keyboard and the mouse hang off the second SCC with no modem control of
// their own. The kernel opens the keyboard line from inside consconfig and
// that open waits for carrier, so the line has to show it. It goes here and
// not in machine_reset because the driver resets before its devices do, and
// the SCC would wipe it.
TIMER_CALLBACK_MEMBER(sun386i_state::hold_lines)
{
	// a real PROM clears the glass before it writes on it
	std::memset(m_vram.target(), 0x00, 1152 * 900 / 8);
	std::memset(m_cvram.target(), 0x00, 1152 * 900);
	// a map the stub can write on before anyone programs it: entry zero is
	// the light background and the last one is ink
	for (unsigned i = 0; i < 256; i++)
		m_cgmap[i][0] = m_cgmap[i][1] = m_cgmap[i][2] = 255 - i;
	m_conx = 0;
	m_cony = 0;

	m_kbdscc->dcda_w(0);
	m_kbdscc->dcdb_w(0);
	m_kbdscc->ctsa_w(0);
	m_kbdscc->ctsb_w(0);
}

void sun386i_state::machine_reset()
{
	for (unsigned i = 0; i < sizeof(s_nvram); i++)
		m_timekpr->write(i, s_nvram[i]);

	// the boot parameter block tells the kernel where its root lives; with no
	// diskette in the drive the stub boots the disk, which is what the default
	// boot device in the real PROM does
	{
		floppy_image_device *const fd = m_floppy->get_device();
		const bool disk = !fd || !fd->exists() || getenv("SUN386I_BOOT_DISK");
		uint8_t *const prom = m_prom->base();
		prom[BOOTPARAM + 0x84] = disk ? 's' : 'f';
		prom[BOOTPARAM + 0x85] = 'd';
		prom[BOOTPARAM + 0x86] = 0;
		prom[BOOTPARAM + 0x8c] = disk ? 2 : 0;
		logerror("sun386i: booting %s\n", disk ? "sd(0,2,0)" : "fd(0,0,0)");

		// romvec +0x28 and +0x2c point at a byte saying which device is the
		// console: 1 is ttya, anything else the screen. The stub holds both
		// values, so switching is a matter of moving the pointer.
		if (getenv("SUN386I_TTYA_CONSOLE"))
		{
			for (unsigned slot : { 0x28, 0x2c })
				for (unsigned b = 0; b < 4; b++)
					prom[slot + b] = uint8_t((PROM_BASE + OFF_ONE) >> (8 * b));
			logerror("sun386i: console on ttya\n");
		}

		// argv[1] of the boot parameters, which is where the kernel looks for
		// the single user flag
		if (getenv("SUN386I_SINGLE"))
		{
			prom[BOOTPARAM + 0x28] = '-';
			prom[BOOTPARAM + 0x29] = 's';
			prom[BOOTPARAM + 0x2a] = 0;
			for (unsigned b = 0; b < 4; b++)
				prom[BOOTPARAM + 0x04 + b] =
						uint8_t((PROM_BASE + BOOTPARAM + 0x28) >> (8 * b));
			logerror("sun386i: booting single user\n");
		}
	}

	logerror("machine reset\n");
	m_lines->adjust(attotime::zero);
	m_enable = 0;
	m_led = 0;

	// The keyboard and the mouse hang off the second SCC with no modem
	// control of their own. The kernel opens the keyboard line from inside
	// consconfig, and that open waits for carrier: without it the boot goes
	// to sleep there and init never runs.
	m_kbdscc->dcda_w(0);
	m_kbdscc->dcdb_w(0);
	m_kbdscc->ctsa_w(0);
	m_kbdscc->ctsb_w(0);
	std::fill(std::begin(m_atblock), std::end(m_atblock), 0);
}

static void sun386i_floppies(device_slot_interface &device)
{
	device.option_add("35hd", FLOPPY_35_HD);
}

void sun386i_state::sun386i(machine_config &config)
{
	I386(config, m_maincpu, 20_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &sun386i_state::mem_map);
	m_maincpu->set_addrmap(AS_IO, &sun386i_state::io_map);
	m_maincpu->set_irq_acknowledge_callback(FUNC(sun386i_state::icu_ack));

	AM9517A(config, m_dma[0], 20_MHz_XTAL / 4);
	m_dma[0]->out_hreq_callback().set(FUNC(sun386i_state::dma_hrq_w));
	m_dma[0]->out_eop_callback().set(FUNC(sun386i_state::dma_eop_w));
	m_dma[0]->in_memr_callback().set(FUNC(sun386i_state::dma_read));
	m_dma[0]->out_memw_callback().set(FUNC(sun386i_state::dma_write));
	m_dma[0]->in_ior_callback<2>().set(m_fdc, FUNC(upd765a_device::dma_r));
	m_dma[0]->out_iow_callback<2>().set(m_fdc, FUNC(upd765a_device::dma_w));
	AM9517A(config, m_dma[1], 20_MHz_XTAL / 4);
	m_dma[1]->out_hreq_callback().set(FUNC(sun386i_state::dma2_hrq_w));
	m_dma[1]->out_eop_callback().set(FUNC(sun386i_state::dma2_eop_w));
	m_dma[1]->in_memr_callback().set(FUNC(sun386i_state::dma2_read));
	m_dma[1]->out_memw_callback().set(FUNC(sun386i_state::dma2_write));
	m_dma[1]->in_ior_callback<3>().set(m_scsi, FUNC(wd33c93_device::dma_r));
	m_dma[1]->out_iow_callback<3>().set(m_scsi, FUNC(wd33c93_device::dma_w));

	for (int bank = 0; bank < 3; bank++)
	{
		PIC8259(config, m_pic[bank]);
		m_pic[bank]->in_sp_callback().set_constant(1);
		m_pic[bank]->out_int_callback().set(
				[this, bank] (int state) { icu_int_w(bank, state); });
	}

	PIT8254(config, m_pit);
	m_pit->set_clk<0>(1'193'182);

	// the clock the kernel actually uses: timer 3 of the interrupt block,
	// counter at offset 0x44 with its control word at 0x47
	PIT8254(config, m_pit3);
	m_pit3->set_clk<0>(1'193'182);
	m_pit3->out_handler<0>().set(m_pic[1], FUNC(pic8259_device::ir0_w));   // IRQ 8

	SCC8530(config, m_scc, 4'915'200);
	m_scc->out_int_callback().set(m_pic[0], FUNC(pic8259_device::ir1_w));   // IRQ 9
	m_scc->out_txda_callback().set(m_ttya, FUNC(rs232_port_device::write_txd));
	m_scc->out_txdb_callback().set("ttyb", FUNC(rs232_port_device::write_txd));

	rs232_port_device &ttya(RS232_PORT(config, m_ttya, default_rs232_devices, "terminal"));
	ttya.rxd_handler().set(m_scc, FUNC(scc8530_device::rxa_w));
	ttya.dcd_handler().set(m_scc, FUNC(scc8530_device::dcda_w));
	ttya.cts_handler().set(m_scc, FUNC(scc8530_device::ctsa_w));
	rs232_port_device &ttyb(RS232_PORT(config, "ttyb", default_rs232_devices, nullptr));
	ttyb.rxd_handler().set(m_scc, FUNC(scc8530_device::rxb_w));
	ttyb.dcd_handler().set(m_scc, FUNC(scc8530_device::dcdb_w));
	ttyb.cts_handler().set(m_scc, FUNC(scc8530_device::ctsb_w));

	SCC8530(config, m_kbdscc, 4'915'200);
	m_kbdscc->out_int_callback().set(m_pic[0], FUNC(pic8259_device::ir1_w));

	UPD765A(config, m_fdc, 24_MHz_XTAL / 3, true, true);
	m_fdc->intrq_wr_callback().set(m_pic[0], FUNC(pic8259_device::ir6_w));   // IRQ 14 is bit 14: bank at 0x20, request 6
	m_fdc->drq_wr_callback().set(m_dma[0], FUNC(am9517a_device::dreq2_w));
	FLOPPY_CONNECTOR(config, "fdc:0", sun386i_floppies, "35hd",
			floppy_image_device::default_pc_floppy_formats);

	// the controller table gives the SCSI its address, and wds_init writes 7
	// as the initiator id; the disks hang off it as sd0, sd1, sd2 and sd3 on
	// targets 0, 1, 2 and 4
	auto &scsibus(NSCSI_BUS(config, "scsibus"));
	WD33C93(config, m_scsi, 10'000'000);
	scsibus.set_external_device(7, m_scsi);
	m_scsi->irq_cb().set(m_pic[2], FUNC(pic8259_device::ir0_w));   // irq 16
	m_scsi->drq_cb().set(m_dma[1], FUNC(am9517a_device::dreq3_w)); // dma 7
	NSCSI_CONNECTOR(config, "scsibus:0", sun386i_scsi_devices, nullptr, false);
	NSCSI_CONNECTOR(config, "scsibus:1", sun386i_scsi_devices, nullptr, false);
	NSCSI_CONNECTOR(config, "scsibus:2", sun386i_scsi_devices, "harddisk", false);
	NSCSI_CONNECTOR(config, "scsibus:3", sun386i_scsi_devices, nullptr, false);
	NSCSI_CONNECTOR(config, "scsibus:4", sun386i_scsi_devices, nullptr, false);
	NSCSI_CONNECTOR(config, "scsibus:5", sun386i_scsi_devices, nullptr, false);
	NSCSI_CONNECTOR(config, "scsibus:6", sun386i_scsi_devices, nullptr, false);

	M48T02(config, m_timekpr);

	SCREEN(config, m_screen);
	m_screen->set_refresh_hz(66);
	m_screen->set_size(1152, 900);
	m_screen->set_visarea(0, 1151, 0, 899);
	m_screen->set_screen_update(FUNC(sun386i_state::screen_update));

	SCREEN(config, m_cscreen);
	m_cscreen->set_refresh_hz(66);
	m_cscreen->set_size(1152, 900);
	m_cscreen->set_visarea(0, 1151, 0, 899);
	m_cscreen->set_screen_update(FUNC(sun386i_state::screen_update_cg3));

	SUNKBD_PORT(config, "keyboard", default_sun_keyboard_devices, "type4hle")
			.rxd_handler().set(m_kbdscc, FUNC(scc8530_device::rxa_w));
	// the mouse shares the second SCC with the keyboard, on channel B at 1200
	// baud; with nothing on that line it sits low and the kernel drowns in
	// special receive interrupts as soon as it opens the console
	SUNMOUSE_PORT(config, "mouse", default_sun_mouse_devices, "hle1200")
			.rxd_handler().set(m_kbdscc, FUNC(scc8530_device::rxb_w));
	m_kbdscc->out_txdb_callback().set("mouse", FUNC(sun_mouse_port_device::write_txd));
	m_kbdscc->out_txda_callback().set("keyboard", FUNC(sun_keyboard_port_device::write_txd));


	quickload_image_device &quik(QUICKLOAD(config, "quickload", "vmunix,coff"));
	quik.set_load_callback(FUNC(sun386i_state::quickload_kernel));
}


static INPUT_PORTS_START(sun386i)
INPUT_PORTS_END

ROM_START(sun386i)
	ROM_REGION32_LE(0x20000, "prom", ROMREGION_ERASEFF)
	ROM_REGION(0x20, "idprom", ROMREGION_ERASE00)
ROM_END

} // anonymous namespace


//    YEAR  NAME     PARENT COMPAT MACHINE  INPUT    CLASS          INIT        COMPANY             FULLNAME    FLAGS
COMP( 1988, sun386i, 0,     0,     sun386i, sun386i, sun386i_state, empty_init, "Sun Microsystems", "Sun 386i", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )

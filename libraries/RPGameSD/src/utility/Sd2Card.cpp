/* Arduino Sd2Card Library
   Copyright (C) 2009 by William Greiman

   This file is part of the Arduino Sd2Card Library

   This Library is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the Arduino Sd2Card Library.  If not, see
   <http://www.gnu.org/licenses/>.
*/
/*
 * CHGAME: this file is where nearly all of the speed-up lives. Read the
 * banner in Sd2Card.h first for the why. In short:
 *
 *   - "Transport" (the first section below) is new for the CH32X035: SPI1
 *     is driven by register, bulk reads go through DMA, and the bus is
 *     borrowed from / returned to the LCD driver around every transaction.
 *     The stock SPI-library and software-SPI transports are kept intact
 *     under #if for other boards.
 *   - Everything from cardCommand() down is the original sdfatlib protocol
 *     code. Where it moved bytes one spiRec() at a time over a buffer, it
 *     now calls spiRecvBulk(), which is one DMA transfer on the CH32.
 *   - readStart()/readStream()/readStop()/readBlocksPipelined() at the end
 *     are new: CMD18 multi-block streaming.
 *
 * Originally this file began with "#define USE_SPI_LIB". That decision now
 * lives in Sd2Card.h, which knows whether the CH32 transport is in use.
 */
#include <Arduino.h>
#include "Sd2Card.h"

#if SD_RP2040_FAST
#include <RPGfx.h>
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/regs/spi.h"
#include "hardware/regs/dma.h"
#include "pico/stdlib.h"

static spi_inst_t *const cardSpi = RPGAME_SD_SPI_BUS ? spi1 : spi0;
static uint8_t s_csPin;
static bool s_csReady, s_busOwned;
static uint8_t s_br = 7;
#if RPGAME_SHARED_SPI
static uint32_t s_hostCr0, s_hostCr1, s_hostCpsr;
#endif
static uint32_t s_sdHz;
static uint8_t s_dmaDead, s_txPending;
static uint8_t s_txFiller = 0xFF, s_rxSink;
static int s_rxChannel = -1, s_txChannel = -1;
#define SD_BR_IDENTIFY 7
#ifndef SD_DMA_MIN_BYTES
#define SD_DMA_MIN_BYTES 16
#endif
#define SD_RAMFUNC __attribute__((section(".time_critical.rpgamesd"), noinline))
#define SD_CS_LOW() gpio_put(s_csPin, 0)
#define SD_CS_HIGH() gpio_put(s_csPin, 1)

static void spiDrain() {
    while (spi_is_busy(cardSpi)) tight_loop_contents();
    while (spi_is_readable(cardSpi)) (void)spi_get_hw(cardSpi)->dr;
    spi_get_hw(cardSpi)->icr = SPI_SSPICR_RORIC_BITS;
}
static void spiPinsInit() {
    gpio_set_function(RPGAME_SD_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(RPGAME_SD_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(RPGAME_SD_SPI_MISO, GPIO_FUNC_SPI);
    gpio_pull_up(RPGAME_SD_SPI_MISO);
    if (!(spi_get_hw(cardSpi)->cr1 & SPI_SSPCR1_SSE_BITS)) spi_init(cardSpi, 187500);
#ifdef RPGAME_SD_NO_DMA
    s_dmaDead = 1;
#else
    if (s_rxChannel < 0) {
        s_rxChannel = dma_claim_unused_channel(true);
        s_txChannel = dma_claim_unused_channel(true);
        dma_channel_set_irq0_enabled(s_rxChannel, false);
        dma_channel_set_irq1_enabled(s_rxChannel, false);
        dma_channel_set_irq0_enabled(s_txChannel, false);
        dma_channel_set_irq1_enabled(s_txChannel, false);
    }
#endif
}
static void busClaim() {
    if (s_busOwned) return;
    // Preserve the existing flush barrier: callers can reuse framebuffer
    // storage for SD data even when the two SPI peripherals are separate.
    gfx_wait();
    spiDrain();
#if RPGAME_SHARED_SPI
    auto *hw = spi_get_hw(cardSpi);
    s_hostCr0 = hw->cr0; s_hostCr1 = hw->cr1; s_hostCpsr = hw->cpsr;
#endif
    uint32_t hz = s_br == SD_BR_IDENTIFY ? 187500u : (RPGAME_SD_MAX_HZ >> s_br);
    s_sdHz = spi_set_baudrate(cardSpi, hz);
    spi_set_format(cardSpi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    s_busOwned = true;
}
static void busRelease() {
    if (!s_busOwned) return;
    spiDrain();
#if RPGAME_SHARED_SPI
    auto *hw = spi_get_hw(cardSpi);
    hw->cr1 &= ~SPI_SSPCR1_SSE_BITS;
    hw->cr0 = s_hostCr0; hw->cpsr = s_hostCpsr; hw->cr1 = s_hostCr1;
#endif
    s_busOwned = false;
}
static inline uint8_t spiXfer(uint8_t b) {
    uint8_t rx;
    spi_write_read_blocking(cardSpi, &b, &rx, 1);
    return rx;
}
static inline void spiSend(uint8_t b) { (void)spiXfer(b); }
static inline uint8_t spiRec() { return spiXfer(0xFF); }
static SD_RAMFUNC void rxPolled(uint8_t *dst, uint32_t n) {
    while (n--) {
        uint8_t v = spiXfer(0xFF);
        if (dst) *dst++ = v;
    }
}
static void xferDmaStart(uint8_t *dst, const uint8_t *src, uint32_t n) {
    dma_channel_config rx = dma_channel_get_default_config(s_rxChannel);
    channel_config_set_transfer_data_size(&rx, DMA_SIZE_8);
    channel_config_set_read_increment(&rx, false);
    channel_config_set_write_increment(&rx, dst != nullptr);
    channel_config_set_dreq(&rx, spi_get_dreq(cardSpi, false));
    dma_channel_config tx = dma_channel_get_default_config(s_txChannel);
    channel_config_set_transfer_data_size(&tx, DMA_SIZE_8);
    channel_config_set_read_increment(&tx, src != nullptr);
    channel_config_set_write_increment(&tx, false);
    channel_config_set_dreq(&tx, spi_get_dreq(cardSpi, true));
    dma_channel_configure(s_rxChannel, &rx, dst ? dst : &s_rxSink,
                          &spi_get_hw(cardSpi)->dr, n, false);
    dma_channel_configure(s_txChannel, &tx, &spi_get_hw(cardSpi)->dr,
                          src ? src : &s_txFiller, n, false);
    dma_start_channel_mask((1u << s_rxChannel) | (1u << s_txChannel));
}
static inline void rxDmaStart(uint8_t *dst, uint32_t n) { xferDmaStart(dst, nullptr, n); }
static bool rxDmaWait(uint32_t n) {
    (void)n;
    uint32_t started = micros();
    while (dma_channel_is_busy(s_rxChannel) || dma_channel_is_busy(s_txChannel)) {
        bool error = ((dma_channel_hw_addr(s_rxChannel)->ctrl_trig |
                       dma_channel_hw_addr(s_txChannel)->ctrl_trig) & DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS);
        if (error || uint32_t(micros() - started) > 1000000u) {
            // RP2350-E5: disable both channels before aborting so they
            // cannot be re-triggered while the abort retires transfers.
            hw_clear_bits(&dma_channel_hw_addr(s_rxChannel)->ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
            hw_clear_bits(&dma_channel_hw_addr(s_txChannel)->ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
            dma_channel_abort(s_rxChannel); dma_channel_abort(s_txChannel);
            s_dmaDead = 1;
            spiDrain();
            return false;
        }
        tight_loop_contents();
    }
    spiDrain();
    return true;
}
static bool spiRecvBulk(uint8_t *dst, uint32_t n) {
    if (n >= SD_DMA_MIN_BYTES && !s_dmaDead) {
        rxDmaStart(dst, n);
        return rxDmaWait(n);
    }
    rxPolled(dst, n);
    return true;
}
uint8_t Sd2Card::dmaEnabled() { return !s_dmaDead; }
uint32_t Sd2Card::sckHz() { return s_sdHz; }

#elif !defined(SOFTWARE_SPI)
//==============================================================================
// Stock transport: Arduino SPI library (unchanged apart from spiRecvBulk)
//==============================================================================
#ifdef USE_SPI_LIB

  #ifndef SDCARD_SPI
    #define SDCARD_SPI SPI
  #endif

  #include <SPI.h>
  static SPISettings settings;
#endif
// functions for hardware SPI
/** Send a byte to the card */
static void spiSend(uint8_t b) {
  #ifndef USE_SPI_LIB
  SPDR = b;
  while (!(SPSR & (1 << SPIF)))
    ;
  #else
  SDCARD_SPI.transfer(b);
  #endif
}
/** Receive a byte from the card */
static  uint8_t spiRec(void) {
  #ifndef USE_SPI_LIB
  spiSend(0XFF);
  return SPDR;
  #else
  return SDCARD_SPI.transfer(0xFF);
  #endif
}
#else  // SOFTWARE_SPI
//------------------------------------------------------------------------------
/** nop to tune soft SPI timing */
#define nop asm volatile ("nop\n\t")
//------------------------------------------------------------------------------
/** Soft SPI receive */
uint8_t spiRec(void) {
  uint8_t data = 0;
  // no interrupts during byte receive - about 8 us
  cli();
  // output pin high - like sending 0XFF
  fastDigitalWrite(SPI_MOSI_PIN, HIGH);

  for (uint8_t i = 0; i < 8; i++) {
    fastDigitalWrite(SPI_SCK_PIN, HIGH);

    // adjust so SCK is nice
    nop;
    nop;

    data <<= 1;

    if (fastDigitalRead(SPI_MISO_PIN)) {
      data |= 1;
    }

    fastDigitalWrite(SPI_SCK_PIN, LOW);
  }
  // enable interrupts
  sei();
  return data;
}
//------------------------------------------------------------------------------
/** Soft SPI send */
void spiSend(uint8_t data) {
  // no interrupts during byte send - about 8 us
  cli();
  for (uint8_t i = 0; i < 8; i++) {
    fastDigitalWrite(SPI_SCK_PIN, LOW);

    fastDigitalWrite(SPI_MOSI_PIN, data & 0X80);

    data <<= 1;

    fastDigitalWrite(SPI_SCK_PIN, HIGH);
  }
  // hold SCK high for a few ns
  nop;
  nop;
  nop;
  nop;

  fastDigitalWrite(SPI_SCK_PIN, LOW);
  // enable interrupts
  sei();
}
#endif  // SOFTWARE_SPI

#if !SD_RP2040_FAST
/* CHGAME: the byte-at-a-time equivalent of the CH32 bulk receive, so the
   shared protocol code below reads the same on every transport. */
static uint8_t spiRecvBulk(uint8_t* dst, uint16_t n) {
  if (dst) {
    while (n--) {
      *dst++ = spiRec();
    }
  } else {
    while (n--) {
      spiRec();
    }
  }
  return true;
}

uint8_t Sd2Card::dmaEnabled(void) {
  return false;
}
#endif

//==============================================================================
// SD PROTOCOL
//==============================================================================
//------------------------------------------------------------------------------
// CHSDtoUSB: CRC7 of a 5-byte command frame, shifted into place with the end
// bit, i.e. the sixth byte on the wire. x^7 + x^3 + 1 (SD spec 4.5).
static uint8_t crc7(const uint8_t* p) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t d = p[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc <<= 1;
      if ((d ^ crc) & 0x80) {
        crc ^= 0x09;
      }
      d <<= 1;
    }
  }
  return (uint8_t)((crc << 1) | 1);
}
//------------------------------------------------------------------------------
// CHSDtoUSB: CRC16-CCITT (x^16 + x^12 + x^5 + 1), which the card sends with
// every block it reads and, after crcOn(), checks on every block it is sent.
#ifndef SD_RAMFUNC
  #define SD_RAMFUNC
#endif
static uint16_t s_crcTable[256];

static void crcTableInit(void) {
  if (s_crcTable[1]) {
    return;
  }
  for (uint16_t i = 0; i < 256; i++) {
    uint16_t c = (uint16_t)(i << 8);
    for (uint8_t b = 0; b < 8; b++) {
      c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    s_crcTable[i] = c;
  }
}

SD_RAMFUNC uint16_t Sd2Card::crc16(const uint8_t* p, uint16_t n, uint16_t crc) {
  uint32_t c = crc;                 // bits above 15 are junk; only 0-15 are kept
  while (n--) {
    c = (c << 8) ^ s_crcTable[((c >> 8) ^ *p++) & 0xFF];
  }
  return (uint16_t)c;
}
//------------------------------------------------------------------------------
// send command and return error code.  Return zero for OK
uint8_t Sd2Card::cardCommand(uint8_t cmd, uint32_t arg) {
  // end read if in partialBlockRead mode
  readEnd();

  // CHGAME: a command in the middle of a CMD18 stream would be read as data
  // by nobody and ignored by the card; close the stream properly first.
  // readStop() clears streaming_ before it sends its own CMD12, so this
  // does not recurse.
  if (streaming_ && cmd != CMD12) {
    readStop();
  }

  // select card
  chipSelectLow();

  // wait up to 300 ms if busy
  // CHGAME: except for CMD12. STOP_TRANSMISSION is sent while the card is
  // still streaming data, so MISO is carrying data bytes, not a busy
  // indication; waiting for 0xFF there would only burn time reading data
  // nobody wants. (SdFat skips the wait for CMD12 for the same reason.)
  if (cmd != CMD12) {
    waitNotBusy(300);
  }

  // send command and argument
  // CHSDtoUSB: followed by the real CRC7 for every command (stock only had
  // the constants for CMD0 and CMD8), so that after crcOn() a command garbled
  // on the wire - a block address with a flipped bit - is refused by the
  // card instead of reading or writing the wrong block.
  uint8_t frame[5] = { (uint8_t)(cmd | 0x40), (uint8_t)(arg >> 24),
                       (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg };
  for (uint8_t i = 0; i < 5; i++) {
    spiSend(frame[i]);
  }
  spiSend(crc7(frame));

  // CHGAME: the byte after CMD12 is a "stuff byte" (SD spec 7.3.1.3) and
  // must be discarded before looking for the R1 response.
  if (cmd == CMD12) {
    spiRec();
  }

  // wait for response
  for (uint8_t i = 0; ((status_ = spiRec()) & 0X80) && i != 0XFF; i++)
    ;
  return status_;
}
//------------------------------------------------------------------------------
/**
   Determine the size of an SD flash memory card.

   \return The number of 512 byte data blocks in the card
           or zero if an error occurs.
*/
uint32_t Sd2Card::cardSize(void) {
  csd_t csd;
  if (!readCSD(&csd)) {
    return 0;
  }
  if (csd.v1.csd_ver == 0) {
    uint8_t read_bl_len = csd.v1.read_bl_len;
    uint16_t c_size = (csd.v1.c_size_high << 10)
                      | (csd.v1.c_size_mid << 2) | csd.v1.c_size_low;
    uint8_t c_size_mult = (csd.v1.c_size_mult_high << 1)
                          | csd.v1.c_size_mult_low;
    return (uint32_t)(c_size + 1) << (c_size_mult + read_bl_len - 7);
  } else if (csd.v2.csd_ver == 1) {
    uint32_t c_size = ((uint32_t)csd.v2.c_size_high << 16)
                      | (csd.v2.c_size_mid << 8) | csd.v2.c_size_low;
    return (c_size + 1) << 10;
  } else {
    error(SD_CARD_ERROR_BAD_CSD);
    return 0;
  }
}
//------------------------------------------------------------------------------
#if SD_RP2040_FAST
/*
 * Chip select also claims/releases the configured SD bus. A shared bus
 * restores the LCD's SPI registers; a separate bus retains its SD format.
 * The claim is
 * idempotent, so the protocol code can call chipSelectLow() as often as it
 * likes; the first call of a transaction does the swap.
 */
void Sd2Card::chipSelectHigh(void) {
  if (s_csReady) {                 // nothing to do before init() has run
    SD_CS_HIGH();
  }
  if (s_busOwned) {
    // 8 clocks with CS high so the card releases MISO (SD spec 6.4.1.1).
    spiXfer(0XFF);
    busRelease();
  }
}
//------------------------------------------------------------------------------
void Sd2Card::chipSelectLow(void) {
  if (!s_csReady) {                // card used before init(): refuse quietly
    return;
  }
  busClaim();
  SD_CS_LOW();
}
#else
static uint8_t chip_select_asserted = 0;

void Sd2Card::chipSelectHigh(void) {
  digitalWrite(chipSelectPin_, HIGH);
  #ifdef USE_SPI_LIB
  if (chip_select_asserted) {
    chip_select_asserted = 0;
    SDCARD_SPI.endTransaction();
  }
  #endif
}
//------------------------------------------------------------------------------
void Sd2Card::chipSelectLow(void) {
  #ifdef USE_SPI_LIB
  if (!chip_select_asserted) {
    chip_select_asserted = 1;
    SDCARD_SPI.beginTransaction(settings);
  }
  #endif
  digitalWrite(chipSelectPin_, LOW);
}
#endif
//------------------------------------------------------------------------------
/** Erase a range of blocks.

   \param[in] firstBlock The address of the first block in the range.
   \param[in] lastBlock The address of the last block in the range.

   \note This function requests the SD card to do a flash erase for a
   range of blocks.  The data on the card after an erase operation is
   either 0 or 1, depends on the card vendor.  The card must support
   single block erase.

   \return The value one, true, is returned for success and
   the value zero, false, is returned for failure.
*/
uint8_t Sd2Card::erase(uint32_t firstBlock, uint32_t lastBlock) {
  if (!eraseSingleBlockEnable()) {
    error(SD_CARD_ERROR_ERASE_SINGLE_BLOCK);
    goto fail;
  }
  if (type_ != SD_CARD_TYPE_SDHC) {
    firstBlock <<= 9;
    lastBlock <<= 9;
  }
  if (cardCommand(CMD32, firstBlock)
      || cardCommand(CMD33, lastBlock)
      || cardCommand(CMD38, 0)) {
    error(SD_CARD_ERROR_ERASE);
    goto fail;
  }
  if (!waitNotBusy(SD_ERASE_TIMEOUT)) {
    error(SD_CARD_ERROR_ERASE_TIMEOUT);
    goto fail;
  }
  chipSelectHigh();
  return true;

fail:
  chipSelectHigh();
  return false;
}
//------------------------------------------------------------------------------
/** Determine if card supports single block erase.

   \return The value one, true, is returned if single block erase is supported.
   The value zero, false, is returned if single block erase is not supported.
*/
uint8_t Sd2Card::eraseSingleBlockEnable(void) {
  csd_t csd;
  return readCSD(&csd) ? csd.v1.erase_blk_en : 0;
}
//------------------------------------------------------------------------------
/**
   Initialize an SD flash memory card.

   \param[in] sckRateID SPI clock rate selector. See setSckRate().
   \param[in] chipSelectPin SD chip select pin number.

   \return The value one, true, is returned for success and
   the value zero, false, is returned for failure.  The reason for failure
   can be determined by calling errorCode() and errorData().
*/
uint8_t Sd2Card::init(uint8_t sckRateID, uint8_t chipSelectPin,
                      unsigned int cmd0Timeout) {
  errorCode_ = inBlock_ = partialBlockRead_ = type_ = 0;
  streaming_ = crcPending_ = 0;   // CHGAME: forget any half-finished stream
  crcTableInit();                 // CHSDtoUSB
  blockRemain_ = 0;
  chipSelectPin_ = chipSelectPin;
  // 16-bit init start time allows over a minute
  unsigned int t0 = millis();
  uint32_t arg;

  #if SD_RP2040_FAST
  s_csPin = chipSelectPin_;
  s_csReady = true;
  gpio_init(s_csPin);
  gpio_put(s_csPin, 1);
  gpio_set_dir(s_csPin, GPIO_OUT);
  spiPinsInit();
  s_br = SD_BR_IDENTIFY;

  // must supply min of 74 clock cycles with CS high.
  busClaim();
  for (uint8_t i = 0; i < 10; i++) {
    spiSend(0XFF);
  }
  busRelease();
  #else
  // set pin modes
  pinMode(chipSelectPin_, OUTPUT);
  digitalWrite(chipSelectPin_, HIGH);
  #ifndef USE_SPI_LIB
  pinMode(SPI_MISO_PIN, INPUT);
  pinMode(SPI_MOSI_PIN, OUTPUT);
  pinMode(SPI_SCK_PIN, OUTPUT);
  #endif

  #ifndef SOFTWARE_SPI
  #ifndef USE_SPI_LIB
  // SS must be in output mode even it is not chip select
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH); // disable any SPI device using hardware SS pin
  // Enable SPI, Master, clock rate f_osc/128
  SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR1) | (1 << SPR0);
  // clear double speed
  SPSR &= ~(1 << SPI2X);
  #else // USE_SPI_LIB
  SDCARD_SPI.begin();
  settings = SPISettings(250000, MSBFIRST, SPI_MODE0);
  #endif // USE_SPI_LIB
  #endif // SOFTWARE_SPI

  // must supply min of 74 clock cycles with CS high.
  #ifdef USE_SPI_LIB
  SDCARD_SPI.beginTransaction(settings);
  #endif
  for (uint8_t i = 0; i < 10; i++) {
    spiSend(0XFF);
  }
  #ifdef USE_SPI_LIB
  SDCARD_SPI.endTransaction();
  #endif
  #endif // SD_RP2040_FAST

  chipSelectLow();

  // command to go idle in SPI mode
  while ((status_ = cardCommand(CMD0, 0)) != R1_IDLE_STATE) {
    unsigned int d = millis() - t0;
    if (d > cmd0Timeout) {
      error(SD_CARD_ERROR_CMD0);
      goto fail;
    }
  }
  // check SD version
  if ((cardCommand(CMD8, 0x1AA) & R1_ILLEGAL_COMMAND)) {
    type(SD_CARD_TYPE_SD1);
  } else {
    // only need last byte of r7 response
    for (uint8_t i = 0; i < 4; i++) {
      status_ = spiRec();
    }
    if (status_ != 0XAA) {
      error(SD_CARD_ERROR_CMD8);
      goto fail;
    }
    type(SD_CARD_TYPE_SD2);
  }
  // initialize card and send host supports SDHC if SD2
  arg = type() == SD_CARD_TYPE_SD2 ? 0X40000000 : 0;

  while ((status_ = cardAcmd(ACMD41, arg)) != R1_READY_STATE) {
    // check for timeout
    unsigned int d = millis() - t0;
    if (d > SD_INIT_TIMEOUT) {
      error(SD_CARD_ERROR_ACMD41);
      goto fail;
    }
  }
  // if SD2 read OCR register to check for SDHC card
  if (type() == SD_CARD_TYPE_SD2) {
    if (cardCommand(CMD58, 0)) {
      error(SD_CARD_ERROR_CMD58);
      goto fail;
    }
    if ((spiRec() & 0XC0) == 0XC0) {
      type(SD_CARD_TYPE_SDHC);
    }
    // discard rest of ocr - contains allowed voltage range
    for (uint8_t i = 0; i < 3; i++) {
      spiRec();
    }
  }
  chipSelectHigh();

  #ifndef SOFTWARE_SPI
  return setSckRate(sckRateID);
  #else  // SOFTWARE_SPI
  return true;
  #endif  // SOFTWARE_SPI

fail:
  chipSelectHigh();
  return false;
}
//------------------------------------------------------------------------------
/**
   Enable or disable partial block reads.

   Enabling partial block reads improves performance by allowing a block
   to be read over the SPI bus as several sub-blocks.  Errors may occur
   if the time between reads is too long since the SD card may timeout.
   The SPI SS line will be held low until the entire block is read or
   readEnd() is called.

   Use this for applications like the Adafruit Wave Shield.

   \param[in] value The value TRUE (non-zero) or FALSE (zero).)
*/
void Sd2Card::partialBlockRead(uint8_t value) {
  readEnd();
  partialBlockRead_ = value;
}
//------------------------------------------------------------------------------
/**
   Read a 512 byte block from an SD card device.

   \param[in] block Logical block to be read.
   \param[out] dst Pointer to the location that will receive the data.

   \return The value one, true, is returned for success and
   the value zero, false, is returned for failure.
*/
uint8_t Sd2Card::readBlock(uint32_t block, uint8_t* dst) {
  return readData(block, 0, 512, dst);
}
//------------------------------------------------------------------------------
/**
   Read part of a 512 byte block from an SD card.

   \param[in] block Logical block to be read.
   \param[in] offset Number of bytes to skip at start of block
   \param[out] dst Pointer to the location that will receive the data.
   \param[in] count Number of bytes to read
   \return The value one, true, is returned for success and
   the value zero, false, is returned for failure.
*/
uint8_t Sd2Card::readData(uint32_t block,
                          uint16_t offset, uint16_t count, uint8_t* dst) {
  if (count == 0) {
    return true;
  }
  if ((count + offset) > 512) {
    goto fail;
  }
  if (!inBlock_ || block != block_ || offset < offset_) {
    block_ = block;
    // use address if not SDHC card
    if (type() != SD_CARD_TYPE_SDHC) {
      block <<= 9;
    }
    if (cardCommand(CMD17, block)) {
      error(SD_CARD_ERROR_CMD17);
      goto fail;
    }
    if (!waitStartBlock()) {
      goto fail;
    }
    offset_ = 0;
    inBlock_ = 1;
  }

  // CHGAME: was two per-byte spiRec() loops (skip to offset, then copy).
  // Each is now a single bulk transfer - DMA on the CH32 - and the skip
  // uses the discard form so no buffer is needed for it.
  if (offset_ < offset) {
    if (!spiRecvBulk(NULL, offset - offset_)) {
      error(SD_CARD_ERROR_DMA);
      goto fail;
    }
    offset_ = offset;
  }
  if (!spiRecvBulk(dst, count)) {
    error(SD_CARD_ERROR_DMA);
    goto fail;
  }

  offset_ += count;
  if (!partialBlockRead_ || offset_ >= 512) {
    // read rest of data, checksum and set chip select high
    readEnd();
  }
  return true;

fail:
  inBlock_ = 0;   // CHGAME: a failed read must not look like an open block
  chipSelectHigh();
  return false;
}
//------------------------------------------------------------------------------
/** Skip remaining data in a block when in partial block read mode. */
void Sd2Card::readEnd(void) {
  if (inBlock_) {
    // skip data and crc
    // CHGAME: one bulk discard of (rest of block + 2 CRC bytes) instead of
    // a spiRec() loop. The original counted offset_ up to 514 the same way.
    spiRecvBulk(NULL, 514 - offset_);
    chipSelectHigh();
    inBlock_ = 0;
  }
}
//------------------------------------------------------------------------------
/** read CID or CSR register */
uint8_t Sd2Card::readRegister(uint8_t cmd, void* buf) {
  uint8_t* dst = reinterpret_cast<uint8_t*>(buf);
  if (cardCommand(cmd, 0)) {
    error(SD_CARD_ERROR_READ_REG);
    goto fail;
  }
  if (!waitStartBlock()) {
    goto fail;
  }
  // transfer data
  for (uint16_t i = 0; i < 16; i++) {
    dst[i] = spiRec();
  }
  spiRec();  // get first crc byte
  spiRec();  // get second crc byte
  chipSelectHigh();
  return true;

fail:
  chipSelectHigh();
  return false;
}
//------------------------------------------------------------------------------
/**
   Set the SPI clock rate.

   \param[in] sckRateID A value in the range [0, 6].

   The SPI clock will be set to F_CPU/pow(2, 1 + sckRateID). The maximum
   SPI rate is F_CPU/2 for \a sckRateID = 0 and the minimum rate is F_CPU/128
   for \a scsRateID = 6.

   CHGAME: on the CH32 that formula is exact, because it is literally the
   SPI1 BR field: 0 = 24 MHz, 1 = 12 MHz, 2 = 6 MHz ... 6 = 375 kHz.

   \return The value one, true, is returned for success and the value zero,
   false, is returned for an invalid value of \a sckRateID.
*/
uint8_t Sd2Card::setSckRate(uint8_t sckRateID) {
  if (sckRateID > 6) {
    error(SD_CARD_ERROR_SCK_RATE);
    return false;
  }
  sckRateId_ = sckRateID;
  #if SD_RP2040_FAST
  // Takes effect at the next busClaim(), i.e. the next transaction.
  s_br = sckRateID;
  #elif !defined(USE_SPI_LIB)
  // see avr processor datasheet for SPI register bit definitions
  if ((sckRateID & 1) || sckRateID == 6) {
    SPSR &= ~(1 << SPI2X);
  } else {
    SPSR |= (1 << SPI2X);
  }
  SPCR &= ~((1 << SPR1) | (1 << SPR0));
  SPCR |= (sckRateID & 4 ? (1 << SPR1) : 0)
          | (sckRateID & 2 ? (1 << SPR0) : 0);
  #else // USE_SPI_LIB
  switch (sckRateID) {
    case 0:  settings = SPISettings(25000000, MSBFIRST, SPI_MODE0); break;
    case 1:  settings = SPISettings(4000000, MSBFIRST, SPI_MODE0); break;
    case 2:  settings = SPISettings(2000000, MSBFIRST, SPI_MODE0); break;
    case 3:  settings = SPISettings(1000000, MSBFIRST, SPI_MODE0); break;
    case 4:  settings = SPISettings(500000, MSBFIRST, SPI_MODE0); break;
    case 5:  settings = SPISettings(250000, MSBFIRST, SPI_MODE0); break;
    default: settings = SPISettings(125000, MSBFIRST, SPI_MODE0);
  }
  #endif // USE_SPI_LIB
  return true;
}
//------------------------------------------------------------------------------
// set the SPI clock frequency
uint8_t Sd2Card::setSpiClock(uint32_t clock) {
  #ifdef USE_SPI_LIB
  settings = SPISettings(clock, MSBFIRST, SPI_MODE0);
  return true;
  #else
  // CHGAME: fastest F_CPU/2^(id+1) that does not exceed the request.
  uint8_t id = 0;
  while (id < 6 && (RPGAME_SD_MAX_HZ >> id) > clock) {
    id++;
  }
  return setSckRate(id);
  #endif
}
//------------------------------------------------------------------------------
// wait for card to go not busy
uint8_t Sd2Card::waitNotBusy(unsigned int timeoutMillis) {
  unsigned int t0 = millis();
  unsigned int d;
  do {
    if (spiRec() == 0XFF) {
      return true;
    }
    d = millis() - t0;
  } while (d < timeoutMillis);
  return false;
}
//------------------------------------------------------------------------------
/** Wait for start block token */
uint8_t Sd2Card::waitStartBlock(void) {
  unsigned int t0 = millis();
  while ((status_ = spiRec()) == 0XFF) {
    unsigned int d = millis() - t0;
    if (d > SD_READ_TIMEOUT) {
      error(SD_CARD_ERROR_READ_TIMEOUT);
      goto fail;
    }
  }
  if (status_ != DATA_START_BLOCK) {
    error(SD_CARD_ERROR_READ);
    goto fail;
  }
  return true;

fail:
  chipSelectHigh();
  return false;
}
//------------------------------------------------------------------------------
/**
   Writes a 512 byte block to an SD card.

   \param[in] blockNumber Logical block to be written.
   \param[in] src Pointer to the location of the data to be written.
   \param[in] blocking If the write should be blocking.
   \return The value one, true, is returned for success and
   the value zero, false, is returned for failure.
*/
uint8_t Sd2Card::writeBlock(uint32_t blockNumber, const uint8_t* src, uint8_t blocking) {
  #if SD_PROTECT_BLOCK_ZERO
  // don't allow write to first block
  if (blockNumber == 0) {
    error(SD_CARD_ERROR_WRITE_BLOCK_ZERO);
    goto fail;
  }
  #endif  // SD_PROTECT_BLOCK_ZERO

  // use address if not SDHC card
  if (type() != SD_CARD_TYPE_SDHC) {
    blockNumber <<= 9;
  }
  if (cardCommand(CMD24, blockNumber)) {
    error(SD_CARD_ERROR_CMD24);
    goto fail;
  }
  if (!writeData(DATA_START_BLOCK, src)) {
    goto fail;
  }
  if (blocking) {
    // wait for flash programming to complete
    if (!waitNotBusy(SD_WRITE_TIMEOUT)) {
      error(SD_CARD_ERROR_WRITE_TIMEOUT);
      goto fail;
    }
    // response is r2 so get and check two bytes for nonzero
    if (cardCommand(CMD13, 0) || spiRec()) {
      error(SD_CARD_ERROR_WRITE_PROGRAMMING);
      goto fail;
    }
  }
  chipSelectHigh();
  return true;

fail:
  chipSelectHigh();
  return false;
}
//------------------------------------------------------------------------------
/** Write one data block in a multiple block write sequence */
uint8_t Sd2Card::writeData(const uint8_t* src, uint16_t crc) {
  return writeDataStart(src) && writeDataEnd(crc);
}
//------------------------------------------------------------------------------
/** CHSDtoUSB: first half of writeData(): wait for the card to finish the
    previous block, then send the token and start the block going out (by
    DMA on the CH32, so the caller can compute the CRC meanwhile). */
uint8_t Sd2Card::writeDataStart(const uint8_t* src) {
  // A failed block deselects the card; select it again so the caller can
  // go on to writeStop() (or retry) without knowing that.
  chipSelectLow();
  // wait for previous write to finish
  if (!waitNotBusy(SD_WRITE_TIMEOUT)) {
    error(SD_CARD_ERROR_WRITE_MULTIPLE);
    chipSelectHigh();
    return false;
  }
  spiSend(WRITE_MULTIPLE_TOKEN);
  #if SD_RP2040_FAST
  if (!s_dmaDead) {
    xferDmaStart(NULL, src, 512);
    s_txPending = 1;
    return true;
  }
  #endif
  for (uint16_t i = 0; i < 512; i++) {
    spiSend(src[i]);
  }
  return true;
}
//------------------------------------------------------------------------------
/** CHSDtoUSB: second half: the block's CRC16, then the card's verdict. */
uint8_t Sd2Card::writeDataEnd(uint16_t crc) {
  #if SD_RP2040_FAST
  if (s_txPending) {
    s_txPending = 0;
    if (!rxDmaWait(512)) {
      error(SD_CARD_ERROR_DMA);
      chipSelectHigh();
      return false;
    }
  }
  #endif
  spiSend(crc >> 8);
  spiSend(crc);
  status_ = spiRec();
  if ((status_ & DATA_RES_MASK) != DATA_RES_ACCEPTED) {
    error(SD_CARD_ERROR_WRITE);
    chipSelectHigh();
    return false;
  }
  return true;
}
//------------------------------------------------------------------------------
// send one block of data for write block or write multiple blocks
// CHGAME: writes are left on the polled path. They are rare in a game (save
// files), the card's programming time dwarfs the transfer anyway, and the
// fast spiSend() is already ~10x the old per-byte cost.
// CHSDtoUSB: now only writeBlock() comes here. Multi-block writes go through
// writeDataStart()/writeDataEnd(), by DMA: for a card reader the polled
// loop was the biggest single cost of a write.
uint8_t Sd2Card::writeData(uint8_t token, const uint8_t* src, uint16_t crc) {
  spiSend(token);
  for (uint16_t i = 0; i < 512; i++) {
    spiSend(src[i]);
  }
  spiSend(crc >> 8);  // CHSDtoUSB: the real CRC16 when the caller has one
  spiSend(crc);

  status_ = spiRec();
  if ((status_ & DATA_RES_MASK) != DATA_RES_ACCEPTED) {
    error(SD_CARD_ERROR_WRITE);
    chipSelectHigh();
    return false;
  }
  return true;
}
//------------------------------------------------------------------------------
/** Start a write multiple blocks sequence.

   \param[in] blockNumber Address of first block in sequence.
   \param[in] eraseCount The number of blocks to be pre-erased.

   \note This function is used with writeData() and writeStop()
   for optimized multiple block writes.

   \return The value one, true, is returned for success and
   the value zero, false, is returned for failure.
*/
uint8_t Sd2Card::writeStart(uint32_t blockNumber, uint32_t eraseCount) {
  #if SD_PROTECT_BLOCK_ZERO
  // don't allow write to first block
  if (blockNumber == 0) {
    error(SD_CARD_ERROR_WRITE_BLOCK_ZERO);
    goto fail;
  }
  #endif  // SD_PROTECT_BLOCK_ZERO
  // send pre-erase count
  if (cardAcmd(ACMD23, eraseCount)) {
    error(SD_CARD_ERROR_ACMD23);
    goto fail;
  }
  // use address if not SDHC card
  if (type() != SD_CARD_TYPE_SDHC) {
    blockNumber <<= 9;
  }
  if (cardCommand(CMD25, blockNumber)) {
    error(SD_CARD_ERROR_CMD25);
    goto fail;
  }
  return true;

fail:
  chipSelectHigh();
  return false;
}
//------------------------------------------------------------------------------
/** End a write multiple blocks sequence.

  \return The value one, true, is returned for success and
   the value zero, false, is returned for failure.
*/
uint8_t Sd2Card::writeStop(void) {
  chipSelectLow();   // CHSDtoUSB: also after a failed writeData()
  if (!waitNotBusy(SD_WRITE_TIMEOUT)) {
    goto fail;
  }
  spiSend(STOP_TRAN_TOKEN);
  if (!waitNotBusy(SD_WRITE_TIMEOUT)) {
    goto fail;
  }
  chipSelectHigh();
  return true;

fail:
  error(SD_CARD_ERROR_STOP_TRAN);
  chipSelectHigh();
  return false;
}
//------------------------------------------------------------------------------
/** CHSDtoUSB: turn on CRC checking in the card (CMD59). */
uint8_t Sd2Card::crcOn(uint8_t on) {
  uint8_t r = cardCommand(CMD59, on ? 1 : 0);
  chipSelectHigh();
  return r == R1_READY_STATE;
}
//------------------------------------------------------------------------------
/** CHSDtoUSB: does the card still answer? SEND_STATUS (CMD13) gets an R2
    from any initialised card, error bits or not. An empty slot leaves MISO
    on its pull-up, so no R1 (bit 7 clear) ever arrives; a card that has been
    swapped in is still in SD-bus mode and does not answer either. */
uint8_t Sd2Card::present(void) {
  uint8_t r = cardCommand(CMD13, 0);
  spiRec();                         // second byte of R2
  chipSelectHigh();
  return !(r & 0X80);
}
//------------------------------------------------------------------------------
/** Check if the SD card is busy

  \return The value one, true, is returned when is busy and
   the value zero, false, is returned for when is NOT busy.
*/
uint8_t Sd2Card::isBusy(void) {
  chipSelectLow();
  byte b = spiRec();
  chipSelectHigh();

  return (b != 0XFF);
}

//==============================================================================
// CHGAME: MULTI-BLOCK STREAMING (CMD18)
//==============================================================================
/*
 * How a CMD18 stream looks on MISO, and what the three state fields track:
 *
 *   CMD18 -> R1 | 0xFF.. 0xFE [512 data] [CRC CRC] | 0xFF.. 0xFE [512] [CRC CRC] | ...
 *                  ^ access    ^ token    ^ blockRemain_ ^ crcPending_
 *                    latency
 *
 * blockRemain_ counts the data bytes still to come in the current block;
 * 0 means the next thing on the wire is (CRC of the previous block, if
 * crcPending_) followed by 0xFF filler and the next start token. The
 * card keeps sending blocks until it gets CMD12.
 */
//------------------------------------------------------------------------------
/**
   CHGAME: begin streaming consecutive blocks from \a block.
   The card is left selected (holding SPI1) until readStop().
*/
uint8_t Sd2Card::readStart(uint32_t block) {
  // use address if not SDHC card
  if (type() != SD_CARD_TYPE_SDHC) {
    block <<= 9;
  }
  if (cardCommand(CMD18, block)) {
    error(SD_CARD_ERROR_CMD18);
    chipSelectHigh();
    return false;
  }
  streaming_ = 1;
  crcPending_ = 0;
  blockRemain_ = 0;
  return true;
}
//------------------------------------------------------------------------------
/**
   CHGAME: the next \a count bytes of the stream into \a dst, or skip them
   if \a dst is NULL. May be called any number of times with any sizes; it
   handles the token and CRC at each 512-byte boundary.
*/
uint8_t Sd2Card::readStream(uint8_t* dst, uint32_t count) {
  if (!streaming_) {
    return false;
  }
  while (count) {
    if (blockRemain_ == 0) {
      if (crcPending_) {
        spiRec();          // CRC is not checked (CRC is off in SPI mode)
        spiRec();
        crcPending_ = 0;
      }
      if (!waitStartBlock()) {
        streamAbort();
        return false;
      }
      blockRemain_ = 512;
    }
    uint16_t n = count < blockRemain_ ? (uint16_t)count : blockRemain_;
    if (!spiRecvBulk(dst, n)) {
      error(SD_CARD_ERROR_DMA);
      streamAbort();
      return false;
    }
    if (dst) {
      dst += n;
    }
    count -= n;
    blockRemain_ -= n;
    if (blockRemain_ == 0) {
      crcPending_ = 1;
    }
  }
  return true;
}
//------------------------------------------------------------------------------
/**
   CHSDtoUSB: the next whole block of the stream into \a dst, checked against
   the CRC16 the card sends with every block (with CRC checking on or off).
   On the CH32 the CRC is worked out while the block is still arriving, one
   byte behind the DMA, so the check costs little beyond the transfer.

   Call it on a block boundary. On a CRC mismatch the stream stays open at
   the next block (errorCode() is SD_CARD_ERROR_CRC); after any other
   failure it has been closed.
*/
uint8_t Sd2Card::readBlockChecked(uint8_t* dst) {
  if (!streaming_ || blockRemain_) {
    return false;
  }
  if (crcPending_) {
    spiRec();
    spiRec();
    crcPending_ = 0;
  }
  if (!waitStartBlock()) {
    streamAbort();
    return false;
  }
  uint16_t crc = 0;
  if (!spiRecvBulk(dst, 512)) {
    error(SD_CARD_ERROR_DMA);
    streamAbort();
    return false;
  }
  crc = crc16(dst, 512, 0);
  uint16_t wire = (uint16_t)(spiRec() << 8);
  wire |= spiRec();
  if (wire != crc) {
    error(SD_CARD_ERROR_CRC);
    return false;
  }
  return true;
}
//------------------------------------------------------------------------------
/**
   CHGAME: end the stream. The rest of a partly-read block is drained first
   so CMD12 always lands on a block boundary - the SD spec allows CMD12 at
   any point, but "between blocks" is the case every card handles, and it
   costs at most one block of reading. Callers that stop on a boundary pay
   nothing (see the tail cache in CHSpriteView.cpp).

   The card is left in its brief post-CMD12 busy state; the next command
   waits for that in cardCommand(), so the busy time overlaps whatever the
   sketch does next (an LCD flush, usually).
*/
uint8_t Sd2Card::readStop(void) {
  if (!streaming_) {
    return true;
  }
  streaming_ = 0;   // first: cardCommand() below must not recurse into us
  if (blockRemain_) {
    spiRecvBulk(NULL, blockRemain_);
    blockRemain_ = 0;
    crcPending_ = 1;
  }
  if (crcPending_) {
    spiRec();
    spiRec();
    crcPending_ = 0;
  }
  cardCommand(CMD12, 0);
  chipSelectHigh();
  // Any R1 (bit 7 clear) is accepted: some cards flag "out of range" when
  // the stop arrives as they pre-fetch past the last block, which is
  // harmless. Only no response at all is an error.
  if (status_ & 0X80) {
    error(SD_CARD_ERROR_CMD12);
    return false;
  }
  return true;
}
//------------------------------------------------------------------------------
/**
   CHGAME: abandon a stream after an error. The card is still in multi-block
   mode and will ignore anything but CMD12, so send that - but do not try to
   drain the current block first, because after a lost token or a failed DMA
   the byte position is unknown. waitStartBlock() may already have
   deselected the card; cardCommand() re-selects it.
*/
void Sd2Card::streamAbort(void) {
  streaming_ = 0;
  blockRemain_ = 0;
  crcPending_ = 0;
  uint8_t code = errorCode_;   // keep the original failure, not CMD12's
  uint8_t data = status_;      // CHSDtoUSB: ... and its token, for errorData()
  cardCommand(CMD12, 0);
  chipSelectHigh();
  errorCode_ = code;
  status_ = data;
}
//------------------------------------------------------------------------------
/**
   CHGAME: stream \a count blocks from \a block through a two-buffer
   pipeline. See the declaration in Sd2Card.h for the contract.

   Timeline for block k (DMA path):

     CPU:  wait token k | start DMA k -> buf[k&1] | fn(block k-1) | wait DMA k | CRC k
     SPI:  0xFF..0xFE   | <------------- 512 bytes of block k -------------->  | CRC

   fn() for block k-1 runs while block k is on the wire, so for a consumer
   faster than the bus, the whole read costs only its bus time.
*/
#ifdef SD_PROFILE
SdProfile sdProf;
/* Cycle timestamp: the core's millisecond count times 48000 plus SysTick,
   which counts HCLK (48 MHz) up to 47999 and resets every millisecond. */
uint32_t sdCycles() { return micros() * (F_CPU / 1000000u); }
  #define PROF_T(v)      uint32_t v = sdCycles()
  #define PROF_ADD(f, a) sdProf.f += sdCycles() - (a)
#else
  #define PROF_T(v)
  #define PROF_ADD(f, a)
#endif

uint8_t Sd2Card::readBlocksPipelined(uint32_t block, uint16_t count,
                                     uint8_t* buf0, uint8_t* buf1,
                                     BlockFn fn, void* user) {
  if (count == 0) {
    return true;
  }
  PROF_T(tStart);
  if (!readStart(block)) {
    return false;
  }
  PROF_ADD(cmd, tStart);
  uint8_t* bufs[2] = { buf0, buf1 };
  const uint8_t* pending = NULL;    // received, not yet handed to fn()

  for (uint16_t k = 0; k < count; k++) {
    uint8_t* cur = bufs[k & 1];

    PROF_T(tTok);
    if (!waitStartBlock()) {
      streamAbort();
      return false;
    }
    #ifdef SD_PROFILE
    if (k == 0) { PROF_ADD(firstToken, tTok); } else { PROF_ADD(nextTokens, tTok); }
    #endif
    #if SD_RP2040_FAST
    if (!s_dmaDead) {
      rxDmaStart(cur, 512);
      PROF_T(tFn);
      if (pending) {
        fn(pending, user);          // overlaps the DMA above
      }
      PROF_ADD(fn, tFn);
      PROF_T(tDma);
      if (!rxDmaWait(512)) {
        error(SD_CARD_ERROR_DMA);
        streamAbort();
        return false;
      }
      PROF_ADD(dmaWait, tDma);
    } else
    #endif
    {
      spiRecvBulk(cur, 512);
      if (pending) {
        fn(pending, user);
      }
    }
    spiRec();                       // CRC
    spiRec();
    pending = cur;
  }

  // Stop first, then consume the last block: the card's post-CMD12 busy
  // time runs in parallel with fn().
  blockRemain_ = 0;
  crcPending_ = 0;
  PROF_T(tStop);
  uint8_t ok = readStop();
  PROF_ADD(stop, tStop);
  PROF_T(tLast);
  fn(pending, user);
  PROF_ADD(lastFn, tLast);
  PROF_ADD(total, tStart);
  #ifdef SD_PROFILE
  sdProf.calls++;
  #endif
  return ok;
}

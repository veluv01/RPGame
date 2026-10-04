// FlashProbe - answers two questions before CHBlackjack relies on them:
//   1. Can a sketch (running in user mode after the RPGame bootloader hands
//      off) erase and program a flash page?
//   2. Does a page above the image survive a re-upload? The bootloader only
//      erases ceil(imageSize/256) pages from 0x3000, so it should.
//
// Upload, open the serial monitor, read the report. Upload again (unchanged)
// and read it again: "survived: yes" and a bumped run count answer (2).
//
// The routines mirror CH32SerialBoot/bootloader/src/flash.c: RAM-resident,
// controller addresses in the 0x08000000 alias, interrupts masked around each
// page operation because the vector table lives in flash.

#include <Arduino.h>

#define RAMFUNC __attribute__((section(".srodata.ramfunc.probe"), noinline))

static const uint32_t PAGE      = 256;
static const uint32_t PROBE_ADR = 0xF600;    // just below the 0xF700 metadata page
static const uint32_t MAGIC     = 0x31425250; // "PRB1"

extern "C" uint32_t _data_lma, _data_vma, _edata;

#define CR_STRT     0x00000040u
#define CR_FLOCK    0x00008000u
#define CR_PAGE_PG  0x00010000u
#define CR_PAGE_ER  0x00020000u
#define CR_BUF_LOAD 0x00040000u
#define CR_BUF_RST  0x00080000u
#define SR_BSY      0x00000001u
#define FKEY1       0x45670123u
#define FKEY2       0xCDEF89ABu
#define PROG(a)     ((a) + 0x08000000u)

RAMFUNC static uint32_t irqOff() {
  uint32_t old;
  __asm volatile ("csrr %0, 0x800" : "=r"(old));
  __asm volatile ("csrw 0x800, %0" : : "r"(old & ~0x88u));
  return old;
}
RAMFUNC static void irqRestore(uint32_t old) { __asm volatile ("csrw 0x800, %0" : : "r"(old)); }

RAMFUNC static void pageWrite(uint32_t addr, const uint32_t *words) {
  uint32_t irq = irqOff();
  FLASH->KEYR = FKEY1;     FLASH->KEYR = FKEY2;
  FLASH->MODEKEYR = FKEY1; FLASH->MODEKEYR = FKEY2;

  FLASH->CTLR |= CR_PAGE_ER;
  FLASH->ADDR = PROG(addr);
  FLASH->CTLR |= CR_STRT;
  while (FLASH->STATR & SR_BSY) {}
  FLASH->CTLR &= ~CR_PAGE_ER;

  FLASH->CTLR |= CR_PAGE_PG;
  FLASH->CTLR |= CR_BUF_RST;
  while (FLASH->STATR & SR_BSY) {}
  FLASH->CTLR &= ~CR_PAGE_PG;
  for (uint32_t i = 0; i < PAGE / 4; i++) {
    FLASH->CTLR |= CR_PAGE_PG;
    *(volatile uint32_t *)(PROG(addr) + i * 4) = words[i];
    FLASH->CTLR |= CR_BUF_LOAD;
    while (FLASH->STATR & SR_BSY) {}
    FLASH->CTLR &= ~CR_PAGE_PG;
  }
  FLASH->CTLR |= CR_PAGE_PG;
  FLASH->ADDR = PROG(addr);
  FLASH->CTLR |= CR_STRT;
  while (FLASH->STATR & SR_BSY) {}
  FLASH->CTLR &= ~CR_PAGE_PG;

  FLASH->CTLR |= CR_FLOCK;
  irqRestore(irq);
}

static uint32_t buf[PAGE / 4];

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 8000) { digitalWrite(LED_BUILTIN, (millis() / 100) & 1); }
  delay(200);

  uint32_t imgEnd = (uint32_t)&_data_lma + ((uint32_t)&_edata - (uint32_t)&_data_vma);
  Serial.printf("\r\nFlashProbe\r\nimage end 0x%x, probe page 0x%x\r\n", (unsigned)imgEnd, (unsigned)PROBE_ADR);
  if (imgEnd > PROBE_ADR) { Serial.println("image overlaps probe page, abort"); return; }

  const volatile uint32_t *pg = (const volatile uint32_t *)PROBE_ADR;
  bool had = (pg[0] == MAGIC && pg[1] == ~MAGIC);
  uint32_t runs = had ? pg[2] : 0;
  Serial.printf("before: %08x %08x %08x %08x -> survived: %s (runs %u)\r\n",
                (unsigned)pg[0], (unsigned)pg[1], (unsigned)pg[2], (unsigned)pg[3], had ? "yes" : "no", (unsigned)runs);
  Serial.flush();

  for (uint32_t i = 0; i < PAGE / 4; i++) buf[i] = 0xA5000000u | i;
  buf[0] = MAGIC; buf[1] = ~MAGIC; buf[2] = runs + 1;

  uint32_t us = micros();
  pageWrite(PROBE_ADR, buf);
  us = micros() - us;

  uint32_t bad = 0;
  for (uint32_t i = 0; i < PAGE / 4; i++) if (pg[i] != buf[i]) bad++;
  Serial.printf("write took %u us, verify: %s (%u bad words)\r\n", (unsigned)us, bad ? "FAIL" : "OK", (unsigned)bad);
  Serial.printf("after : %08x %08x %08x %08x\r\n", (unsigned)pg[0], (unsigned)pg[1], (unsigned)pg[2], (unsigned)pg[3]);
  Serial.println("done");
}

void loop() {
  digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
}

#pragma once
#include <cstdint>
#include <cstddef>
using uint = unsigned;
struct spi_hw_t { uint32_t cr0=0, cr1=0, dr=0, cpsr=0, icr=0; };
struct spi_inst_t { spi_hw_t hw; unsigned id; };
extern spi_inst_t mock_spis[2];
#define spi0 (&mock_spis[0])
#define spi1 (&mock_spis[1])
inline spi_hw_t *spi_get_hw(spi_inst_t *s) { return &s->hw; }
constexpr unsigned SPI_CPOL_0=0, SPI_CPHA_0=0, SPI_MSB_FIRST=0;
constexpr uint32_t SPI_SSPCR1_SSE_BITS=2, SPI_SSPICR_RORIC_BITS=1;
constexpr unsigned GPIO_FUNC_SPI=1, GPIO_OUT=1;
bool spi_is_busy(spi_inst_t *);
bool spi_is_readable(spi_inst_t *);
uint32_t spi_init(spi_inst_t *,uint32_t);
uint32_t spi_set_baudrate(spi_inst_t *,uint32_t);
void spi_set_format(spi_inst_t *,unsigned,unsigned,unsigned,unsigned);
int spi_write_read_blocking(spi_inst_t *,const uint8_t *,uint8_t *,size_t);
unsigned spi_get_dreq(spi_inst_t *,bool);
void gpio_set_function(unsigned,unsigned);
void gpio_pull_up(unsigned);
void gpio_init(unsigned);
void gpio_put(unsigned,bool);
void gpio_set_dir(unsigned,unsigned);
void tight_loop_contents();
struct dma_channel_config { bool readInc=false, writeInc=false; unsigned dreq=0; };
struct dma_channel_hw_t { uint32_t ctrl_trig=1; };
constexpr unsigned DMA_SIZE_8=0;
constexpr uint32_t DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS=0x80000000u, DMA_CH0_CTRL_TRIG_EN_BITS=1;
int dma_claim_unused_channel(bool);
void dma_channel_set_irq0_enabled(unsigned,bool);
void dma_channel_set_irq1_enabled(unsigned,bool);
dma_channel_config dma_channel_get_default_config(unsigned);
void channel_config_set_transfer_data_size(dma_channel_config *,unsigned);
void channel_config_set_read_increment(dma_channel_config *,bool);
void channel_config_set_write_increment(dma_channel_config *,bool);
void channel_config_set_dreq(dma_channel_config *,unsigned);
void dma_channel_configure(unsigned,const dma_channel_config *,void *,const void *,uint32_t,bool);
void dma_start_channel_mask(uint32_t);
bool dma_channel_is_busy(unsigned);
dma_channel_hw_t *dma_channel_hw_addr(unsigned);
void dma_channel_abort(unsigned);
inline void hw_clear_bits(uint32_t *v,uint32_t bits) { *v &= ~bits; }

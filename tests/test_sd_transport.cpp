// Exercise the real RP SD transport against a minimal SDK/card model.
#include <Arduino.h>
#include <MockSDK.h>
#include <Sd2Card.h>
#include <array>
#include <cassert>
#include <cstring>
#include <deque>
#include <vector>
#include <cstdio>

spi_inst_t mock_spis[2] = {{{},0},{{},1}};
static unsigned functions[48], pulls[48], directions[48], pinWrites[48];
static bool selected, present, badCrc, hangDma;
static uint32_t clockUs, flushWaits, dmaRuns, aborts, wireCalls[2];
static std::vector<uint8_t> command, written;
static std::deque<uint8_t> incoming;
static std::array<uint8_t,512> block;
static bool writeMode;
static unsigned writeLeft;
static uint16_t writeCrc;
static uint16_t crc16(const uint8_t *p,unsigned n) {
    uint16_t c=0;
    while(n--) { c^=uint16_t(*p++)<<8; for(unsigned i=0;i<8;++i)c=(c&0x8000)?uint16_t((c<<1)^0x1021):uint16_t(c<<1); }
    return c;
}
static void respond() {
    unsigned cmd=command[0]&63;
    switch(cmd) {
    case 0: incoming.push_back(1); break;
    case 8: for(uint8_t b: {1,0,0,1,0xaa}) incoming.push_back(b); break;
    case 55: incoming.push_back(1); break;
    case 41: case 23: case 59: incoming.push_back(0); break;
    case 58: for(uint8_t b: {0,0xc0,0,0,0}) incoming.push_back(b); break;
    case 17: case 18: {
        incoming.push_back(0);incoming.push_back(0xfe);
        for(uint8_t b:block)incoming.push_back(b);
        uint16_t c=crc16(block.data(),block.size())^(badCrc?1:0);
        incoming.push_back(c>>8);incoming.push_back(c&255);break;
    }
    case 12: incoming.push_back(0xff);incoming.push_back(0);break;
    case 24: case 25: incoming.push_back(0);writeMode=true;break;
    case 13: incoming.push_back(0);incoming.push_back(0);break;
    default: assert(false && "Unexpected card command");
    }
    command.clear();
}
static uint8_t transfer(spi_inst_t *s,uint8_t value) {
    ++wireCalls[s->id];assert(s->id==RPGAME_SD_SPI_BUS);
    assert((s->hw.cr0&15)==7); // every card operation uses 8-bit mode
    if(!selected || !present)return 0xff;
    if(!incoming.empty()){auto b=incoming.front();incoming.pop_front();return b;}
    if(writeLeft) {
        if(writeLeft>2)written.push_back(value);
        else writeCrc=uint16_t((writeCrc<<8)|value);
        if(!--writeLeft){assert(written.size()==512);assert(writeCrc==crc16(written.data(),written.size()));incoming.push_back(5);}
        return 0xff;
    }
    if(writeMode) {
        if(value==0xfe || value==0xfc){written.clear();writeCrc=0;writeLeft=514;}
        else if(value==0xfd)writeMode=false;
        return 0xff;
    }
    if(!command.empty() || (value&0xc0)==0x40) {
        command.push_back(value);if(command.size()==6)respond();
    }
    return 0xff;
}
uint32_t millis(){clockUs+=1000;return clockUs/1000;}
uint32_t micros(){clockUs+=250001;return clockUs;}
void gfx_wait(){++flushWaits;}
void tight_loop_contents(){}
bool spi_is_busy(spi_inst_t *){return false;}
bool spi_is_readable(spi_inst_t *){return false;}
uint32_t spi_init(spi_inst_t *s,uint32_t hz){s->hw.cr1=2;return spi_set_baudrate(s,hz);}
uint32_t spi_set_baudrate(spi_inst_t *s,uint32_t hz){s->hw.cpsr=hz;return hz;}
void spi_set_format(spi_inst_t *s,unsigned bits,unsigned,unsigned,unsigned){s->hw.cr0=(s->hw.cr0&~15u)|(bits-1);}
int spi_write_read_blocking(spi_inst_t *s,const uint8_t *tx,uint8_t *rx,size_t n){for(size_t i=0;i<n;++i)rx[i]=transfer(s,tx[i]);return int(n);}
unsigned spi_get_dreq(spi_inst_t *s,bool tx){return s->id*2+(tx?0:1);}
void gpio_set_function(unsigned p,unsigned f){assert(p<48);functions[p]=f;}
void gpio_pull_up(unsigned p){assert(p<48);pulls[p]=1;}
void gpio_init(unsigned p){assert(p<48);}
void gpio_set_dir(unsigned p,unsigned d){assert(p<48);directions[p]=d;}
void gpio_put(unsigned p,bool high){
    assert(p==PIN_SD_CS);++pinWrites[p];selected=!high;
    if(high){incoming.clear();command.clear();writeMode=false;writeLeft=0;}
}
struct Channel {dma_channel_config config;void *dst;const void *src;uint32_t count;dma_channel_hw_t hw;bool busy=false;};
static Channel channels[2];static unsigned allocated;
int dma_claim_unused_channel(bool){assert(allocated<2);return allocated++;}
void dma_channel_set_irq0_enabled(unsigned,bool){}
void dma_channel_set_irq1_enabled(unsigned,bool){}
dma_channel_config dma_channel_get_default_config(unsigned){return {};}
void channel_config_set_transfer_data_size(dma_channel_config *,unsigned size){assert(size==DMA_SIZE_8);}
void channel_config_set_read_increment(dma_channel_config *c,bool v){c->readInc=v;}
void channel_config_set_write_increment(dma_channel_config *c,bool v){c->writeInc=v;}
void channel_config_set_dreq(dma_channel_config *c,unsigned v){c->dreq=v;}
void dma_channel_configure(unsigned id,const dma_channel_config *c,void *dst,const void *src,uint32_t n,bool start){
    assert(!start);channels[id].config=*c;channels[id].dst=dst;channels[id].src=src;channels[id].count=n;channels[id].hw.ctrl_trig=1;
}
void dma_start_channel_mask(uint32_t mask){
    assert(mask==3);++dmaRuns;
    auto &rx=channels[0];auto &tx=channels[1];
    assert(rx.config.dreq==RPGAME_SD_SPI_BUS*2+1 && tx.config.dreq==RPGAME_SD_SPI_BUS*2);
    assert(rx.src==&mock_spis[RPGAME_SD_SPI_BUS].hw.dr && tx.dst==rx.src);
    assert(rx.count==tx.count);
    if(hangDma){rx.busy=tx.busy=true;hangDma=false;return;}
    for(uint32_t i=0;i<rx.count;++i){
        uint8_t value=static_cast<const uint8_t *>(tx.src)[tx.config.readInc?i:0];
        static_cast<uint8_t *>(rx.dst)[rx.config.writeInc?i:0]=transfer(&mock_spis[RPGAME_SD_SPI_BUS],value);
    }
}
bool dma_channel_is_busy(unsigned id){return channels[id].busy;}
dma_channel_hw_t *dma_channel_hw_addr(unsigned id){return &channels[id].hw;}
void dma_channel_abort(unsigned id){assert(!(channels[id].hw.ctrl_trig&DMA_CH0_CTRL_TRIG_EN_BITS));channels[id].busy=false;++aborts;}
static void busesIntact(const spi_hw_t &panel,const spi_hw_t &other){
    if(!RPGAME_SHARED_SPI)assert(!memcmp(&mock_spis[RPGAME_SPI_BUS].hw,&panel,sizeof panel));
    else {assert(mock_spis[RPGAME_SPI_BUS].hw.cr0==panel.cr0);assert(mock_spis[RPGAME_SPI_BUS].hw.cr1==panel.cr1);assert(mock_spis[RPGAME_SPI_BUS].hw.cpsr==panel.cpsr);}
    if(RPGAME_SHARED_SPI)assert(!memcmp(&mock_spis[1-RPGAME_SPI_BUS].hw,&other,sizeof other));
    assert(!selected);assert(pinWrites[PIN_LCD_CS]==0);
}
int main(){
    for(unsigned i=0;i<block.size();++i)block[i]=uint8_t(i*37+11);
    mock_spis[RPGAME_SPI_BUS].hw={15,2,0,12345,0};
    const auto panel=mock_spis[RPGAME_SPI_BUS].hw,other=mock_spis[1-RPGAME_SPI_BUS].hw;
    Sd2Card card;
    assert(!card.init(SPI_FULL_SPEED,PIN_SD_CS,2));busesIntact(panel,other);
    present=true;assert(card.init(SPI_FULL_SPEED,PIN_SD_CS));assert(card.type()==SD_CARD_TYPE_SDHC);assert(card.crcOn());
    assert(functions[RPGAME_SD_SPI_SCK]==GPIO_FUNC_SPI && functions[RPGAME_SD_SPI_MOSI]==GPIO_FUNC_SPI);
    assert(functions[RPGAME_SD_SPI_MISO]==GPIO_FUNC_SPI && pulls[RPGAME_SD_SPI_MISO]);
    if(!RPGAME_SHARED_SPI)assert(functions[RPGAME_SPI_SCK]==0 && functions[RPGAME_SPI_MOSI]==0 && functions[RPGAME_SPI_MISO]==0);
    assert(directions[PIN_SD_CS]==GPIO_OUT);busesIntact(panel,other);
    uint8_t output[512];assert(card.readBlock(5,output));assert(!memcmp(output,block.data(),512));busesIntact(panel,other);
    assert(card.readStart(7));assert(card.readBlockChecked(output));assert(!memcmp(output,block.data(),512));assert(card.readStop());busesIntact(panel,other);
    badCrc=true;assert(card.readStart(7));assert(!card.readBlockChecked(output));assert(card.errorCode()==SD_CARD_ERROR_CRC);assert(card.readStop());badCrc=false;busesIntact(panel,other);
    assert(card.writeStart(8,1));assert(card.writeDataStart(block.data()));assert(card.writeDataEnd(crc16(block.data(),512)));assert(card.writeStop());assert(written==std::vector<uint8_t>(block.begin(),block.end()));busesIntact(panel,other);
    hangDma=true;assert(!card.readBlock(9,output));assert(card.errorCode()==SD_CARD_ERROR_DMA);assert(aborts==2 && !card.dmaEnabled());busesIntact(panel,other);
    assert(card.readBlock(9,output));assert(!memcmp(output,block.data(),512));busesIntact(panel,other);
    assert(dmaRuns>=5 && flushWaits>0 && wireCalls[1-RPGAME_SD_SPI_BUS]==0);
    printf("SD transport: SPI%u, pins %u/%u/%u CS%u, %s, init/DMA/CRC/write/abort/fallback: PASS\n",RPGAME_SD_SPI_BUS,RPGAME_SD_SPI_SCK,RPGAME_SD_SPI_MOSI,RPGAME_SD_SPI_MISO,PIN_SD_CS,RPGAME_SHARED_SPI?"LCD register restoration":"LCD SPI untouched");
}

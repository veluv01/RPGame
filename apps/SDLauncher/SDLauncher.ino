// Resident RP2350 RISC-V menu. Packages are linked at flash + 512 KiB.
#include <RPGame.h>
#include <Fat.h>
#include <SdSpi.h>
#include <rpgame/Package.h>
#include <hardware/watchdog.h>
#include <hardware/structs/watchdog.h>
#include <hardware/irq.h>
#include <hardware/sync.h>
#include <hardware/resets.h>
#include <pico/bootrom.h>
#include <USB.h>

constexpr uint32_t LAUNCH_COOKIE=0x5250474c, CHAIN_FAILED=0x52504552;
struct Entry { char shortName[12]; fat::File file; char title[32]; };
static Entry entries[64];
static uint8_t count=0, selected=0;
static const char *notice="START: rescan";
static fat::Run fileRuns[64];
static uint8_t runCount;
alignas(4) static uint8_t block[512];

static void screen() {
    gfx_wait();gfx_clear(0);
    text35x2(4,3,"RPGame",1);text35(4,18,"RP2350  RISC-V",3);
    const uint8_t top=(selected/7)*7;
    for(uint8_t i=top;i<count && i<top+7;i++) {
        int y=30+int(i-top)*10;
        if(i==selected)gfx_fillRect(1,y-1,126,9,2);
        char name[30];memcpy(name,entries[i].title,29);name[29]=0;
        text35(4,y,name,i==selected?1:3);
    }
    if(!count)text35(4,36,"No GAMES/*.RPG files",1);
    text35(3,104,notice,3);
    text35(3,115,"A play  B installed",1);
    gfx_flush();gfx_wait();
}
static bool readFile(void *,uint32_t off,uint8_t *dst,uint32_t n) {
    while(n) {
        uint32_t take=512-off%512;if(take>n)take=n;
        if(!fat::read(fileRuns,runCount,off/512,block))return false;
        memcpy(dst,block+off%512,take);dst+=take;off+=take;n-=take;
    }
    return true;
}
static bool open(const fat::File &f) {
    int8_t rc=fat::runs(f,fileRuns,64,block);runCount=rc>0?uint8_t(rc):0;
    return runCount!=0;
}
static bool add(const char *name,const fat::File &f,bool dir,void *) {
    if(!dir && !memcmp(name+8,"RPG",3) && count<64) {
        Entry &e=entries[count++];memcpy(e.shortName,name,11);e.shortName[11]=0;e.file=f;
        memset(e.title,0,sizeof e.title);memcpy(e.title,name,8);
    }
    return true;
}
static void scan() {
    count=0;selected=0;notice="Reading SD card";screen();
    if(!sd::init()) {notice="SD card not ready";screen();return;}
    int8_t rc=fat::mount(block);
    if(rc!=fat::OK) {notice=rc==fat::E_EXFAT?"Use FAT16/FAT32":"No FAT volume";screen();return;}
    fat::File dir;
    if(fat::folder("GAMES      ",dir,block)!=fat::OK || fat::list(dir,add,nullptr,block)!=fat::OK) {
        notice="GAMES folder missing";screen();return;
    }
    for(uint8_t i=0;i<count;i++) {
        pkg::Header h;
        pkg::Reader r={nullptr,entries[i].file.size,readFile};
        if(open(entries[i].file) && pkg::header(r,h)==pkg::OK)memcpy(entries[i].title,h.title,32);
    }
    for(uint8_t i=1;i<count;i++)for(uint8_t j=i;j && strcmp(entries[j-1].title,entries[j].title)>0;j--) {
        Entry tmp=entries[j-1];entries[j-1]=entries[j];entries[j]=tmp;
    }
    notice="START: rescan";screen();
}
static void progress(uint32_t done,uint32_t total) {
    gfx_wait();gfx_fillRect(2,86,124,12,0);
    gfx_fillRect(2,88,(uint64_t(done)*124)/total,7,3);
    gfx_flushRectAsync(2,86,124,12);gfx_wait();
}
static void launch() {
    pkg::Header h;
    if(!pkg::installed(h) || !pkg::verifyInstalled()) {notice="No valid installed game";screen();return;}
    gfx_wait();audio::setOn(false);
    watchdog_hw->scratch[0]=LAUNCH_COOKIE;
    watchdog_hw->scratch[1]=~LAUNCH_COOKIE;
    watchdog_hw->scratch[2]=h.payloadCrc;
    watchdog_hw->scratch[3]=0;
    watchdog_reboot(0,0,0);
    for(;;) {}
}
static void play() {
    if(!count)return;
    if(!open(entries[selected].file)){notice="File chain too fragmented";screen();return;}
    notice="Checking, then installing";screen();
    pkg::Reader r={nullptr,entries[selected].file.size,readFile};
    pkg::Error e=pkg::install(r,progress);
    if(e!=pkg::OK){notice=pkg::message(e);screen();return;}
    launch();
}
void setup() {
    // A fresh reset reaches here before LCD, SD or sequencer initialization.
    // The Arduino core has started USB; disconnect it before handing off.
    bool requested=watchdog_hw->scratch[0]==LAUNCH_COOKIE && watchdog_hw->scratch[1]==~LAUNCH_COOKIE;
    uint32_t wantedCrc=watchdog_hw->scratch[2];
    watchdog_hw->scratch[0]=watchdog_hw->scratch[1]=watchdog_hw->scratch[2]=0;
    if(requested) {
        pkg::Header h;
        if(pkg::installed(h) && h.payloadCrc==wantedCrc && pkg::verifyInstalled()) {
            USB.disconnect();save_and_disable_interrupts();
            irq_set_enabled(USB.usbTaskIRQ,false);
            reset_block_mask(1u << RESET_USBCTRL);
            for(uint i=0;i<NUM_IRQS;i++)irq_set_enabled(i,false);
            alignas(16) static uint8_t work[4096];
            int rc=rom_chain_image(work,sizeof work,0x10000000u+nv::APP_OFFSET,h.bytes);
            watchdog_hw->scratch[3]=CHAIN_FAILED;
            watchdog_hw->scratch[2]=uint32_t(rc);
            watchdog_reboot(0,0,0);for(;;) {}
        }
    }
    rpgame.boot();rpgame.startExits=false;rpgame.setFrameRate(30);
    gfx_begin(GFX_DIV2,GFX_12BPP);
    gfx_setPaletteEntry(0,0x0044);gfx_setPaletteEntry(1,0xefff);gfx_setPaletteEntry(2,0x220c);gfx_setPaletteEntry(3,0x667c);
    if(watchdog_hw->scratch[3]==CHAIN_FAILED) {
        watchdog_hw->scratch[3]=watchdog_hw->scratch[2]=0;
        notice="ROM launch failed";screen();
    } else scan();
}
void loop() {
    if(!rpgame.nextFrame())return;rpgame.pollButtons();
    if(rpgame.pressed(SELECT_BUTTON|B_BUTTON))rpgame_enter_bootloader();
    if(rpgame.justPressed(START_BUTTON)){scan();return;}
    if(rpgame.justPressed(B_BUTTON)){launch();return;}
    if(rpgame.justPressed(A_BUTTON)){play();return;}
    if(!count)return;
    uint8_t before=selected;
    if(rpgame.repeat(UP_BUTTON))selected=selected?selected-1:count-1;
    if(rpgame.repeat(DOWN_BUTTON))selected=(selected+1)%count;
    if(rpgame.repeat(LEFT_BUTTON))selected=selected>=7?selected-7:0;
    if(rpgame.repeat(RIGHT_BUTTON))selected=selected+7<count?selected+7:count-1;
    if(selected!=before)screen();
}

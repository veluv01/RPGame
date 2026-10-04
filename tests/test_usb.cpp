#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <tusb-msc.h>
#include "../apps/RPSDtoUSB/src/usb/UsbMsc.h"

extern "C" {
uint8_t tud_msc_get_maxlun_cb();
bool tud_msc_test_unit_ready_cb(uint8_t);
bool tud_msc_start_stop_cb(uint8_t,uint8_t,bool,bool);
int32_t tud_msc_read10_cb(uint8_t,uint32_t,uint32_t,void*,uint32_t);
int32_t tud_msc_write10_cb(uint8_t,uint32_t,uint32_t,uint8_t*,uint32_t);
void tud_msc_read10_complete_cb(uint8_t);
void tud_msc_write10_complete_cb(uint8_t);
int32_t tud_msc_scsi_cb(uint8_t,const uint8_t[16],void*,uint16_t);
}
static std::array<std::array<uint8_t,512>,4> disk;
static uint32_t readPos,writePos;
static int readStarts,readStops,writeStarts,writeStops;
static bool failRead,failWrite,failStart;
static int commandStarts, commandEnds;
static void observeStart(bool,uint32_t) { ++commandStarts; }
static void observeEnd() { ++commandEnds; }
static uint32_t capacity() { return disk.size(); }
static bool readStart(uint32_t lba) { ++readStarts; readPos=lba; return !failStart; }
static bool readBlock(uint8_t *dst) {
    if(failRead) return false;
    memcpy(dst,disk.at(readPos++).data(),512); return true;
}
static bool readStop() { ++readStops; return true; }
static bool writeStart(uint32_t lba,uint32_t) { ++writeStarts; writePos=lba; return !failStart; }
static bool writeBlock(const uint8_t *src) {
    if(failWrite) return false;
    memcpy(disk.at(writePos++).data(),src,512); return true;
}
static bool writeStop() { ++writeStops; return true; }
static void clearMediaChange() {
    usbmsc::mediaChanged();
    assert(!tud_msc_test_unit_ready_cb(0));
    assert(senseKey==SCSI_SENSE_UNIT_ATTENTION);
    assert(tud_msc_test_unit_ready_cb(0));
}

int main() {
    for(size_t sector=0;sector<disk.size();++sector)
        for(size_t i=0;i<512;++i) disk[sector][i]=uint8_t(sector*19+i);
    const usbmsc::BlockDevice dev={capacity,readStart,readBlock,readStop,writeStart,writeBlock,writeStop};
    usbmsc::observe(observeStart,observeEnd);
    usbmsc::begin(dev);
    assert(tud_msc_get_maxlun_cb()==1); // TinyUSB expects a count, not the max index.
    assert(tud_msc_test_unit_ready_cb(0));
    uint8_t packet[64];
    for(uint32_t offset=0;offset<512;offset+=64) {
        assert(tud_msc_read10_cb(0,1,offset,packet,64)==64);
        assert(!memcmp(packet,disk[1].data()+offset,64));
    }
    assert(readStarts==1 && readStops==1);
    assert(commandStarts==1 && commandEnds==0);
    tud_msc_read10_complete_cb(0);
    assert(commandEnds==1);
    assert(tud_msc_read10_cb(0,1,0,packet,64)==64);
    tud_msc_read10_complete_cb(0);
    assert(readStarts==2 && commandStarts==2 && commandEnds==2);

    std::array<uint8_t,512> replacement;
    replacement.fill(0xA5);
    const auto old=disk[2];
    for(uint32_t offset=0;offset<512;offset+=64) {
        assert(tud_msc_write10_cb(0,2,offset,replacement.data()+offset,64)==64);
        if(offset<448) assert(disk[2]==old); // No partial-sector writes.
    }
    assert(disk[2]==replacement);
    assert(writeStarts==1 && writeStops==1);
    tud_msc_write10_complete_cb(0);

    // Abort one partial write, then start another to the same sector.
    memset(packet,0xCC,64);
    assert(tud_msc_write10_cb(0,2,0,packet,64)==64);
    assert(disk[2]==replacement);
    replacement.fill(0x77);
    for(uint32_t offset=0;offset<512;offset+=64)
        assert(tud_msc_write10_cb(0,2,offset,replacement.data()+offset,64)==64);
    assert(disk[2]==replacement);

    // Both directions reject overflow and out-of-range blocks before card I/O.
    const int starts=readStarts+writeStarts;
    assert(tud_msc_read10_cb(0,UINT32_MAX,0,packet,64)==-1);
    assert(tud_msc_write10_cb(0,4,0,packet,64)==-1);
    assert(readStarts+writeStarts==starts);

    usbmsc::setReadOnly(true);
    assert(!tud_msc_test_unit_ready_cb(0)); // Policy change reports unit attention.
    assert(tud_msc_write10_cb(0,0,0,replacement.data(),512)==-1);
    assert(senseKey==SCSI_SENSE_DATA_PROTECT);
    usbmsc::setReadOnly(false); clearMediaChange();

    // Failed reads/writes and failed starts always close the card transaction.
    failRead=true;
    assert(tud_msc_read10_cb(0,3,0,packet,64)==-1);
    assert(readStarts==readStops && !usbmsc::busy());
    failRead=false; failWrite=true;
    assert(tud_msc_write10_cb(0,3,0,replacement.data(),512)==-1);
    assert(writeStarts==writeStops && !usbmsc::busy());
    failWrite=false; failStart=true;
    assert(tud_msc_read10_cb(0,3,0,packet,64)==-1);
    assert(readStarts==readStops);
    failStart=false;

    // Card replacement invalidates cached blocks.
    assert(tud_msc_read10_cb(0,1,0,packet,64)==64);
    disk[1].fill(0x33); clearMediaChange();
    assert(tud_msc_read10_cb(0,1,0,packet,64)==64);
    for(auto v:packet) assert(v==0x33);
    tud_msc_start_stop_cb(0,0,false,true);
    assert(tud_msc_read10_cb(0,1,0,packet,64)==-1);
    tud_msc_start_stop_cb(0,0,true,true);
    assert(tud_msc_test_unit_ready_cb(0));

    uint8_t command[16]={0x35};
    assert(tud_msc_scsi_cb(0,command,nullptr,0)==0);
    command[0]=0xFF;
    assert(tud_msc_scsi_cb(0,command,nullptr,0)==-1);
    assert(senseKey==SCSI_SENSE_ILLEGAL_REQUEST);
    puts("USB sector, abort/restart, range, read-only, failure cleanup, media and SCSI checks passed");
}

// A real staged RPG file installed through the portable FAT extent reader.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <Fat.h>
#include <SdSpi.h>
#include <rpgame/Package.h>
static FILE *card;
static std::vector<uint8_t> flash(nv::FLASH_BYTES,0xff);
static fat::Run runs[64];static uint8_t runCount;
alignas(4) static uint8_t sector[512];
namespace sd {
bool init(){return true;}
bool read(uint32_t lba,uint8_t *dst){return !fseek(card,uint64_t(lba)*512,SEEK_SET) && fread(dst,1,512,card)==512;}
}
const uint8_t *rpgame_host_flash_read(uint32_t off){return flash.data()+off;}
bool rpgame_host_flash_write(uint32_t off,const uint8_t *p,uint32_t n){
    assert(off%4096==0 && off>=nv::APP_OFFSET && off<nv::WRITABLE_END);
    memset(flash.data()+off,0xff,4096);memcpy(flash.data()+off,p,n);return true;
}
static bool reader(void *,uint32_t off,uint8_t *dst,uint32_t n){
    while(n){uint32_t take=512-off%512;if(take>n)take=n;
        if(!fat::read(runs,runCount,off/512,sector))return false;
        memcpy(dst,sector+off%512,take);off+=take;dst+=take;n-=take;
    }return true;
}
int main(int argc,char **argv){
    assert(argc==2);card=fopen(argv[1],"rb");assert(card);
    assert(fat::mount(sector)==fat::OK);fat::File folder,file;
    assert(fat::folder("GAMES      ",folder,sector)==fat::OK);
    assert(fat::match(folder,"FOUR    RPG",0,file,nullptr,sector)==fat::OK);
    int8_t count=fat::runs(file,runs,64,sector);assert(count>0);runCount=count;
    pkg::Reader r={nullptr,file.size,reader};pkg::Header h;
    assert(pkg::validate(r,h)==pkg::OK);assert(!strcmp(h.title,"Four"));
    assert(pkg::install(r)==pkg::OK);assert(pkg::verifyInstalled());
    assert(flash[nv::APP_OFFSET-1]==0xff && flash[nv::SAVE_A]==0xff);
    printf("SD package: %u extents, %lu bytes, FAT lookup/validation/install/verify: PASS\n",runCount,(unsigned long)h.bytes);
    fclose(card);
}

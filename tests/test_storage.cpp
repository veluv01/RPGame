// Actual package/save implementation under interrupted flash operations.
#include <array>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <rpgame/Package.h>
#include <rpgame/Save.h>
static std::vector<uint8_t> disk(nv::FLASH_BYTES,0xff);
alignas(4) static uint8_t scratch[2048];
static int writes, failAt=-1, partial;
const uint8_t *rpgame_host_flash_read(uint32_t off) { return disk.data()+off; }
bool rpgame_host_flash_write(uint32_t off,const uint8_t *src,uint32_t n) {
    assert(off%4096==0 && off>=nv::APP_OFFSET && off<nv::WRITABLE_END);
    int index=writes++;
    if(failAt>=0 && index>=failAt) {
        memset(disk.data()+off,0xff,4096);
        if(index==failAt)memcpy(disk.data()+off,src,n<uint32_t(partial)?n:partial);
        return false;
    }
    memset(disk.data()+off,0xff,4096);memcpy(disk.data()+off,src,n);return true;
}
void gfx_wait() {}
uint8_t *gfx_chunkScratch() { return scratch; }
struct Source { std::vector<uint8_t> bytes; int reads=0,fail=-1; };
static bool read(void *ctx,uint32_t off,uint8_t *dst,uint32_t n) {
    auto &s=*static_cast<Source*>(ctx);
    if(s.reads++==s.fail || uint64_t(off)+n>s.bytes.size())return false;
    memcpy(dst,s.bytes.data()+off,n);return true;
}
static Source image(uint8_t seed) {
    Source s;s.bytes.resize(512+8192);
    auto *h=reinterpret_cast<pkg::Header*>(s.bytes.data());
    memset(h,0,sizeof *h);memcpy(h->magic,"RPGAME1\0",8);
    h->version=1;h->family=pkg::FAMILY;h->layout=pkg::LAYOUT;h->offset=nv::APP_OFFSET;h->bytes=8192;
    strcpy(h->title,"Storage test");
    for(size_t i=512;i<s.bytes.size();i++)s.bytes[i]=uint8_t(i*7+seed);
    const uint32_t rom[]={0xffffded3,0x11010142,0x344,0x10080020,0x20082000,0x4ff,8172,0xab123579};
    const uint32_t end[]={0xffffded3,0x1fe,0x1ff,uint32_t(-8172),0xab123579};
    memcpy(s.bytes.data()+512,rom,sizeof rom);memcpy(s.bytes.data()+512+8172,end,sizeof end);
    h->payloadCrc=pkg::crc32(s.bytes.data()+512,8192);h->crc=pkg::crc32(s.bytes.data(),508);
    return s;
}
static pkg::Error install(Source &s) { pkg::Reader r={&s,uint32_t(s.bytes.size()),read};return pkg::install(r); }
int main() {
    assert(pkg::crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926);
    assert(!nv::writeSector(0,scratch,1));assert(!nv::writeSector(nv::APP_OFFSET+1,scratch,1));
    assert(!nv::writeSector(nv::WRITABLE_END,scratch,1));
    assert(!nv::writeSector(nv::SAVE_A,scratch,4097));assert(writes==0);
    Source a=image(1),b=image(2);assert(install(a)==pkg::OK);assert(pkg::verifyInstalled());
    auto valid=disk;int before=writes;a.reads=0;assert(install(a)==pkg::OK);assert(writes==before);
    for(int field:{0,8,12,16,20,24,32,508,512,4096}) {
        Source bad=b;bad.bytes[field]^=0x80;before=writes;
        assert(install(bad)!=pkg::OK);assert(writes==before && disk==valid);
    }
    Source wrongRom=b;
    wrongRom.bytes[516]^=1;
    auto *wrongHeader=reinterpret_cast<pkg::Header*>(wrongRom.bytes.data());
    wrongHeader->payloadCrc=pkg::crc32(wrongRom.bytes.data()+512,8192);
    wrongHeader->crc=pkg::crc32(wrongRom.bytes.data(),508);
    before=writes;assert(install(wrongRom)==pkg::IMAGE && writes==before);
    Source shortFile=b;shortFile.bytes.pop_back();assert(install(shortFile)==pkg::LENGTH);
    Source bad=b;auto *h=reinterpret_cast<pkg::Header*>(bad.bytes.data());h->bytes=0xfffff000;
    assert(install(bad)==pkg::LENGTH);
    int scenarios=0;
    for(int cut=0;cut<4;cut++)for(int prefix:{0,4,64,127,128,512,4096}) {
        disk=valid;writes=0;failAt=cut;partial=prefix;Source interrupted=b;
        assert(install(interrupted)==pkg::FLASH);
        pkg::Header installed;
        if(pkg::installed(installed))assert(pkg::verifyInstalled());
        assert(memcmp(disk.data(),valid.data(),nv::APP_OFFSET)==0);
        assert(memcmp(disk.data()+nv::SAVE_A,valid.data()+nv::SAVE_A,8192)==0);
        assert(memcmp(disk.data()+nv::WRITABLE_END,valid.data()+nv::WRITABLE_END,nv::FLASH_BYTES-nv::WRITABLE_END)==0);
        scenarios++;
    }
    disk=valid;writes=0;failAt=-1;b.reads=0;b.fail=20; // second install payload read, after validation
    assert(install(b)==pkg::IO);pkg::Header h2;assert(!pkg::installed(h2));
    b.fail=-1;b.reads=0;assert(install(b)==pkg::OK);assert(pkg::verifyInstalled());
    disk[nv::APP_OFFSET+900]^=1;assert(!pkg::verifyInstalled());
    // Save sequence and CRC fallback use the real Save.cpp record format.
    memset(disk.data()+nv::SAVE_A,0xff,8192);
    uint32_t value=0,loaded=0;constexpr uint32_t magic=save::magic("RPST");
    for(value=1;value<=100;value++){assert(save::store(magic,1,value));assert(save::load(magic,1,loaded));assert(value==loaded);}
    assert(!save::load(magic,2,loaded));assert(!save::load(save::magic("NONE"),1,loaded));
    disk[nv::SAVE_A+10]^=1;assert(save::load(magic,1,loaded) && loaded==99);
    assert(!save::write(magic,1,save::MAX_DATA+1));
    // A torn next save leaves the preceding record loadable after restart.
    memset(disk.data()+nv::SAVE_A,0xff,4096);
    assert(save::load(magic,1,loaded) && loaded==99);
    assert(pkg::verifyInstalled()==false);
    printf("storage: format/CRC/bounds, %d interrupted installs, 100 saves, CRC fallback: PASS\n",scenarios);
}

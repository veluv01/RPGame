#include "Package.h"
#include <string.h>
namespace pkg {
static uint32_t update(uint32_t c, const uint8_t *p, uint32_t n) {
    while (n--) { c ^= *p++; for (int i=0;i<8;i++) c=(c>>1)^(0xedb88320u & (0u-(c&1))); }
    return c;
}
uint32_t crc32(const uint8_t *p,uint32_t n) { return ~update(~0u,p,n); }
const char *message(Error e) {
    static const char *const m[]={"OK","SD read failed","Not an RPG file","Wrong CPU/layout",
        "Bad file length","Bad header CRC","Bad image CRC","Flash write failed","Bad ROM image metadata"};
    return m[e];
}
Error header(const Reader &r, Header &h) {
    if (r.size < sizeof h || !r.read || !r.read(r.ctx,0,reinterpret_cast<uint8_t*>(&h),sizeof h)) return IO;
    if (memcmp(h.magic,"RPGAME1\0",8) || h.version!=1) return FORMAT;
    if (h.family!=FAMILY || h.layout!=LAYOUT || h.offset!=nv::APP_OFFSET) return TARGET;
    if (!h.bytes || h.bytes%nv::SECTOR || h.bytes>nv::APP_END-nv::APP_OFFSET || r.size!=512+h.bytes) return LENGTH;
    if (!memchr(h.title,0,sizeof h.title) || !memchr(h.author,0,sizeof h.author) ||
        !memchr(h.release,0,sizeof h.release)) return FORMAT;
    return crc32(reinterpret_cast<const uint8_t*>(&h),508)==h.crc ? OK : HEADER_CRC;
}
Error validate(const Reader &r, Header &h) {
    Error e=header(r,h); if(e!=OK) return e;
    alignas(4) uint8_t b[512]; uint32_t c=~0u;
    for(uint32_t off=0;off<h.bytes;off+=sizeof b) {
        if(!r.read(r.ctx,512+off,b,sizeof b)) return IO;
        c=update(c,b,sizeof b);
    }
    if(~c!=h.payloadCrc)return PAYLOAD_CRC;
    alignas(4) uint32_t w[8],end[5];
    if(!r.read(r.ctx,512,reinterpret_cast<uint8_t*>(w),sizeof w))return IO;
    uint32_t origin=0x10000000u+nv::APP_OFFSET;
    if(w[0]!=0xffffded3 || w[1]!=0x11010142 || w[2]!=0x344 || w[4]!=0x20082000 ||
       w[5]!=0x4ff || w[7]!=0xab123579 || w[3]<origin || w[3]>=origin+h.bytes || (w[3]&1) ||
       w[6]<32 || w[6]>h.bytes-20 || (w[6]&3))return IMAGE;
    if(!r.read(r.ctx,512+w[6],reinterpret_cast<uint8_t*>(end),sizeof end))return IO;
    return end[0]==0xffffded3 && end[1]==0x1fe && end[2]==0x1ff && end[3]==0u-w[6] && end[4]==0xab123579 ? OK : IMAGE;
}
struct Meta {
    uint32_t magic,state,generation,bytes,payloadCrc,headerCrc;
    char title[32]; uint8_t reserved[68]; uint32_t crc;
};
static_assert(sizeof(Meta)==128,"Metadata size");
static bool good(const Meta &m) { return m.magic==0x52504d31 && m.crc==crc32(reinterpret_cast<const uint8_t*>(&m),124); }
static const Meta *latest(uint32_t &offset) {
    const auto *a=reinterpret_cast<const Meta*>(nv::read(nv::META_A));
    const auto *b=reinterpret_cast<const Meta*>(nv::read(nv::META_B));
    bool va=good(*a),vb=good(*b);
    if(va && (!vb || int32_t(a->generation-b->generation)>0)) {offset=nv::META_A;return a;}
    if(vb) {offset=nv::META_B;return b;}
    offset=nv::META_B; return nullptr;
}
static bool commit(Meta &m,uint32_t offset) {
    m.crc=crc32(reinterpret_cast<const uint8_t*>(&m),124);
    return nv::writeSector(offset,reinterpret_cast<const uint8_t*>(&m),sizeof m);
}
bool installed(Header &h) {
    uint32_t off;const Meta *m=latest(off);
    if(!m || m->state!=2 || !m->bytes || m->bytes%nv::SECTOR || m->bytes>nv::APP_END-nv::APP_OFFSET) return false;
    memset(&h,0,sizeof h);h.bytes=m->bytes;h.payloadCrc=m->payloadCrc;h.crc=m->headerCrc;
    memcpy(h.title,m->title,sizeof h.title);h.title[31]=0;return true;
}
bool verifyInstalled() {
    Header h;return installed(h) && crc32(nv::read(nv::APP_OFFSET),h.bytes)==h.payloadCrc;
}
Error install(const Reader &r,Progress progress) {
    Header h;Error e=validate(r,h);if(e!=OK)return e;
    Header old;
    if(installed(old) && old.bytes==h.bytes && old.payloadCrc==h.payloadCrc && old.crc==h.crc && verifyInstalled()) return OK;
    uint32_t off;const Meta *last=latest(off);
    Meta m={};m.magic=0x52504d31;m.state=1;m.generation=last ? last->generation+1 : 1;
    m.bytes=h.bytes;m.payloadCrc=h.payloadCrc;m.headerCrc=h.crc;memcpy(m.title,h.title,32);
    uint32_t pending=off==nv::META_A ? nv::META_B : nv::META_A;
    if(!commit(m,pending)) return FLASH;
    alignas(4) static uint8_t sector[nv::SECTOR];
    for(uint32_t i=0;i<h.bytes;i+=nv::SECTOR) {
        if(!r.read(r.ctx,512+i,sector,nv::SECTOR)) return IO;
        if(!nv::writeSector(nv::APP_OFFSET+i,sector,nv::SECTOR)) return FLASH;
        if(progress)progress(i+nv::SECTOR,h.bytes);
    }
    if(crc32(nv::read(nv::APP_OFFSET),h.bytes)!=h.payloadCrc) return PAYLOAD_CRC;
    m.state=2;m.generation++;
    return commit(m,off) ? OK : FLASH;
}
}

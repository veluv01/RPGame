#pragma once
#include <stdint.h>
#define TUD_MSC_DESC_LEN 23
#define TUD_MSC_DESCRIPTOR(number, str, out, in, size) 9,4,number,0,2,8,6,80,str,7,5,out,2,size,0,0,7,5,in,2,size,0,0
enum { SCSI_SENSE_NOT_READY=2, SCSI_SENSE_MEDIUM_ERROR=3,
       SCSI_SENSE_ILLEGAL_REQUEST=5, SCSI_SENSE_UNIT_ATTENTION=6,
       SCSI_SENSE_DATA_PROTECT=7 };
inline uint8_t senseKey, senseCode;
inline void tud_msc_set_sense(uint8_t, uint8_t key, uint8_t code, uint8_t) {
    senseKey=key; senseCode=code;
}
inline bool tud_mounted() { return true; }
inline bool tud_suspended() { return false; }
inline bool tud_cdc_connected() { return false; }
inline int tud_cdc_available() { return 0; }
inline int tud_cdc_read_char() { return -1; }
inline int tud_cdc_write_available() { return 0; }
inline void tud_cdc_write(const void *, uint32_t) {}
inline void tud_cdc_write_flush() {}
inline void tud_disconnect() {}
inline void tud_task() {}

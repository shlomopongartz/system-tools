// Copyright (c) 2014 Quanta Research Cambridge, Inc.

// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//
// FTDI interface documented at:
//     http://www.ftdichip.com/Documents/AppNotes/AN2232C-01_MPSSE_Cmnd.pdf
// Xilinx Series7 Configuation documented at:
//     ug470_7Series_Config.pdf
// ARM JTAG-DP registers documented at:
//     DDI0314H_coresight_components_trm.pdf
// ARM DPACC/APACC programming documented at:
//     IHI0031C_debug_interface_as.pdf

// for using libftdi.so
//#define USE_LIBFTDI
#define USE_CORTEX_ADI

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <libusb.h>

#define USB_TIMEOUT    5000
#define ENDPOINT_IN     0x02
#define ENDPOINT_OUT    0x81
#define USB_CHUNKSIZE   4096
#define USB_INDEX                     0

#define USBSIO_RESET                     0 /* Reset the port */
#define USBSIO_RESET_PURGE_RX            1
#define USBSIO_RESET_PURGE_TX            2
#define USBSIO_SET_BAUD_RATE             3 /* Set baud rate */
#define USBSIO_SET_LATENCY_TIMER_REQUEST 9
#define USBSIO_SET_BITMODE_REQUEST       11

static libusb_device_handle *usbhandle = NULL;
static FILE *logfile;
static int logall = 1;
static int datafile_fd = -1;
static void openlogfile(void)
{
    if (!logfile)
        logfile = fopen("/tmp/xx.logfile2", "w");
    if (datafile_fd < 0)
        datafile_fd = creat("/tmp/xx.datafile2", 0666);
}
#include "dumpdata.h"
#ifdef USE_LIBFTDI
//#include "ftdi_reference.h"
#include "ftdi.h"
#else
#define MPSSE_WRITE_NEG 0x01   /* Write TDI/DO on negative TCK/SK edge*/
#define MPSSE_BITMODE   0x02   /* Write bits, not bytes */
#define MPSSE_READ_NEG  0x04   /* Sample TDO/DI on negative TCK/SK edge */
#define MPSSE_LSB       0x08   /* LSB first */
#define MPSSE_DO_WRITE  0x10   /* Write TDI/DO */
#define MPSSE_DO_READ   0x20   /* Read TDO/DI */
#define MPSSE_WRITE_TMS 0x40   /* Write TMS/CS */
#define SET_BITS_LOW   0x80
#define SET_BITS_HIGH  0x82
#define LOOPBACK_END   0x85
#define TCK_DIVISOR    0x86
#define DIS_DIV_5       0x8a
#define CLK_BYTES       0x8f
#define SEND_IMMEDIATE 0x87

struct ftdi_context {
};
struct ftdi_transfer_control {
};
static unsigned char usbreadbuffer[USB_CHUNKSIZE];
#define ftdi_deinit(A)
#define ftdi_transfer_data_done(A) (void)(A)
#define ftdi_set_usbdev(A,B)
#define ftdi_write_data_submit(A, B, C) (ftdi_write_data((A), (B), (C)), NULL)
static int ftdi_write_data(struct ftdi_context *ftdi, const unsigned char *buf, int size)
{
    int actual_length;
formatwrite(1, buf, size, "WRITE");
    if (libusb_bulk_transfer(usbhandle, ENDPOINT_IN, (unsigned char *)buf, size, &actual_length, USB_TIMEOUT) < 0)
        printf( "usb bulk write failed");
    return actual_length;
}
static int ftdi_read_data(struct ftdi_context *ftdi, unsigned char *buf, int size)
{
    int actual_length = 1;
    do {
        int ret = libusb_bulk_transfer (usbhandle, ENDPOINT_OUT, usbreadbuffer, USB_CHUNKSIZE, &actual_length, USB_TIMEOUT);
        if (ret < 0)
            printf( "usb bulk read failed");
        actual_length -= 2;
    } while (actual_length == 0);
    memcpy (buf, usbreadbuffer+2, actual_length);
    if (actual_length != size) {
        printf("[%s:%d] bozo actual_length %d size %d\n", __FUNCTION__, __LINE__, actual_length, size);
        //exit(-1);
        }
memdumpfile(buf, actual_length, "READ");
    return actual_length;
}
static struct ftdi_context *ftdi_new(void)
{
static struct ftdi_context foo;
printf("[%s:%d] funky version\n", __FUNCTION__, __LINE__);
    return &foo;
}
#endif

static struct libusb_context *usb_context;
static int number_of_devices = 1;
static int found_232H;

static void memdump(uint8_t *p, int len, char *title)
{
int i;

    i = 0;
    while (len > 0) {
        if (!(i & 0xf)) {
            if (i > 0)
                printf("\n");
            printf("%s: ",title);
        }
        printf("%02x ", *p++);
        i++;
        len--;
    }
    printf("\n");
}

#define BUFFER_MAX_LEN      1000000
#define FILE_READSIZE          6464
#define MAX_SINGLE_USB_DATA    4045
#define DITEM(...) ((uint8_t[]){sizeof((uint8_t[]){ __VA_ARGS__ }), __VA_ARGS__})
#define M(A)               ((A) & 0xff)
#define INT16(A)           M(A), M((A) >> 8)
#define INT32(A)           INT16(A), INT16((A) >> 16)
#define BSWAP(A) ((((A) & 1) << 7) | (((A) & 2) << 5) | (((A) & 4) << 3) | (((A) & 8) << 1) \
         | (((A) & 0x10) >> 1) | (((A) & 0x20) >> 3) | (((A) & 0x40) >> 5) | (((A) & 0x80) >> 7))
#define MS(A)              BSWAP(M(A))
#define SWAP32(A)          MS((A) >> 24), MS((A) >> 16), MS((A) >> 8), MS(A)
#define SWAP32B(A)         MS(A), MS((A) >> 8), MS((A) >> 16), MS((A) >> 24)
#define C2BIT40(A)       (  ((uint64_t)(A)[0])        \
                         | (((uint64_t)(A)[1]) <<  8) \
                         | (((uint64_t)(A)[2]) << 16) \
                         | (((uint64_t)(A)[3]) << 24) \
                         | (((uint64_t)(A)[4]) << 32) )

/*
 * FTDI constants
 */
#define MREAD   (MPSSE_LSB|MPSSE_READ_NEG)
#define MWRITE  (MPSSE_LSB|MPSSE_WRITE_NEG)
#define DREAD   (MPSSE_DO_READ  | MREAD)
#define DWRITE  (MPSSE_DO_WRITE | MWRITE)

#define TMSW  (MPSSE_WRITE_TMS      |MWRITE|MPSSE_BITMODE)//4b
#define TMSRW (MPSSE_WRITE_TMS|DREAD|MWRITE|MPSSE_BITMODE)//6f

#define DATAWBIT  (DWRITE|MPSSE_BITMODE)       //1b
#define DATARBIT  (DREAD |MPSSE_BITMODE)       //2e
#define DATARWBIT (DREAD |DWRITE|MPSSE_BITMODE)//3f
#define DATAW(READA, A)    (DWRITE|(READA)), INT16((A)-1) //(0)->19 (DREAD)->3d
#define DATAR(A)           DREAD, INT16((A)-1) //2c

#define IDLE_TO_SHIFT_IR   TMSW, 0x03, 0x03  /* Idle -> Shift-IR */
#define IDLE_TO_SHIFT_DR   TMSW, 0x02, 0x01  /* Idle -> Shift-DR */
#define EXIT1_TO_IDLE      TMSW, 0x01, 0x01  /* Exit1/Exit2 -> Idle */
#define IDLE_TO_RESET      TMSW, 0x02, 0x07  /* Idle -> Reset */
#define RESET_TO_IDLE      TMSW, 0x00, 0x00  /* Reset -> Idle */
#define IN_RESET_STATE     TMSW, 0x00, 0x7f  /* Marker for Reset */
#define SHIFT_TO_EXIT1(READA, A) \
     TMSW | (READA), 0x00, ((A) | 0x01)             /* Shift-IR -> Exit1-IR */
#define SHIFT_TO_UPDATE_TO_IDLE(READA, A) \
     TMSW | (READA), 0x02, ((A) | 0x03)    /* Shift-DR -> Update-DR -> Idle */
#define FORCE_RETURN_TO_RESET TMSW, 0x04, 0x1f /* go back to TMS reset state */
#define RESET_TO_SHIFT_DR     TMSW, 0x03, 0x02  /* Reset -> Shift-DR */
#define TMS_WAIT \
         TMSW, 0x06, 0x00, TMSW, 0x06, 0x00, TMSW, 0x06, 0x00
#define TMSW_DELAY                                             \
         RESET_TO_IDLE,  /* Hang out in Idle for a while */ \
         TMS_WAIT, TMS_WAIT, TMS_WAIT, TMS_WAIT, \
         TMSW, 0x06, 0x00, TMSW, 0x06, 0x00, TMSW, 0x01, 0x00
#define PAUSE_TO_SHIFT       TMSW, 0x01, 0x01 /* Pause-DR -> Shift-DR */
#define SHIFT_TO_PAUSE       TMSW, 0x01, 0x01 /* Shift-DR -> Pause-DR */
#define TMS_RESET_WEIRD      TMSW, 0x04, 0x7f /* Reset????? */
#define EXTEND_EXTRA 0xc0

#ifdef USE_CORTEX_ADI
#define OPCODE_BITS 0x05
#define IRREG_EXTRABIT 0x100
#define EXTRA_BIT(READA, B)     DATAWBIT | (READA), 0x02, M((IRREG_BYPASS<<4) | (B)),
#else
#define OPCODE_BITS 0x04
#define IRREG_EXTRABIT 0
#define EXTRA_BIT(READA, B)
#endif

#define JTAG_IRREG(READA, A)                             \
     IDLE_TO_SHIFT_IR,                            \
     DATAWBIT | (READA), 4, M(A),                        \
     SHIFT_TO_EXIT1((READA), ((A) & 0x100)>>1)

#define JTAG_IRREG_EXTRA(READA, A)                             \
     IDLE_TO_SHIFT_IR,                            \
     DATAWBIT | (READA), OPCODE_BITS, M(A),                        \
     EXTRA_BIT(READA, 0xff)                                      \
     SHIFT_TO_EXIT1((READA), ((A) & 0x100)>>1)

#define EXTENDED_COMMAND(READA, A, B)                       \
     IDLE_TO_SHIFT_IR,                            \
     DATAWBIT | (READA), OPCODE_BITS, M(A),                 \
     EXTRA_BIT(READA, B)                                      \
     SHIFT_TO_UPDATE_TO_IDLE((READA), ((A) & 0x100)>>1)

static uint8_t *catlist(uint8_t *arg[])
{
    static uint8_t prebuffer[BUFFER_MAX_LEN];
    uint8_t *ptr = prebuffer + 1;
    while (*arg) {
        memcpy(ptr, *arg+1, (*arg)[0]);
        ptr += (*arg)[0];
        arg++;
    }
    prebuffer[0] = ptr - (prebuffer + 1);
    return prebuffer;
}

static uint8_t *pulse_gpio(int delay)
{
#define GPIO_DONE            0x10
#define GPIO_01              0x01
#define SET_LSB_DIRECTION(A) SET_BITS_LOW, 0xe0, (0xea | (A))

    static uint8_t prebuffer[BUFFER_MAX_LEN];
    static uint8_t pulsepre[] =
      DITEM(SET_LSB_DIRECTION(GPIO_DONE | GPIO_01),
            SET_LSB_DIRECTION(GPIO_DONE));
    static uint8_t pulse65k[] = DITEM(CLK_BYTES, INT16(65536 - 1));
    static uint8_t pulsepost[] =
      DITEM(SET_LSB_DIRECTION(GPIO_DONE | GPIO_01),
            SET_LSB_DIRECTION(GPIO_01));
    uint8_t *ptr = prebuffer+1;
    memcpy(ptr, pulsepre+1, pulsepre[0]);
    ptr += pulsepre[0];
    while(delay > 65536) {
        memcpy(ptr, pulse65k+1, pulse65k[0]);
        ptr += pulse65k[0];
        delay -= 65536;
    }
    *ptr++ = CLK_BYTES;
    *ptr++ = M(delay-1);
    *ptr++ = M((delay-1)>>8);
    memcpy(ptr, pulsepost+1, pulsepost[0]);
    ptr += pulsepost[0];
    prebuffer[0] = ptr - (prebuffer + 1);
    return prebuffer;
}

static void write_data(struct ftdi_context *ftdi, uint8_t *buf, int size)
{
    struct ftdi_transfer_control* writetc = ftdi_write_data_submit(ftdi, buf, size);
    ftdi_transfer_data_done(writetc);
}

static uint8_t bitswap[256];
static int last_read_data_length;
static uint8_t *read_data(struct ftdi_context *ftdi, int size)
{
    static uint8_t last_read_data[10000];
#ifdef USE_LIBFTDI
    struct ftdi_transfer_control* tc = ftdi_read_data_submit(ftdi, last_read_data, size);
    ftdi_transfer_data_done(tc);
    last_read_data_length = size;
#else
    last_read_data_length = ftdi_read_data(ftdi, last_read_data, size);
#endif
    return last_read_data;
}

static uint64_t read_data_int(struct ftdi_context *ftdi, int size)
{
    uint64_t ret = 0;
    uint8_t *bufp = read_data(ftdi, size);
    uint8_t *backp = bufp + size;
    while (backp > bufp)
        ret = (ret << 8) | bitswap[*--backp];  //each byte is bitswapped
    return ret;
}

static uint8_t *check_read_data(int linenumber, struct ftdi_context *ftdi, uint8_t *buf)
{
    uint8_t *rdata = read_data(ftdi, buf[0]);
    if (last_read_data_length != buf[0] || memcmp(buf+1, rdata, buf[0])) {
        printf("[%s] mismatch on line %d\n", __FUNCTION__, linenumber);
        memdump(buf+1, buf[0], "EXPECT");
        memdump(rdata, last_read_data_length, "ACTUAL");
    }
    return rdata;
}

static uint16_t fetch16(struct ftdi_context *ftdi, uint8_t *req)
{
    write_data(ftdi, req+1, req[0]);
#if 1
    uint8_t *rdata = read_data(ftdi, sizeof(uint16_t));
    return rdata[0] | (rdata[1] << 8);
#else
    return read_data_int(ftdi, 2);
#endif
}

static uint64_t fetch32(struct ftdi_context *ftdi, uint8_t *req)
{
    write_data(ftdi, req+1, req[0]);
    return read_data_int(ftdi, 4);
}

static uint64_t fetch40(struct ftdi_context *ftdi, uint8_t *req)
{
    write_data(ftdi, req+1, req[0]);
    return read_data_int(ftdi, 5);
}

static uint8_t *send_data_frame(struct ftdi_context *ftdi, uint8_t read_param, uint8_t *headerl[],
    uint8_t *tail, uint8_t *ptrin, int size, int limit_len, uint8_t *checkdata)
{
    int i;
    static uint8_t packetbuffer[BUFFER_MAX_LEN];
    uint8_t *readptr = packetbuffer;

    uint8_t *header = catlist(headerl);
    memcpy(readptr, header+1, header[0]);
    readptr += header[0];
    while (size > 0) {
        int rlen = size-1;
        if (rlen > limit_len)
            rlen = limit_len;
        if (rlen < limit_len)
            rlen--;                   // last byte is actually loaded with DATAW command
        *readptr++ = DWRITE | read_param;
        *readptr++ = rlen;
        *readptr++ = rlen >> 8;
        for (i = 0; i <= rlen; i++)
            *readptr++ = *ptrin++;
        if (rlen < limit_len) {
            uint8_t ch = *ptrin++;
            *readptr++ = DATAWBIT | read_param;
            *readptr++ = 0x06;
            *readptr++ = ch;        // 7 bits of data here
            memcpy(readptr, tail+1, tail[0]);
            *readptr |= read_param; // this is a TMS instruction to shift state
            *(readptr+2) |= 0x80 & ch; // insert 1 bit of data here
            readptr += tail[0];
        }
        write_data(ftdi, packetbuffer, readptr - packetbuffer);
        size -= limit_len+1;
        readptr = packetbuffer;
    }
    if(checkdata)
        return check_read_data(__LINE__, ftdi, checkdata);
    return NULL;
}

/*
 * Xilinx constants
 */
#define CLOCK_FREQUENCY      15000000
//#define CLOCK_FREQUENCY      30000000
#define MAX_PACKET_STRING    10

#define IRREG_USER2          (IRREG_EXTRABIT | 0x003)
#define IRREG_CFG_OUT        (IRREG_EXTRABIT | 0x004)
#define IRREG_CFG_IN         (IRREG_EXTRABIT | 0x005)
#define IRREG_USERCODE       (IRREG_EXTRABIT | 0x008)
#define IRREG_JPROGRAM       (IRREG_EXTRABIT | 0x00b)
#define IRREG_JSTART         (IRREG_EXTRABIT | 0x00c)
#define IRREG_ISC_NOOP       (IRREG_EXTRABIT | 0x014)
#define IRREG_BYPASS         (IRREG_EXTRABIT | 0x13f)

/* ARM JTAG-DP registers */
#define IRREGA_ABORT         0x8
/* 35 bit register ? */
#define IRREGA_DPACC         0xa   /* Debug Port access */
#define IRREGA_APACC         0xb   /* Access Port access */
#define IRREGA_IDCODE        0xe
#define IRREGA_BYPASS        0xf

#define SMAP_DUMMY           0xffffffff
#define SMAP_SYNC            0xaa995566

// Type 1 Packet
#define SMAP_TYPE1(OPCODE,REG,COUNT) \
    (0x20000000 | ((OPCODE) << 27) | ((REG) << 13) | (COUNT))
#define SMAP_OP_NOP         0
#define SMAP_OP_READ        1
#define SMAP_OP_WRITE       2
#define SMAP_REG_CMD     0x04  // CMD register, Table 5-22
#define     SMAP_CMD_DESYNC 0x0000000d  // end of configuration
#define SMAP_REG_STAT    0x07  // STAT register, Table 5-25
#define SMAP_REG_BOOTSTS 0x16  // BOOTSTS register, Table 5-35

// Type 2 Packet
#define SMAP_TYPE2(LEN) (0x40000000 | (LEN))

#define PATTERN1 \
         INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff), \
         INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff), \
         INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff)

#define PATTERN2 \
         INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), \
         INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), \
         INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), \
         INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), \
         INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), \
         INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), \
         INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff), \
         INT32(0xffffffff), INT32(0xffffffff), INT32(0xffffffff)

#ifndef USE_CORTEX_ADI
#define COMMAND_ENDING  /* Enters in Shift-DR */            \
     DATAR(3),                                              \
     DATARBIT, 0x06,                                        \
     SHIFT_TO_UPDATE_TO_IDLE(DREAD, 0),                     \
     SEND_IMMEDIATE
#else
#define COMMAND_ENDING  /* Enters in Shift-DR */            \
    DATAR(4),                                               \
    SHIFT_TO_UPDATE_TO_IDLE(0, 0),                          \
    SEND_IMMEDIATE
#endif

#ifdef USE_CORTEX_ADI
#define WRITE_READ(LL, A,B) \
    /*printf("[%d]\n", LL);*/ \
    write_data(ftdi, (A)+1, (A)[0]); \
    check_read_data(LL, ftdi, (B));
#define TEMPLOADIR(A) \
    IDLE_TO_SHIFT_IR, DATAWBIT, 0x05, 0xff, DATAWBIT, 0x02, M((IRREG_BYPASS<<4) | (A)) //JTAG_IRREG
#define TEMPLOADDR(A) \
    IDLE_TO_SHIFT_DR, \
    DATAWBIT, 0x00, 0x00
#define LOADIR(A) \
    TEMPLOADIR(A), TMSW, 0x01, 0x83
#define LOADDR(AREAD, A) \
    TEMPLOADDR(0), DATAW((AREAD), 4), INT32(A), \
    DATAWBIT | (AREAD), 0x01, ((((uint64_t)(A))>>32) & 0x3f),\
    SHIFT_TO_UPDATE_TO_IDLE((AREAD),((((uint64_t)(A))>>32) & 0x80))

#define DR_WAIT RESET_TO_IDLE, TMS_WAIT, TMSW, 0x02, 0x00

#define LOADIRDR(IRA, AREAD, A) \
    LOADIR(IRA), LOADDR(AREAD, A)

#define LOADDR_3_7 \
    LOADDR(DREAD, 0x03), LOADDR(DREAD, 0x07), SEND_IMMEDIATE

#define LOADIRDR_3_7(A) \
    LOADIR(A), LOADDR_3_7

#define RET_PATTERN1 0x0a, 0x00, 0x00, 0x80, 0xe0, 0xfc
#define RET_PATTERN2 0x12, 0x02, 0x00, 0x00, 0x00, 0xe0
#define RET_PATTERN3 0x02, 0x00, 0x00, 0x00, 0x00, 0x00
#define RET_PATTERN4 0x12, 0x00, 0x86, 0x18, 0x06, 0x00
#define RET_PATTERN5 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00
#define RET_PATTERN6 0x02, 0xa0, 0x0d, 0x00, 0x80, 0xf0
#define RET_PATTERN7 0x82, 0x81, 0x9b, 0xa8, 0x6a, 0x0d
#define RET_PATTERN8 0x02, 0x10, 0x00, 0xf8, 0x3e, 0x07
#define RET_PATTERN9 0x02, 0x00, 0x14, 0x00, 0x00, 0x00
#define RET_PATTERNA 0x02, 0x00, 0x00, 0x08, 0x02, 0x00
#define RET_PATTERNB 0x12, 0x00, 0x04, 0x18, 0x06, 0x00
#define RET_PATTERNC 0x12, 0x02, 0x00, 0x04, 0x01, 0x00

static void clear_cortex(struct ftdi_context *ftdi)
{
    uint8_t *senddata = DITEM(
          LOADIRDR(IRREGA_ABORT, 0, 0x08), /* Clear WDATAERR write data error flag */
          LOADIRDR(IRREGA_DPACC, 0, 0x028000019aLL),
          LOADIRDR(IRREGA_DPACC, 0, 0x03),
          LOADDR_3_7,);
    uint8_t *dresp = DITEM( RET_PATTERN1, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
}
static void cortex_test(struct ftdi_context *ftdi, int count, int extra)
{
int i, j;
uint8_t *senddata, *dresp;
if (extra) {
if (extra == 2) {
    clear_cortex(ftdi);
    senddata = DITEM( LOADIRDR(IRREGA_DPACC, 0, 0x04),
                     LOADIRDR(IRREGA_APACC, DREAD, 0x01),
                     LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERN3, RET_PATTERNC, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM( LOADIRDR(IRREGA_DPACC, 0, 0x08000004),
                     LOADIRDR(IRREGA_APACC, DREAD, 0x01),
                     LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERNA, RET_PATTERN2, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM( LOADIRDR(IRREGA_DPACC, 0, 0x04),
                     LOADIRDR(IRREGA_APACC, DREAD, 0x10),
                     LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERN3, RET_PATTERN2, RET_PATTERN1);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM( LOADIRDR(IRREGA_DPACC, 0, 0x08000004),
                     LOADIRDR(IRREGA_APACC, DREAD, 0x8400000010LL),
                     LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERNA, RET_PATTERN2, RET_PATTERN1);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(
          LOADIRDR(IRREGA_DPACC, 0, 0x04),
          LOADIR(IRREGA_APACC),
          LOADDR(DREAD, 0x87c0000802LL), LOADDR(DREAD, 0x07), RESET_TO_IDLE, TMSW, 0x01, 0x00,
          LOADDR(DREAD, 0x87c0000902LL), LOADDR(DREAD, 0x07), RESET_TO_IDLE, TMSW, 0x01, 0x00,
          LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERN3, RET_PATTERN2, RET_PATTERN9, RET_PATTERN9, RET_PATTERN8, RET_PATTERN1);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(
          LOADIRDR(IRREGA_DPACC, 0, 0x08000004),
          LOADIRDR(IRREGA_APACC, DREAD, 0x8400480002LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x84004818a2LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x8400480442LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x8400480142LL), LOADDR(DREAD, 0x07),
          LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERNA, RET_PATTERN8, RET_PATTERN7, RET_PATTERN7,
             RET_PATTERN5, RET_PATTERN5, RET_PATTERN4, RET_PATTERN4, RET_PATTERN3, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    uint8_t senddata33[] = {
          LOADIRDR(IRREGA_APACC, 0, 0x8400480442LL),
          LOADDR(DREAD, 0x18060016), LOADDR(DREAD, 0x18860016),
          LOADDR(DREAD, 0x8400490002LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x84004918a2LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x8400490442LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x8400490142LL), LOADDR(DREAD, 0x07),
          LOADIRDR_3_7(IRREGA_DPACC)};
    dresp = DITEM( RET_PATTERN3, RET_PATTERN3, RET_PATTERN3, RET_PATTERN3,
             RET_PATTERN7, RET_PATTERN7, RET_PATTERN5, RET_PATTERN5,
             RET_PATTERN4, RET_PATTERN4, RET_PATTERN3, RET_PATTERN1,);
    write_data(ftdi, senddata33, sizeof(senddata33));
    check_read_data(__LINE__, ftdi, dresp);
    senddata = DITEM(
          LOADIRDR(IRREGA_APACC, 0, 0x8400490442LL),
          LOADDR(DREAD, 0x18060016), LOADDR(DREAD, 0x18860016),
          LOADDR(DREAD, 0x84004818a2LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x8400480442LL), LOADDR(DREAD, 0x07),
          LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERN3, RET_PATTERN3, RET_PATTERN3, RET_PATTERN3,
              RET_PATTERN5, RET_PATTERN5, RET_PATTERN4, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(
          LOADIRDR(IRREGA_APACC, 0, 0x8400480422LL), LOADDR(DREAD, 0x07),
          LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERN4, RET_PATTERN6, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(
          LOADIRDR(IRREGA_APACC, 0, 0x84004918a2LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x8400490442LL), LOADDR(DREAD, 0x07),
          LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERN6, RET_PATTERN5, RET_PATTERN5, RET_PATTERN4, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(
          LOADIRDR(IRREGA_APACC, 0, 0x8400490422LL), LOADDR(DREAD, 0x07),
          LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERN4, RET_PATTERN6, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(
          IDLE_TO_RESET, TMS_RESET_WEIRD,
          RESET_TO_SHIFT_DR,
          DATAW(DREAD, 0x3f), PATTERN1, 0xff, 0x00, 0x00,
          DATARWBIT, 0x06, 0x00,
          SHIFT_TO_UPDATE_TO_IDLE(DREAD, 0),
          SEND_IMMEDIATE);
    dresp = DITEM( 0x93, 0x70, 0x72, 0x03, 0x77, 0x04, 0xa0, 0x4b,
             INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff),
             INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff),
             INT32(0xff), INT32(0xff), INT32(0xff), INT32(0xff),
             0x00,);
    WRITE_READ(__LINE__, senddata, dresp);
    for (i = 0; i < 3; i++) {
        senddata = DITEM(
          TEMPLOADIR(IRREGA_BYPASS), SHIFT_TO_UPDATE_TO_IDLE(0, 0x80),
          IDLE_TO_SHIFT_IR, DATAWBIT, 0x05, 0xc3, DATAWBIT, 0x02, 0xff, SHIFT_TO_UPDATE_TO_IDLE(0, 0x80),
          IDLE_TO_SHIFT_DR, DATAR(4), SHIFT_TO_UPDATE_TO_IDLE(0, 0), SEND_IMMEDIATE);
        dresp = DITEM( INT32(0),);
        WRITE_READ(__LINE__, senddata, dresp);
        senddata = DITEM(
          TEMPLOADIR(IRREGA_BYPASS), SHIFT_TO_UPDATE_TO_IDLE(0, 0x80),
          IDLE_TO_SHIFT_IR, DATAWBIT, 0x05, 0xc3, DATAWBIT, 0x02, 0xff, SHIFT_TO_UPDATE_TO_IDLE(0, 0x80),
          IDLE_TO_SHIFT_DR, DATAW(0, 1), 0x69, DATAWBIT, 0x01, 0x00, DATAWBIT, 0x00, 0x00, DATAR(4), SHIFT_TO_UPDATE_TO_IDLE(0, 0),
          SEND_IMMEDIATE);
        dresp = DITEM( INT32(0),);
        WRITE_READ(__LINE__, senddata, dresp);
        for (j = 0; j < 2; j++) {
            senddata = DITEM(
              TEMPLOADIR(IRREGA_BYPASS), SHIFT_TO_UPDATE_TO_IDLE(0, 0x80),
              IDLE_TO_SHIFT_IR, DATAWBIT, 0x05, 0xc3, DATAWBIT, 0x02, 0xff, SHIFT_TO_UPDATE_TO_IDLE(0, 0x80),
              IDLE_TO_SHIFT_DR, DATAWBIT, 0x05, 0x0c, SHIFT_TO_UPDATE_TO_IDLE(0, 0),
              IDLE_TO_SHIFT_DR, DATAW(0, 1), 0x69, DATAWBIT, 0x01, 0x00, DATAWBIT, 0x00, 0x00, DATAR(4), SHIFT_TO_UPDATE_TO_IDLE(0, 0),
              SEND_IMMEDIATE);
            dresp = DITEM( INT32(0),);
            WRITE_READ(__LINE__, senddata, dresp);
        }
    }
}
    clear_cortex(ftdi);
    senddata = DITEM( LOADIRDR(IRREGA_DPACC, 0, 0x04),
                      LOADIRDR(IRREGA_APACC, DREAD, 0x01),
                      LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERN3, RET_PATTERNC, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM( LOADIRDR(IRREGA_DPACC, 0, 0x08000004),
                      LOADIRDR(IRREGA_APACC, DREAD, 0x01),
                      LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERNA, RET_PATTERN2, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
}
else {
    clear_cortex(ftdi);
//01
    senddata = DITEM( LOADIRDR(IRREGA_DPACC, 0, 0x04),
                      LOADIRDR(IRREGA_APACC, DREAD, 0x01), RESET_TO_IDLE, TMS_WAIT, TMSW, 0x03, 0x00,
                      LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERN3, 0x12, 0x02, 0x00, 0x1c, 0x87, 0x10, RET_PATTERN1);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM( LOADIRDR(IRREGA_DPACC, 0, 0x08000004),
                      LOADIRDR(IRREGA_APACC, DREAD, 0x01), DR_WAIT,
                      LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERNA, RET_PATTERN2, RET_PATTERN1,);
WRITE_READ(__LINE__, senddata, dresp);
//10
    senddata = DITEM( LOADIRDR(IRREGA_DPACC, 0, 0x04),
                      LOADIRDR(IRREGA_APACC, DREAD, 0x10), RESET_TO_IDLE, TMS_WAIT, TMSW, 0x03, 0x00, 
                     LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERN3, RET_PATTERN2, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM( LOADIRDR(IRREGA_DPACC, 0, 0x08000004),
                      LOADIRDR(IRREGA_APACC, DREAD, 0x8400000010LL), DR_WAIT,
                      LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERNA, RET_PATTERN2, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);

    senddata = DITEM(
           LOADIRDR(IRREGA_DPACC, 0, 0x04),
           LOADIR(IRREGA_APACC),
           LOADDR(DREAD, 0x87c0000802LL), RESET_TO_IDLE, TMS_WAIT, TMSW, 0x03, 0x00,
                      LOADDR(DREAD, 0x07), RESET_TO_IDLE, TMS_WAIT, TMSW, 0x03, 0x00,
           LOADDR(DREAD, 0x87c0000902LL), RESET_TO_IDLE, TMS_WAIT, TMSW, 0x03, 0x00,
                      LOADDR(DREAD, 0x07), RESET_TO_IDLE, TMS_WAIT, TMSW, 0x03, 0x00,
           LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERN3, RET_PATTERN2, RET_PATTERN9, RET_PATTERN9, RET_PATTERN8, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(
           LOADIR(IRREGA_APACC),
           LOADDR(0, 0x87c0038402LL), LOADDR(DREAD, 0x07), RESET_TO_IDLE, TMSW, 0x01, 0x00,
           LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERN8, RET_PATTERN3, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(
           LOADIRDR(IRREGA_DPACC, 0, 0x08000004),
           LOADIRDR(IRREGA_APACC, DREAD, 0x8400480002LL), LOADDR(DREAD, 0x07),
           LOADDR(DREAD, 0x84004818a2LL), LOADDR(DREAD, 0x07),
           LOADDR(DREAD, 0x8400480442LL), LOADDR(DREAD, 0x07),
           LOADDR(DREAD, 0x8400480142LL), LOADDR(DREAD, 0x07),
           LOADIRDR_3_7(IRREGA_DPACC),);
    dresp = DITEM( RET_PATTERNA, RET_PATTERN3, RET_PATTERN7, RET_PATTERN7, RET_PATTERN5, RET_PATTERN5, RET_PATTERNB, RET_PATTERNB, RET_PATTERN3, RET_PATTERN1,);
    WRITE_READ(__LINE__, senddata, dresp);
    static uint8_t senddata23[] = {
           LOADIRDR(IRREGA_APACC, 0, 0x8400480442LL),
           LOADDR(DREAD, 0x18060016), LOADDR(DREAD, 0x18860016),
           LOADDR(DREAD, 0x8400490002LL), LOADDR(DREAD, 0x07),
           LOADDR(DREAD, 0x84004918a2LL), LOADDR(DREAD, 0x07),
           LOADDR(DREAD, 0x8400490442LL), LOADDR(DREAD, 0x07),
           LOADDR(DREAD, 0x8400490142LL), LOADDR(DREAD, 0x07),
           LOADIRDR_3_7(IRREGA_DPACC),};
    dresp = DITEM( RET_PATTERN3, RET_PATTERN3, RET_PATTERN3, RET_PATTERN3,
             RET_PATTERN7, RET_PATTERN7, RET_PATTERN5, RET_PATTERN5,
             RET_PATTERNB, RET_PATTERNB, RET_PATTERN3, RET_PATTERN1,);
    write_data(ftdi, senddata23, sizeof(senddata23));
    check_read_data(__LINE__, ftdi, dresp);
}
    while (count--) {
            senddata = DITEM(
                     LOADIRDR(IRREGA_ABORT, 0, 0x08), /* Clear WDATAERR write data error flag */
                     LOADIRDR(IRREGA_DPACC, 0, 0x028000019aLL),
                     LOADIRDR(IRREGA_APACC, 0, 0x8400490442LL),
                     LOADDR(DREAD, 0x18060016), LOADDR(DREAD, 0x18860016),
                     LOADIRDR(IRREGA_DPACC, DREAD, 0x03), LOADDR_3_7);
            dresp = DITEM( RET_PATTERNA, RET_PATTERN2, RET_PATTERN1);
            WRITE_READ(__LINE__, senddata, dresp);
            senddata = DITEM(LOADIRDR(IRREGA_DPACC, 0, 0x04),
                      LOADIRDR(IRREGA_APACC, DREAD, 0x01),
                      LOADIRDR_3_7(IRREGA_DPACC));
            dresp = DITEM( RET_PATTERN3, RET_PATTERNC, RET_PATTERN1);
            WRITE_READ(__LINE__, senddata, dresp);
            senddata = DITEM(LOADIRDR(IRREGA_DPACC, 0, 0x08000004),
                      LOADIRDR(IRREGA_APACC, DREAD, 0x01),
                      LOADIRDR_3_7(IRREGA_DPACC));
            dresp = DITEM( RET_PATTERNA, RET_PATTERN2, RET_PATTERN1);
            WRITE_READ(__LINE__, senddata, dresp);
    }
    senddata = DITEM(LOADIRDR(IRREGA_DPACC, 0, 0x04),
                    LOADIRDR(IRREGA_APACC, DREAD, 0x10),
                    LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERN3, RET_PATTERN2, RET_PATTERN1);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(   LOADIRDR(IRREGA_DPACC, 0, 0x08000004),
                      LOADIRDR(IRREGA_APACC, DREAD, 0x8400000010LL),
                      LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERNA, RET_PATTERN2, RET_PATTERN1);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(LOADIRDR(IRREGA_DPACC, 0, 0x04),
             LOADIR(IRREGA_APACC),
             LOADDR(DREAD, 0x87c0000802LL),
                      LOADDR(DREAD, 0x07), RESET_TO_IDLE, TMSW, 0x01, 0x00,
             LOADDR(DREAD, 0x87c0000902LL),
                      LOADDR(DREAD, 0x07), RESET_TO_IDLE, TMSW, 0x01, 0x00,
             LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERN3, RET_PATTERN2, RET_PATTERN9, RET_PATTERN9, RET_PATTERN8, RET_PATTERN1);
    WRITE_READ(__LINE__, senddata, dresp);
    uint8_t senddata3[] = {
        LOADIRDR(IRREGA_DPACC, 0, 0x08000004),
        LOADIRDR(IRREGA_APACC, DREAD, 0x8400480002LL), LOADDR(DREAD, 0x07),
       LOADDR(DREAD, 0x84004818a2LL), LOADDR(DREAD, 0x07),
       LOADDR(DREAD, 0x8400480442LL), LOADDR(DREAD, 0x07),
       LOADDR(DREAD, 0x8400480142LL), LOADDR(DREAD, 0x07),
      LOADIRDR_3_7(IRREGA_DPACC)};
    dresp = DITEM( RET_PATTERNA, RET_PATTERN8, RET_PATTERN7, RET_PATTERN7, RET_PATTERN5, RET_PATTERN5, RET_PATTERN4, RET_PATTERN4, RET_PATTERN3, RET_PATTERN1);
    write_data(ftdi, senddata3, sizeof(senddata3));
    check_read_data(__LINE__, ftdi, dresp);
    uint8_t senddata17[] = {
            LOADIRDR(IRREGA_APACC, 0, 0x8400480442LL),
          LOADDR(DREAD, 0x18060016), LOADDR(DREAD, 0x18860016),
          LOADDR(DREAD, 0x8400490002LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x84004918a2LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x8400490442LL), LOADDR(DREAD, 0x07),
          LOADDR(DREAD, 0x8400490142LL), LOADDR(DREAD, 0x07),
          LOADIRDR_3_7(IRREGA_DPACC)};
    dresp = DITEM( RET_PATTERN3, RET_PATTERN3, RET_PATTERN3, RET_PATTERN3,
             RET_PATTERN7, RET_PATTERN7, RET_PATTERN5, RET_PATTERN5,
             RET_PATTERN4, RET_PATTERN4, RET_PATTERN3, RET_PATTERN1);
    write_data(ftdi, senddata17, sizeof(senddata17));
    check_read_data(__LINE__, ftdi, dresp);
    uint8_t senddata18[] = {
                     LOADIRDR(IRREGA_APACC, 0, 0x8400490442LL),
                     LOADDR(DREAD, 0x18060016), LOADDR(DREAD, 0x18860016),
                     LOADDR(DREAD, 0x84004818a2LL), LOADDR(DREAD, 0x07),
                     LOADDR(DREAD, 0x8400480442LL), LOADDR(DREAD, 0x07),
                     LOADIRDR_3_7(IRREGA_DPACC)};
    dresp = DITEM( RET_PATTERN3, RET_PATTERN3, RET_PATTERN3, RET_PATTERN3, RET_PATTERN5, RET_PATTERN5, RET_PATTERN4, RET_PATTERN1);
    write_data(ftdi, senddata18, sizeof(senddata18));
    check_read_data(__LINE__, ftdi, dresp);

    senddata = DITEM(LOADIRDR(IRREGA_APACC, 0, 0x8400480422LL), LOADDR(DREAD, 0x07),
                      LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM(RET_PATTERN4, RET_PATTERN6, RET_PATTERN1);
    WRITE_READ(__LINE__, senddata, dresp);

    senddata = DITEM(
                 LOADIRDR(IRREGA_APACC, 0, 0x84004918a2LL), LOADDR(DREAD, 0x07),
                 LOADDR(DREAD, 0x8400490442LL), LOADDR(DREAD, 0x07),
                 LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM( RET_PATTERN6, RET_PATTERN5, RET_PATTERN5, RET_PATTERN4, RET_PATTERN1);
    WRITE_READ(__LINE__, senddata, dresp);

    senddata = DITEM(LOADIRDR(IRREGA_APACC, 0, 0x8400490422LL), LOADDR(DREAD, 0x07),
                      LOADIRDR_3_7(IRREGA_DPACC));
    dresp = DITEM(RET_PATTERN4, RET_PATTERN6, RET_PATTERN1);
    WRITE_READ(__LINE__, senddata, dresp);
}
#endif

//idcode for cortex 0x4ba00477
static uint8_t idcode_pattern1[] = DITEM( INT32(0), PATTERN1, 0x00); // starts with idcode
static uint8_t idcode_pattern2[] = DITEM( INT32(0), PATTERN2, 0xff); // starts with idcode

static int idcode_setup;
static void check_idcode(struct ftdi_context *ftdi, uint8_t *statep, uint32_t idcode)
{
    static uint8_t patdata[] =  {INT32(0xff), PATTERN1};
    uint32_t returnedid;

    send_data_frame(ftdi, DREAD,
        (uint8_t *[]){statep,
                      DITEM(TMS_RESET_WEIRD, RESET_TO_SHIFT_DR),
                      NULL},
        DITEM( SHIFT_TO_UPDATE_TO_IDLE(0, 0), SEND_IMMEDIATE),
        patdata, sizeof(patdata), 9999, NULL);
    uint8_t *rdata = read_data(ftdi, idcode_pattern1[0]);
    if (!idcode_setup) {    // only setup idcode patterns on first call!
        memcpy(&returnedid, rdata, 4);
        idcode |= 0xf0000000 & returnedid;
        memcpy(idcode_pattern2+1, rdata, 4); // copy returned idcode
        memcpy(idcode_pattern1+1, rdata, 4);       // copy returned idcode
        if (memcmp(idcode_pattern1+1, rdata, idcode_pattern1[0])) {
            uint32_t anotherid;
            memcpy(&anotherid, rdata+4, 4);
            printf("[%s] second device idcode found 0x%x\n", __FUNCTION__, anotherid);
            memcpy(idcode_pattern1+4+1, rdata+4, 4);   // copy 2nd idcode
            memcpy(idcode_pattern2+4+1, rdata+4, 4);   // copy 2nd idcode
            number_of_devices++;
        }
        idcode_setup = 1;
        if (idcode != returnedid) {
            printf("[%s] id %x from file does not match actual id %x\n", __FUNCTION__, idcode, returnedid);
            exit(1);
        }
    }
    if (memcmp(idcode_pattern1+1, rdata, idcode_pattern1[0])) {
        printf("[%s]\n", __FUNCTION__);
        memdump(idcode_pattern1+1, idcode_pattern1[0], "EXPECT");
        memdump(rdata, idcode_pattern1[0], "ACTUAL");
    }
}

static void data_test(struct ftdi_context *ftdi)
{
    int i;
    uint64_t ret40;
    uint8_t *added_item[] = {
        DITEM( DATAW(0, 1), 0x69, DATAWBIT, 0x01, 0x00, 
#ifdef USE_CORTEX_ADI
        DATAWBIT, 0x00, 0x00,                                   
#endif
       ),
        DITEM( DATAWBIT, OPCODE_BITS, 0x0c, SHIFT_TO_UPDATE_TO_IDLE(0, 0), IDLE_TO_SHIFT_DR)};
    uint8_t *alist[5] = {
        DITEM( EXTENDED_COMMAND(0, EXTEND_EXTRA | IRREG_BYPASS, IRREGA_BYPASS),
               EXTENDED_COMMAND(0, EXTEND_EXTRA | IRREG_USER2, IRREGA_BYPASS),
               IDLE_TO_SHIFT_DR),
        DITEM(COMMAND_ENDING),
        NULL, NULL, NULL};
#define ALEN (sizeof(alist)/sizeof(alist[0]))
    for (i = 0; i < 4; i++) {
#ifndef USE_CORTEX_ADI
        if ((ret40 = fetch40(ftdi, catlist(alist))) != 0)
#else
        if ((ret40 = fetch32(ftdi, catlist(alist))) != 0)
#endif
            printf("[%s:%d] mismatch %" PRIx64 "\n", __FUNCTION__, __LINE__, ret40);
        if (i <= 1) {
            alist[ALEN-2] = alist[ALEN-3];
            alist[ALEN-3] = alist[ALEN-4];
            alist[ALEN-4] = added_item[i];
        }
    }
}
static void bypass_test(struct ftdi_context *ftdi, uint8_t *statep, int j, int str_count)
{
    check_idcode(ftdi, statep, 0); // idcode parameter ignored, since this is not the first invocation
    while (j-- > 0) {
        data_test(ftdi);
    }
}

/*
 * FTDI initialization
 */
struct ftdi_context *init_ftdi(uint32_t clock_frequency, uint32_t idcode)
{
#define SET_CLOCK_DIVISOR    TCK_DIVISOR, INT16(30000000/clock_frequency - 1)
int i;
    struct ftdi_context *ftdi = ftdi_new();
#ifdef USE_LIBFTDI
    ftdi_set_usbdev(ftdi, usbhandle);
    ftdi->usb_ctx = usb_context;
    ftdi->max_packet_size = 512; //5000;
#endif

    /*
     * Generic command synchronization with ftdi chip
     */
    static uint8_t errorcode_aa[] = { 0xfa, 0xaa };
    static uint8_t errorcode_ab[] = { 0xfa, 0xab };
    uint8_t retcode[2];
    for (i = 0; i < 4; i++) {
        static uint8_t illegal_command[] = { 0xaa, SEND_IMMEDIATE };
        ftdi_write_data(ftdi, illegal_command, sizeof(illegal_command));
        ftdi_read_data(ftdi, retcode, sizeof(retcode));
        if (memcmp(retcode, errorcode_aa, sizeof(errorcode_aa)))
            memdump(retcode, sizeof(retcode), "RETaa");
    }
    static uint8_t command_ab[] = { 0xab, SEND_IMMEDIATE };
    ftdi_write_data(ftdi, command_ab, sizeof(command_ab));
        ftdi_read_data(ftdi, retcode, sizeof(retcode));
    if (memcmp(retcode, errorcode_ab, sizeof(errorcode_ab)))
        memdump(retcode, sizeof(retcode), "RETab");
    uint8_t *initialize_sequence = DITEM(
         LOOPBACK_END, // Disconnect TDI/DO from loopback
         DIS_DIV_5, // Disable clk divide by 5
         SET_CLOCK_DIVISOR,
         SET_BITS_LOW, 0xe8, 0xeb,
         SET_BITS_HIGH, 0x20, 0x30,
         SET_BITS_HIGH, 0x30, 0x00,
         SET_BITS_HIGH, 0x00, 0x00,
         FORCE_RETURN_TO_RESET
    );
    uint8_t *initialize_sequence_232h = DITEM(
         LOOPBACK_END, // Disconnect TDI/DO from loopback
         DIS_DIV_5, // Disable clk divide by 5
         SET_CLOCK_DIVISOR,
         SET_BITS_LOW, 0xe8, 0xeb,
         SET_BITS_HIGH, 0x20, 0x30,
         //SET_BITS_HIGH, 0x30, 0x00,
         //SET_BITS_HIGH, 0x00, 0x00,
         FORCE_RETURN_TO_RESET
    );
    uint8_t *initialstr = (found_232H) ? initialize_sequence_232h : initialize_sequence;
    ftdi_write_data(ftdi, initialstr+1, initialstr[0]);
    uint8_t *move_to_reset = DITEM(SHIFT_TO_EXIT1(0, 0));
    i = number_of_devices;
    while (i--) {
        check_idcode(ftdi, move_to_reset, idcode);
        move_to_reset = DITEM(IDLE_TO_RESET);
    }
    return ftdi;
}

static void send_data_file(struct ftdi_context *ftdi, int inputfd)
{
    int size, i;
    uint8_t *tailp = DITEM(SHIFT_TO_PAUSE);
    uint8_t *headerp =
         DITEM( EXIT1_TO_IDLE,
#ifdef USE_CORTEX_ADI
              EXTRA_BIT(0, IRREGA_BYPASS) SHIFT_TO_EXIT1(0, 0x80), EXIT1_TO_IDLE,
#endif
              IDLE_TO_SHIFT_DR,
#ifdef USE_CORTEX_ADI
              DATAW(0, 7), INT32(0), 0x00, 0x00, 0x00, DATAWBIT, 0x06, 0x00,
#endif
              DATAW(0, 4), INT32(0));
    int limit_len = MAX_SINGLE_USB_DATA - headerp[0];
    printf("Starting to send file\n");
    do {
        static uint8_t filebuffer[FILE_READSIZE];
        size = read(inputfd, filebuffer, FILE_READSIZE);
        if (size < FILE_READSIZE)
            tailp = DITEM(
#ifndef USE_CORTEX_ADI
                          SHIFT_TO_EXIT1(0, 0),
#else
                          EXIT1_TO_IDLE,
                          EXIT1_TO_IDLE,
                          SHIFT_TO_EXIT1(0, 0x80),
#endif
                          EXIT1_TO_IDLE);
        for (i = 0; i < size; i++)
            filebuffer[i] = bitswap[filebuffer[i]];
        send_data_frame(ftdi, 0, (uint8_t *[]){headerp, NULL}, tailp, filebuffer, size, limit_len, NULL);
        headerp = DITEM(PAUSE_TO_SHIFT);
        limit_len = MAX_SINGLE_USB_DATA;
    } while(size == FILE_READSIZE);
    printf("Done sending file\n");
}

static void read_status(struct ftdi_context *ftdi, uint8_t *stat2, uint8_t *stat3, uint32_t expected)
{
#define STATREQ \
         SWAP32(SMAP_DUMMY), SWAP32(SMAP_SYNC), SWAP32(SMAP_TYPE2(0)), \
         SWAP32(SMAP_TYPE1(SMAP_OP_READ, SMAP_REG_STAT, 1))
uint8_t *req = catlist((uint8_t *[]){
    DITEM(IDLE_TO_RESET), stat2, stat3,
    DITEM(
#ifdef USE_CORTEX_ADI
        DATAW(0, 20), STATREQ, INT32(0),
#else
        DATAW(0, 19), STATREQ,
             0x00, 0x00, 0x00,
        DATAWBIT, 0x06, 0x00,
#endif
        SHIFT_TO_UPDATE_TO_IDLE(0, 0),
        EXTENDED_COMMAND(0, EXTEND_EXTRA | IRREG_CFG_OUT, IRREGA_BYPASS),
#ifdef USE_CORTEX_ADI
        IDLE_TO_SHIFT_DR, DATAR(4), SHIFT_TO_UPDATE_TO_IDLE(0, 0),
        SEND_IMMEDIATE
#else
        IDLE_TO_SHIFT_DR,
        COMMAND_ENDING
#endif
        ),
    NULL});
    write_data(ftdi, req+1, req[0]);
    uint64_t ret40 = read_data_int(ftdi, 5);
    uint32_t status = ret40 >> 8;
    if (M(ret40) != 0x40 || status != expected)
        printf("[%s:%d] mismatch %" PRIx64 "\n", __FUNCTION__, __LINE__, ret40);
    printf("STATUS %08x done %x release_done %x eos %x startup_state %x\n", status,
        status & 0x4000, status & 0x2000, status & 0x10, (status >> 18) & 7);
}

static uint64_t read_smap(struct ftdi_context *ftdi, uint8_t *prefix, uint32_t data)
{
    uint8_t *sendreq = catlist((uint8_t *[]){prefix,
          DITEM(JTAG_IRREG_EXTRA(0, IRREG_CFG_IN), EXIT1_TO_IDLE,
                 IDLE_TO_SHIFT_DR,
                 DATAW(0, 4), SWAP32(SMAP_DUMMY),
#ifdef USE_CORTEX_ADI
                 DATAW(0, 7), INT32(0), 0x00, 0x00, 0x00, DATAWBIT, 0x06, 0x00, 
#endif
                 DATAW(0, 4), SWAP32(SMAP_SYNC),
                 DATAW(0, 4), SWAP32(SMAP_TYPE1(SMAP_OP_NOP, 0,0)),
                 DATAW(0, 4)),
          (uint8_t []){4, SWAP32(SMAP_TYPE1(SMAP_OP_READ, data, 1))},
          DITEM(DATAW(0, 4), SWAP32(SMAP_TYPE1(SMAP_OP_NOP, 0,0)),
                 DATAW(0, 4), SWAP32(SMAP_TYPE1(SMAP_OP_NOP, 0,0)),
                 DATAW(0, 4), SWAP32(SMAP_TYPE1(SMAP_OP_WRITE, SMAP_REG_CMD, 1)),
                 DATAW(0, 4), SWAP32(SMAP_CMD_DESYNC),
                 DATAW(0, 4), SWAP32(SMAP_TYPE1(SMAP_OP_NOP, 0,0))),
          DITEM(
#ifdef USE_CORTEX_ADI
                 DATAW(0, 4), INT32(0x04), SHIFT_TO_EXIT1(0, 0x80),
#else
                 DATAW(0, 3), 0x04, 0x00, 0x00, DATAWBIT, 0x06, 0x00, SHIFT_TO_EXIT1(0, 0),
#endif
                 EXIT1_TO_IDLE,
                 JTAG_IRREG_EXTRA(0, IRREG_CFG_OUT), EXIT1_TO_IDLE,
                 IDLE_TO_SHIFT_DR, DATAW(DREAD, 3), 0x00, 0x00, 0x00, DATARWBIT, 0x06, 0x00,
#ifdef USE_CORTEX_ADI
                 TMSRW, 0x01, 0x01,
#else
                 SHIFT_TO_EXIT1(DREAD, 0),
#endif
                 SEND_IMMEDIATE ),
          NULL});
    write_data(ftdi, sendreq+1, sendreq[0]);
    return read_data_int(ftdi, 5);
}

static struct ftdi_context *initialize(uint32_t idcode, const char *serialno, uint32_t clock_frequency)
{
    struct ftdi_context *ftdi;
    int cfg, type = 0, i = 0, baudrate = 9600;
    static const char frac_code[8] = {0, 3, 2, 4, 1, 5, 6, 7};
    int best_divisor = 12000000*8 / baudrate;
    unsigned long encdiv = (best_divisor >> 3) | (frac_code[best_divisor & 0x7] << 14);
    libusb_device **device_list, *dev, *usbdev = NULL;
    struct libusb_device_descriptor desc;
    struct libusb_config_descriptor *config_descrip;

    /*
     * Locate USB interface for JTAG
     */
    if (libusb_init(&usb_context) < 0
     || libusb_get_device_list(usb_context, &device_list) < 0) {
        printf("libusb_init failed\n");
        exit(-1);
    }
    while ((dev = device_list[i++]) ) {
        if (libusb_get_device_descriptor(dev, &desc) < 0)
            break;
        if ( desc.idVendor == 0x403 && (desc.idProduct == 0x6001 || desc.idProduct == 0x6010
         || desc.idProduct == 0x6011 || desc.idProduct == 0x6014)) {
            unsigned char serial[64], manuf[64], descrip[128];
            libusb_ref_device(dev);
            if (libusb_open(dev, &usbhandle) < 0
             || libusb_get_string_descriptor_ascii(usbhandle, desc.iManufacturer, manuf, sizeof(manuf)) < 0
             || libusb_get_string_descriptor_ascii(usbhandle, desc.iProduct, descrip, sizeof(descrip)) < 0
             || libusb_get_string_descriptor_ascii(usbhandle, desc.iSerialNumber, serial, sizeof(serial)) < 0)
                goto error;
            printf("[%s] %s:%s:%s\n", __FUNCTION__, manuf, descrip, serial);
            if (!serialno || !strcmp(serialno, (char *)serial)) {
                usbdev = dev;
                break;
            }
            libusb_close (usbhandle);
        }
    }
    libusb_free_device_list(device_list,1);
    if (!usbdev || libusb_get_config_descriptor(usbdev, 0, &config_descrip) < 0)
        goto error;
    int configv = config_descrip->bConfigurationValue;
    libusb_free_config_descriptor (config_descrip);
    libusb_detach_kernel_driver(usbhandle, 0);
#define USBCTRL(A,B,C) \
     libusb_control_transfer(usbhandle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE \
           | LIBUSB_ENDPOINT_OUT, (A), (B), (C) | USB_INDEX, NULL, 0, USB_TIMEOUT)

    if (libusb_get_configuration (usbhandle, &cfg) < 0
     || (desc.bNumConfigurations > 0 && cfg != configv && libusb_set_configuration(usbhandle, configv) < 0)
     || libusb_claim_interface(usbhandle, 0) < 0
     || USBCTRL(USBSIO_RESET, USBSIO_RESET, 0) < 0
     || USBCTRL(USBSIO_SET_BAUD_RATE, (encdiv | 0x20000) & 0xFFFF, ((encdiv >> 8) & 0xFF00)) < 0
     || USBCTRL(USBSIO_SET_LATENCY_TIMER_REQUEST, 255, 0) < 0
     || USBCTRL(USBSIO_SET_BITMODE_REQUEST, 0, 0) < 0
     || USBCTRL(USBSIO_SET_BITMODE_REQUEST, 2 << 8, 0) < 0
     || USBCTRL(USBSIO_RESET, USBSIO_RESET_PURGE_RX, 0) < 0
     || USBCTRL(USBSIO_RESET, USBSIO_RESET_PURGE_TX, 0) < 0)
        goto error;
    //(desc.bcdDevice == 0x700) //kc       TYPE_2232H
    printf("[%s:%d] bcd %x type %d\n", __FUNCTION__, __LINE__, desc.bcdDevice, type);
    if (desc.bcdDevice == 0x900) //zedboard TYPE_232H
        found_232H = 1;
    for (i = 0; i < sizeof(bitswap); i++)
        bitswap[i] = BSWAP(i);

    /*
     * Initialize FTDI chip and GPIO pins
     */
    ftdi = init_ftdi(clock_frequency, idcode);   /* generic initialization */

    /*
     * Step 5: Check Device ID
     */

    bypass_test(ftdi, DITEM(IDLE_TO_RESET), 2 + number_of_devices, number_of_devices);
#ifdef USE_CORTEX_ADI
    cortex_test(ftdi, number_of_devices-1, 0);
#endif
    bypass_test(ftdi, DITEM(IDLE_TO_RESET), 3, 1);
#ifdef USE_CORTEX_ADI
    cortex_test(ftdi, 0, 1);
#endif
#ifndef USE_CORTEX_ADI
    static uint8_t i2resetin[] = DITEM(IDLE_TO_RESET, IN_RESET_STATE);
    write_data(ftdi, i2resetin+1, i2resetin[0]);
    uint8_t command_set_divisor[] = { SET_CLOCK_DIVISOR };
    ftdi_write_data(ftdi, command_set_divisor, sizeof(command_set_divisor));
#endif

    static uint8_t iddata[] = {INT32(0xffffffff),  PATTERN2};
    send_data_frame(ftdi, DREAD,
        (uint8_t *[]){DITEM(
#ifdef USE_CORTEX_ADI
             IDLE_TO_RESET, IN_RESET_STATE,
             0x86, 0x01, 0x00,
#endif
             SHIFT_TO_EXIT1(0, 0),
             IN_RESET_STATE,
             SHIFT_TO_EXIT1(0, 0),
             IN_RESET_STATE, RESET_TO_IDLE, IDLE_TO_SHIFT_DR), NULL},
        DITEM(PAUSE_TO_SHIFT, SEND_IMMEDIATE),
        iddata, sizeof(iddata), 9999, idcode_pattern2);
    return ftdi;
error:
    //libusb_close (usbhandle);
    printf("Can't find usable usb interface\n");
    exit(-1);
}

static uint8_t *cfg_in_command = DITEM(RESET_TO_IDLE, EXTENDED_COMMAND(0, EXTEND_EXTRA | IRREG_CFG_IN, IRREGA_BYPASS), IDLE_TO_SHIFT_DR);
int main(int argc, char **argv)
{
logfile = stdout;
    struct ftdi_context *ftdi;
    uint32_t idcode;
    uint16_t ret16;
    uint64_t ret40;
    int i = 1;
    int inputfd = 0;   /* default input for '-' is stdin */
    const char *serialno = NULL;

    if (argc < 2) {
        printf("%s: [ -s <serialno> ] <filename>\n", argv[0]);
        exit(1);
    }
    if (!strcmp(argv[i], "-s")) {
        serialno = argv[++i];
        i++;
    }
    if (strcmp(argv[i], "-")) {
        inputfd = open(argv[i], O_RDONLY);
        if (inputfd == -1) {
            printf("Unable to open file '%s'\n", argv[1]);
            exit(-1);
        }
    }
    lseek(inputfd, 0x80, SEEK_SET); /* read idcode from file to be programmed */
    read(inputfd, &idcode, sizeof(idcode));
    idcode = (M(idcode) << 24) | (M(idcode >> 8) << 16) | (M(idcode >> 16) << 8) | M(idcode >> 24);
    lseek(inputfd, 0, SEEK_SET);
    ftdi = initialize(idcode, serialno, CLOCK_FREQUENCY);

#ifndef USE_CORTEX_ADI
    if ((ret40 = fetch40(ftdi,
        DITEM(FORCE_RETURN_TO_RESET, IN_RESET_STATE, RESET_TO_IDLE,
              EXTENDED_COMMAND(0, EXTEND_EXTRA | IRREG_USERCODE, IRREGA_APACC),
              IDLE_TO_SHIFT_DR,
              COMMAND_ENDING))) != 0xffffffffff)
        printf("[%s:%d] mismatch %" PRIx64 "\n", __FUNCTION__, __LINE__, ret40);
#else
    uint8_t *senddata = DITEM(
        FORCE_RETURN_TO_RESET, IN_RESET_STATE, RESET_TO_IDLE,
        IDLE_TO_SHIFT_IR, DATAW(DREAD, 1), 0xff, DATARWBIT, 0x00, 0xff, SHIFT_TO_UPDATE_TO_IDLE(DREAD, 0x80),
        SEND_IMMEDIATE);
    uint8_t *dresp = DITEM(0x51, 0x28, 0x05);
    WRITE_READ(__LINE__, senddata, dresp);
    senddata = DITEM(
        EXTENDED_COMMAND(0, EXTEND_EXTRA | 0x108, IRREGA_BYPASS),
        IDLE_TO_SHIFT_DR, DATAR(4), SHIFT_TO_UPDATE_TO_IDLE(0, 0),
        SEND_IMMEDIATE);
    dresp = DITEM(0xff, 0xff, 0xff, 0xff);
    WRITE_READ(__LINE__, senddata, dresp);
#endif
    for (i = 0; i < 3; i++) {
        ret16 = fetch16(ftdi, DITEM(
#ifndef USE_CORTEX_ADI
              EXTENDED_COMMAND(DREAD, EXTEND_EXTRA | IRREG_BYPASS, IRREGA_BYPASS),
#else
              IDLE_TO_SHIFT_IR, DATAW(DREAD, 1), 0xff, DATARWBIT, 0x00, 0xff, SHIFT_TO_UPDATE_TO_IDLE(DREAD, 0x80),
#endif
              SEND_IMMEDIATE));
        if (ret16 == 0x118f)
            printf("xjtag: bypass first time %x\n", ret16);
        else if (ret16 == 0x1188)
            printf("xjtag: bypass next times %x\n", ret16);
        else if (ret16 == 0xf5af)
            printf("xjtag: bypass already programmed %x\n", ret16);
        else
            printf("xjtag: bypass mismatch %x\n", ret16);
    }
    read_status(ftdi, cfg_in_command, DITEM(), 0x30861900);
    static uint8_t i2reset[] = DITEM(IDLE_TO_RESET );
    write_data(ftdi, i2reset+1, i2reset[0]);
    bypass_test(ftdi, DITEM(SHIFT_TO_EXIT1(0, 0)), 3, 1);
#ifdef USE_CORTEX_ADI
    cortex_test(ftdi, 0, 2);
#endif
    bypass_test(ftdi, DITEM(IDLE_TO_RESET), 3, 1);
#ifdef USE_CORTEX_ADI
    cortex_test(ftdi, 0, 1);
#endif
    bypass_test(ftdi, DITEM(IDLE_TO_RESET), 3, 1);
#ifdef USE_CORTEX_ADI
    cortex_test(ftdi, 0, 1);
#endif
#ifndef USE_CORTEX_ADI
    bypass_test(ftdi, DITEM(IDLE_TO_RESET), 3, 1);
#endif
    /*
     * Step 2: Initialization
     */
    if ((ret16 = fetch16(ftdi,
        catlist((uint8_t *[]){
            DITEM(IDLE_TO_RESET, IN_RESET_STATE, RESET_TO_IDLE,
               JTAG_IRREG_EXTRA(0, IRREG_JPROGRAM), EXIT1_TO_IDLE,
               JTAG_IRREG_EXTRA(0, IRREG_ISC_NOOP), EXIT1_TO_IDLE),
            pulse_gpio(CLOCK_FREQUENCY/80/* 12.5 msec */),
            DITEM(
#ifndef USE_CORTEX_ADI
                 JTAG_IRREG(DREAD, IRREG_ISC_NOOP),
#else
                 IDLE_TO_SHIFT_IR, DATARWBIT, 0x04, 0x14, TMSRW, 0x01, 0x01, //JTAG_IRREG
#endif
                 SEND_IMMEDIATE),
            NULL}))) != 0x4488)
        printf("[%s:%d] mismatch %x\n", __FUNCTION__, __LINE__, ret16);
    /*
     * Step 6: Load Configuration Data Frames
     */
    if ((ret16 = fetch16(ftdi,
        DITEM(
#ifndef USE_CORTEX_ADI
         EXIT1_TO_IDLE, JTAG_IRREG(DREAD, IRREG_CFG_IN), 
#else
         EXIT1_TO_IDLE, EXTRA_BIT(0, IRREGA_BYPASS) SHIFT_TO_EXIT1(0, 0x80), EXIT1_TO_IDLE, 
         IDLE_TO_SHIFT_IR, DATARWBIT, 0x04, 0x05, TMSRW, 0x01, 0x01, //JTAG_IRREG
#endif
             SEND_IMMEDIATE))) != 0x458a)
        printf("[%s:%d] mismatch %x\n", __FUNCTION__, __LINE__, ret16);
    send_data_file(ftdi, inputfd);
    /*
     * Step 8: Startup
     */
    if ((ret40 = read_smap(ftdi, pulse_gpio(CLOCK_FREQUENCY/800/*1.25 msec*/), SMAP_REG_BOOTSTS)) != 0x0100000000)
        printf("[%s:%d] mismatch %" PRIx64 "\n", __FUNCTION__, __LINE__, ret40);
    if ((ret16 = fetch16(ftdi, DITEM( EXIT1_TO_IDLE,
#ifdef USE_CORTEX_ADI
             SHIFT_TO_EXIT1(0, 0x80),
             EXIT1_TO_IDLE,
#endif
             JTAG_IRREG_EXTRA(0, IRREG_BYPASS), EXIT1_TO_IDLE,
             JTAG_IRREG_EXTRA(0, IRREG_JSTART), EXIT1_TO_IDLE,
             TMSW_DELAY,
#ifdef USE_CORTEX_ADI
              IDLE_TO_SHIFT_IR, DATARWBIT, 0x04, 0x3f, TMSRW, 0x01, 0x81,
//JTAG_IRREG
#else
              JTAG_IRREG(DREAD, IRREG_BYPASS), 
#endif
              SEND_IMMEDIATE))) != 0xd6ac)
        printf("[%s:%d] mismatch %x\n", __FUNCTION__, __LINE__, ret16);

    if ((ret40 = read_smap(ftdi, DITEM(
#ifdef USE_CORTEX_ADI
              EXIT1_TO_IDLE, EXTRA_BIT(0, IRREGA_BYPASS) SHIFT_TO_EXIT1(0, 0x80),
#endif
              EXIT1_TO_IDLE ), SMAP_REG_STAT)) != (((uint64_t)0xfcfe7910 << 8) | 0x40))
        printf("[%s:%d] mismatch %" PRIx64 "\n", __FUNCTION__, __LINE__, ret40);
#ifndef USE_CORTEX_ADI
    static uint8_t bypass_end[] = DITEM(EXIT1_TO_IDLE, JTAG_IRREG(0, IRREG_BYPASS), EXIT1_TO_IDLE);
    write_data(ftdi, bypass_end+1, bypass_end[0]);
#endif
    if ((ret16 = fetch16(ftdi,
        DITEM(
#ifdef USE_CORTEX_ADI
              EXIT1_TO_IDLE,
              SHIFT_TO_EXIT1(0, 0x80), EXIT1_TO_IDLE,
              IDLE_TO_SHIFT_IR, DATAWBIT, 0x05, 0x3f, EXTRA_BIT(0, IRREGA_BYPASS) //JTAG_IRREG_EXTRA
              SHIFT_TO_EXIT1(0, 0x80), EXIT1_TO_IDLE,
#endif
              IDLE_TO_RESET, IN_RESET_STATE, RESET_TO_IDLE,
#ifndef USE_CORTEX_ADI
              EXTENDED_COMMAND(DREAD, EXTEND_EXTRA | IRREG_BYPASS, IRREGA_BYPASS),
#else
              IDLE_TO_SHIFT_IR, DATAW(DREAD, 1), 0xff, DATARWBIT, 0x00, 0xff,
              SHIFT_TO_UPDATE_TO_IDLE(DREAD, 0x80),
#endif
              SEND_IMMEDIATE))) != 0xf5a9)
        printf("[%s:%d] mismatch %x\n", __FUNCTION__, __LINE__, ret16);
#ifndef USE_CORTEX_ADI
    bypass_test(ftdi, DITEM(IDLE_TO_RESET), 3, 1);
#endif
    read_status(ftdi, DITEM(IN_RESET_STATE, SHIFT_TO_EXIT1(0, 0)), cfg_in_command, 0xf0fe7910);
#ifndef USE_CORTEX_ADI
    write_data(ftdi, i2reset+1, i2reset[0]);
#else
    bypass_test(ftdi, DITEM(IDLE_TO_RESET, SHIFT_TO_EXIT1(0, 0),), 3, 1);
    cortex_test(ftdi, 0, 1);
printf("[%s:%d]\n", __FUNCTION__, __LINE__);
#endif
    ftdi_deinit(ftdi);
#ifndef USE_LIBFTDI
    libusb_close (usbhandle);
    libusb_exit(usb_context);
#endif
    //execlp("/usr/local/bin/pciescanportal", "arg", (char *)NULL); /* rescan pci bus to discover device */
    return 0;
}

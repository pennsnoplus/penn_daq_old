#include <string.h>
#include <stdlib.h> 
#include <stdio.h>
#include <stdint.h>

#define MAX_DATA_SIZE                   150000 // max number of allowed bits
#define MTC_CLOCK                       0x00000002UL //sets CCLK high
#define MTC_SET_DP                      0x00000004UL //sets DP high
#define MTC_DRIVE_DP                    0x00000008UL //enables DP buffer
#define MTC_DONE_PROG                   0x00000010UL //DP readback

// define MTC register space
#define MTCRegAddressBase  (0x00007000)
#define MTCRegAddressMod   (0x29)
#define MTCRegAddressSpace (0x01)
#define MTCMemAddressBase  (0x03800000)
#define MTCMemAddressMod   (0x09)
#define MTCMemAddressSpace (0x02)

#define MTCControlReg (0x0)
#define MTCSerialReg  (0x4)
#define MTCDacCntReg (0x8)
#define MTCSoftGTReg (0xC)
#define MTCPedWidthReg (0x10)
#define MTCCoarseDelayReg (0x14)
#define MTCFineDelayReg (0x18)
#define MTCThresModReg (0x1C)
#define MTCPmskReg (0x20)
#define MTCScaleReg (0x24)
#define MTCBwrAddOutReg (0x28)
#define MTCBbaReg (0x2C)
#define MTCGtLockReg (0x30)
#define MTCMaskReg (0x34)
#define MTCXilProgReg (0x38)
#define MTCGmskReg (0x3C)
#define MTCOcGtReg (0x80)
#define MTCC50_0_31Reg (0x84)
#define MTCC50_32_42Reg (0x88)
#define MTCC10_0_31Reg (0x8C)
#define MTCC10_32_52Reg (0x90)

int trigger_scan(char *buffer);
int set_thresholds(char *buffer);

int mtc_read(char *buffer);
int mtc_write(char *buffer);

int mtc_xilinxload(void);
int mtc_init(char *buffer);

int set_gt_mask_cmd(char *buffer);
int unset_gt_mask_cmd(char *buffer);
void unset_gt_mask(unsigned long raw_trig_types);
void set_gt_mask(uint32_t raw_trig_types);

void unset_ped_crate_mask(unsigned long crate);
uint32_t set_ped_crate_mask(uint32_t crate);

void unset_gt_crate_mask(unsigned long crate);
void set_gt_crate_mask(uint32_t crate);

/* get_get_db_thresh(skey)-
 * gets threshold values for trigger types from database
 * 
 * skey 0..9 - cheese (NHIT=0)
 * skey 10   - current NHIT settings
 * skey 100.. - pepperoni (NHIT=1)
 *
 * M. Neubauer 26 Oct 1997
 */
#define MTCA_CURRENT_THRESH_SKEY        10
#define MTCA_CURRENT_ZERO_SKEY          200
#define MTCA_DAC_SLOPE                  (2048.0/5000.0)
#define MTCA_DAC_OFFSET                 ( + 2048 )

typedef struct {
    float mtca_dac_values[14];
} mtc_cons;

int load_mtc_dacs_counts(int *counts);
int load_mtc_dacs(mtc_cons *mtc_cons_ptr);
/* load_mtc_dacs()
 * Loads all the mtc/a dac thresholds. Values are taken from a file for now.
 *                              
 * M. Neubauer 4 Sept 1997
 */

int set_lockout_width(unsigned short width);
/*
 * set_lockout_width(width)
 * sets the width of the global trigger lockout time
 * width is in ns
 * M. Neubauer 2 Sept 1997
 */

int set_gt_counter(unsigned long count);
/* set_gt_counter(count) 
 * load a count into the global trigger counter
 * 
 * M. Neubauer 2 Sept 1997
 */

int set_prescale(unsigned short scale);
/*  
 * set_prescale(scale)-
 * Set up prescaler for desired number of NHIT_100_LO triggers per PRESCALE trigger
 * scale is the number of NHIT_100_LO triggers per PRESCALE trigger
 * M. Neubauer 29 Aug 1997
 */

int set_pulser_frequency(float freq);
/*  
 * set_pulser_frequency(freq)-
 * Set up the pulser for the desired frequency using the 50 MHz clock as the source
 * freq is in Hz. Pass zero for SOFT_GT as source of the pulser
 * M. Neubauer 29 Aug 1997
 */

int set_pedestal_width(unsigned short width);
/*
 * set_pedestal_width(width)
 * set width of PED pulses
 * width is in 5 ns increments from 5 to 1275
 * M. Neubauer 1 Sept 1997
 */

int set_coarse_delay(unsigned short delay);
/*
 * set_coarse_delay(delay)
 * set coarse 10 ns delay of PED->PULSE_GT
 * delay is in ns
 * M. Neubauer 2 Sept 1997
 */

float set_fine_delay(float delay);
/*
 * set_fine_delay(delay)
 * set fine ~79 ps delay of PED->PULSE_GT
 * delay is in ns
 * M. Neubauer 2 Sept 1997
 */

void reset_memory();
/* reset_fifo()-
 * toggles fifo_reset bit to reset the memory controller and clears BBA register
 *
 * M. Neubauer 2 Sept 1997
 *
 */


int setup_pedestals(float pulser_freq, uint32_t pedestal_width, /* in ns */
	uint32_t coarse_delay, uint32_t fine_delay);


int prepare_mtc_pedestals(float pulser_freq, /* in Hz */
	uint16_t pedestal_width, uint16_t coarse_delay, uint16_t fine_delay /* in ns */);

void enable_pedestal();
void disable_pedestal();
void enable_pulser();
void disable_pulser();

uint32_t get_mtc_crate_mask(uint32_t crate_number);

int send_softgt();

float set_gt_delay(float gtdel);
int get_gt_count(uint32_t *count);

//////////////////////////////
// REGISTER VALUES ///////////
//////////////////////////////

/* control register */

#define PED_EN     0x00000001UL
#define PULSE_EN   0x00000002UL
#define LOAD_ENPR  0x00000004UL
#define LOAD_ENPS  0x00000008UL
#define LOAD_ENPW  0x00000010UL
#define LOAD_ENLK  0x00000020UL
#define ASYNC_EN   0x00000040UL
#define RESYNC_EN  0x00000080UL
#define TESTGT     0x00000100UL
#define TEST50     0x00000200UL
#define TEST10     0x00000400UL
#define LOAD_ENGT  0x00000800UL
#define LOAD_EN50  0x00001000UL
#define LOAD_EN10  0x00002000UL
#define TESTMEM1   0x00004000UL
#define TESTMEM2   0x00008000UL
#define FIFO_RESET 0x00010000UL

/* serial register */           

#define SEN        0x00000001UL
#define SERDAT     0x00000002UL
#define SHFTCLKGT  0x00000004UL
#define SHFTCLK50  0x00000008UL
#define SHFTCLK10  0x00000010UL
#define SHFTCLKPS  0x00000020UL


/* DAC_control register */

#define TUB_SDATA  0x00000400UL
#define TUB_SCLK   0x00000800UL
#define TUB_SLATCH 0x00001000UL
#define DACSEL     0x00004000UL 
#define DACCLK     0x00008000UL

/* Masks */

#define MASKALL 0xFFFFFFFFUL
#define MASKNUN 0x00000000UL

/* Global Trigger Mask */

#define NHIT100LO        0x00000001UL
#define NHIT100MED       0x00000002UL
#define NHIT100HI        0x00000004UL
#define NHIT20           0x00000008UL
#define NHIT20LB         0x00000010UL
#define ESUMLO           0x00000020UL
#define ESUMHI           0x00000040UL
#define OWLN             0x00000080UL
#define OWLELO           0x00000100UL
#define OWLEHI           0x00000200UL
#define PULSE_GT         0x00000400UL
#define PRESCALE         0x00000800UL
#define PEDESTAL         0x00001000UL
#define PONG             0x00002000UL
#define SYNC             0x00004000UL
#define EXT_ASYNC        0x00008000UL
#define EXT2             0x00010000UL 
#define EXT3             0x00020000UL
#define EXT4             0x00040000UL
#define EXT5             0x00080000UL
#define EXT6             0x00100000UL
#define EXT7             0x00200000UL
#define EXT8_PULSE_ASYNC 0x00400000UL
#define SPECIAL_RAW      0x00800000UL 
#define NCD              0x01000000UL
#define SOFT_GT          0x02000000UL

/* Crate Masks */

#define MSK_CRATE1  0x00000001UL
#define MSK_CRATE2  0x00000002UL
#define MSK_CRATE3  0x00000008UL
#define MSK_CRATE4  0x00000004UL
#define MSK_CRATE5  0x00000010UL
#define MSK_CRATE6  0x00000020UL
#define MSK_CRATE7  0x00000040UL
#define MSK_CRATE8  0x00000080UL
#define MSK_CRATE9  0x00000100UL
#define MSK_CRATE10 0x00000200UL
#define MSK_CRATE11 0x00000400UL
#define MSK_CRATE12 0x00000800UL
#define MSK_CRATE13 0x00001000UL
#define MSK_CRATE14 0x00002000UL
#define MSK_CRATE15 0x00004000UL
#define MSK_CRATE16 0x00008000UL
#define MSK_CRATE17 0x00010000UL
#define MSK_CRATE18 0x00020000UL
#define MSK_CRATE19 0x00040000UL
#define MSK_CRATE20 0x00080000UL
#define MSK_CRATE21 0x00100000UL        // the TUB!
#define MSK_CRATE22 0x00200000UL
#define MSK_CRATE23 0x00400000UL
#define MSK_CRATE24 0x00800000UL
#define MSK_CRATE25 0x01000000UL
#define MSK_TUB     MSK_CRATE21         // everyone's favorite board!  

/* Threshold Monitoring */

#define TMON_NHIT100LO  0x00020000UL
#define TMON_NHIT100MED 0x00000000UL
#define TMON_NHIT100HI  0x00060000UL
#define TMON_NHIT20     0x00040000UL
#define TMON_NHIT20LB   0x000a0000UL
#define TMON_ESUMLO     0x00080000UL
#define TMON_ESUMHI     0x000e0000UL
#define TMON_OWLELO     0x00120000UL
#define TMON_OWLEHI     0x00100000UL
#define TMON_OWLN       0x000c0000UL

/* Default setup parameters */

#define DEFAULT_LOCKOUT_WIDTH  400               /* in ns */
#define DEFAULT_GT_MASK        EXT8_PULSE_ASYNC
#define DEFAULT_PED_CRATE_MASK MASKALL
#define DEFAULT_GT_CRATE_MASK  MASKALL 


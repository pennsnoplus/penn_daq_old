#include "db.h"

#define HOWMANY 350
#define FEC_CSR_CRATE_OFFSET	11

#define TR20DELAYOFFSET         7
#define TR20WIDTHOFFSET         11
#define TACTRIMOFFSET           17
#define CMOSCRUFTOFFSETLOW      25 // since cmos cruft is across 2 long words
#define CMOSCRUFTOFFSETHIGH     7

#define CMOS_PROG_DATA_OFFSET   2 // data is bits 2..17 in CMOS prog registers
#define CMOS_PROG_CLOCK         0x2UL
#define CMOS_PROG_SERSTOR       0x1UL

int get_crate_from_jumpers(uint16_t jumpers);

int fec_test(char *buffer);

int zdisc(char *buffer);

int mem_test(char *buffer);

int vmon(char *buffer);

int board_id(char *buffer);

int update_crate_config(int crate, uint16_t slot_mask);

int fec_load_crateadd(int crate, uint32_t slot_mask);

int set_crate_pedestals(int crate, uint32_t slot_mask, uint32_t pattern);

int32_t read_pmt(int crate, int32_t slot, int32_t limit, uint32_t *pmt_buf);

int loadsDac(unsigned long theDAC, unsigned long theDAC_Value, int crate_number, uint32_t select_reg);

int multiloadsDac(int num_dacs, uint32_t *theDacs, uint32_t *theDAC_Values, int crate_number, uint32_t select_reg);

int read_bundle(char *buffer);

int changedelay(char *buffer);

int ramp_voltage(char *buffer);

int load_relays(char *buffer);

int get_cmos_total_count(int crate,int slot,int channel,uint32_t* total_count);
int get_cmos_total_count2(int crate,int slot,uint32_t* total_count);
void dump_pmt_verbose(int n, uint32_t *pmt_buf, char* msg_buf);

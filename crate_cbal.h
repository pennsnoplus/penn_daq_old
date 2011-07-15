
int crate_cbal(char *buffer);

#define TWOTWENTY	    0x100000

//STRINGS
#define SNTR_TOOLS_DIALOG_DIVIDER   "***************************************************\n"
#define SNTR_TOOLS_DIALOG_COMPLETE  "SNTR Custom Tools Operation Complete.\n"

#define PED_ERROR_MESSAGE           "Error during pedestal collection and calculation.  Aborting.\n"

// Error return codes
#define MAXCHAN   32
#define REG_ERROR 1
#define CHGINJ_SETUP_ERROR 2
#define DRAM_ERROR 3
#define PED_ERROR 66
// error returned by bracketing routine in balance channels
#define MINIMIZATION_ERROR 99   
#define VSIDACOFFSET    17      // VSI dacs are 17 - 24
#define VLIDACOFFSET    9       // VLI dacs are 9-16#define RP1DACOFFSET    121     // RP1 dacs are 121 - 128
#define RP2DACOFFSET    1       // RP2 dacs are 1 - 8
#define ISETMDACOFFSET  131
#define TACREFDAC       133     // number of TACREF dac
#define VMAXDAC         134     // number of VMAX dac
#define DEBUG           0       //turn on/off printf debug statements
#define HOWMANY         350     //number of DB entries
#define FECSLOTS        16

struct channelParams {
    int   hiBalanced;
    int   loBalanced;
    short int test_status;
    short int lowGainBalance;
    short int highGainBalance;
};


struct cell {
    short per_cell;
    int cellno;
    double qlxbar, qlxrms;
    double qhlbar, qhlrms;
    double qhsbar, qhsrms;
    double tacbar, tacrms;
};

struct pedestal {
    short int channelnumber;
    short int per_channel;
    struct cell thiscell[16];
};

// pedestal testing information bits
#define PED_TEST_TAKEN                  0x1
#define PED_CH_HAS_PEDESTALS    0x2
#define PED_RMS_TEST_PASSED     0x4 
#define PED_PED_WITHIN_RANGE    0x8
#define PED_DATA_NO_ENABLE              0x10
#define PED_TOO_FEW_PER_CELL    0x20

// RGV's Special PMT bundle structure
/*PMT Bundle structure definition */
struct PMTBundle{
    unsigned int CdID;
    unsigned int ChID;
    unsigned int GtID;
    unsigned int CeID;
    unsigned int ErID;
    unsigned int ADC_Qlx;
    unsigned int ADC_Qhs;
    unsigned int ADC_Qhl;
    unsigned int ADC_TAC;
    unsigned long Word1;
    unsigned long Word2;
    unsigned long Word3;
};

typedef struct {
    unsigned short CrateID;
    unsigned short BoardID;
    unsigned short ChannelID;
    unsigned short CMOSCellID;
    unsigned long  GlobalTriggerID;
    unsigned short GlobalTriggerID2;
    unsigned short ADC_Qlx;
    unsigned short ADC_Qhs;
    unsigned short ADC_Qhl;
    unsigned short ADC_TAC;
    unsigned short CGT_ES16;
    unsigned short CGT_ES24;
    unsigned short Missed_Count;
    unsigned short NC_CC;
    unsigned short LGI_Select;
    unsigned short CMOS_ES16;
} FEC32PMTBundle;


//function prototypes
short getPedestal(struct pedestal *pedestals, struct channelParams *ChParams, int crate, uint32_t select_reg);

FEC32PMTBundle GetPMTBundle(int crate, uint32_t select_reg);
FEC32PMTBundle MakeBundleFromData(uint32_t *buffer);

int ExtPulser(unsigned long NumPulses);

unsigned int sGetBits(unsigned long value, unsigned int bit_start,
        unsigned int num_bits);


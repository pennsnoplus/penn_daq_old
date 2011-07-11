#ifndef PENN_DAQ_H
#define PENN_DAQ_H

typedef u_char  byte;

// ############### DEFINITIONS ###################	
#define MTC_XILINX_LOCATION "/home/neutrino/select_DAQ/daq_v3.04_c/data/mtcxilinx.rbt"

#define TRUE  1
#define FALSE 0

// what to return on failure, success
#define FAIL -1
#define SUCCESS 0
// number of xl3's allowed to be connected at once
#define MAX_XL3_CON 19
// number of control boards allowed to be connected at once
#define MAX_CONT_CON 1
// number of view clients allowed at once
#define MAX_VIEW_CON 3
// number of SBC/MTC boards/servers allowed to be connected at once
#define MAX_SBC_CON 1
// the port for XL3 boards to connect to, goes from 44601 for crate 0 to 44619 for crate 18
#define XL3_PORT 44601
// the port for the SBC board to connect to
#define SBC_PORT 44630
// the server for the sbc
#define SBC_SERVER "10.0.0.30"
// the port for a controller client to connect to
#define CONT_PORT 44600
// the port for view clients to connect to
#define VIEW_PORT 44599
// the total number of pending requests (10 is a good guestimate; this number isn't all that important)
#define MAX_PENDING_CONS 10
// the number of packet/socket pairs to be stored to be sent out
#define BACKLOG 100
// timeout values
#define SECONDS 20
#define USECONDS 0

// acknowledgement to send to control terminal upon successful reception of command
#define COMACK "_!_"

// the largest number of bytes in a packet
#define MAX_PACKET_SIZE 1444

// database stuff
#define DB_SERVER "http://localhost:5984"
#define DB_ADDRESS "localhost"
#define DB_PORT "5984"
#define DB_BASE_NAME "penndb1"
#define DB_VIEWDOC "_design/view_doc/_view"


#define kSBC_MaxPayloadSizeBytes    1024*400
#define kSBC_MaxMessageSizeBytes    256
#define MAX_ACKS_SIZE		    80
#define MAX_FEC_COMMANDS	    60000
#define NeedToSwap

// possible cmdID's for packets recieved by XL3 (from the DAQ)
#define CHANGE_MODE_ID		    (0x1)	// change mode
#define XL3_TEST_CMD_ID		    (0x2)	// do any of the test functions (check test_function.h)
#define SINGLE_CMD_ID		    (0x4)	// execute one cmd and get result
#define DAQ_QUIT_ID		    (0x5)	// quit
#define FEC_CMD_ID		    (0x6)	// put one or many cmds in cmd queue
#define FEC_TEST_ID		    (0x7)	// DAQ functions below ...
#define MEM_TEST_ID		    (0x8)
#define CRATE_INIT_ID		    (0x9)
#define VMON_START_ID		    (0xA)
#define BOARD_ID_READ_ID	    (0xB)
#define ZERO_DISCRIMINATOR_ID	    (0xC)
#define FEC_LOAD_CRATE_ADD_ID	    (0xD)
#define SET_CRATE_PEDESTALS_ID	    (0xE)
#define DESELECT_FECS_ID	    (0xF)
#define BUILD_CRATE_CONFIG_ID	    (0x10)
#define LOADSDAC_ID		    (0x11)
#define CALD_TEST_ID		    (0x12)
#define STATE_MACHINE_RESET_ID	    (0x13)
#define MULTI_CMD_ID		    (0x14)
#define DEBUGGING_MODE_ID	    (0x15)
#define READ_PEDESTALS_ID	    (0x16)
#define PONG_ID			    (0x17)
#define MULTI_LOADSDAC_ID	    (0x18)
#define CGT_TEST_ID		    (0x19)
#define LOADTACBITS_ID		    (0x1A)
#define CMOSGTVALID_ID		    (0x1B)
#define RESET_FIFOS_ID		    (0x1C)
#define READ_LOCAL_VOLTAGE_ID	    (0x23)
#define HV_READBACK_ID		    (0x24)
#define CHECK_TOTAL_COUNT_ID	    (0x25)

// possible cmdID's for packets sent by XL3 (to the DAQ)
#define CALD_RESPONSE_ID	    (0xAA)
#define PING_ID			    (0xBB)
#define MEGA_BUNDLE_ID	            (0xCC)
#define CMD_ACK_ID	            (0xDD)
#define MESSAGE_ID	            (0xEE)
#define STATUS_ID                   (0xFF)

			

// ############### DEFINITION OF STRUCTS ###################	

// structs copied from SBC_Cmds.h 
typedef struct {
	int32_t baseAddress;
	int32_t addressModifier;
	int32_t programRegOffset;
	uint32_t errorCode;
	int32_t fileSize;
} SNOMtc_XilinxLoadStruct;

typedef
    struct {
        uint32_t address;        /*first address*/
        uint32_t addressModifier;
        uint32_t addressSpace;
        uint32_t unitSize;        /*1,2,or 4*/
        uint32_t errorCode;    /*filled on return*/
        uint32_t numItems;        /*number of items to read*/
    }
SBC_VmeReadBlockStruct;

typedef
    struct {
        uint32_t address;        /*first address*/
        uint32_t addressModifier;
        uint32_t addressSpace;
        uint32_t unitSize;        /*1,2,or 4*/
        uint32_t errorCode;    /*filled on return*/
        uint32_t numItems;        /*number Items of data to follow*/
        /*followed by the requested data, number of items from above*/
    }
SBC_VmeWriteBlockStruct;


typedef
    struct {
        uint32_t destination;    /*should be kSBC_Command*/
        uint32_t cmdID;
        uint32_t numberBytesinPayload;
    }
SBC_CommandHeader;
typedef
    struct {
        uint32_t numBytes;                //filled in automatically
        SBC_CommandHeader cmdHeader;
        char message[kSBC_MaxMessageSizeBytes];
        char payload[kSBC_MaxPayloadSizeBytes];
    }
SBC_Packet;

// XL3 structs (same should be on the actual XL3) 
typedef
    struct {
	uint32_t cmd_num;
        uint16_t packet_num;
        uint8_t flags;
        uint32_t address;
        uint32_t data;
    }
FECCommand;

typedef
    struct {
	uint32_t howmany;
	FECCommand cmd[MAX_ACKS_SIZE];
    }
MultiFC;

typedef
    struct {
        //uint32_t destination;
	uint16_t packet_num;
        uint8_t packet_type;
	uint8_t num_bundles;
        //uint32_t numberBytesinPayload;
    }
XL3_CommandHeader;

typedef
    struct {
        //uint32_t numBytes;                //filled in automatically
        XL3_CommandHeader cmdHeader;
        char payload[kSBC_MaxPayloadSizeBytes];
    }
XL3_Packet;

typedef
    struct {
	uint32_t word1;
	uint32_t word2;
	uint32_t word3;
    }
PMTBundle;



// ############### vARIOUS GLOBALS ###################	

int count_d;
MultiFC multifc_buffer;
int multifc_buffer_full;
int command_number;
// allows print_send to send more than simple strings
char psb[5000];
// determines whether or not a log is opened for writing
int write_log; //(by default, false)
// log file variable for print_send()
FILE *ps_log_file;
FILE *cald_test_file;
// timeout for select functions (any function that reads/writes)
struct timeval delay_value;
// output file
FILE *fp;
int sbc_is_connected;
int current_location;

uint32_t current_hv_level;

// WHERE TO SAVE TO DATABASE - DEBUG ENTRIES OR HW ENTRIES
int db_debug;

typedef struct {
  uint16_t mb_id;
  uint16_t dc_id[4];
} hware_vals_t;

hware_vals_t crate_config[19][16];

////////////////////////////////////////////
// for bundle readout speed test          //
long int delta_t;
long int dt;
long int start_time;
long int new_time, old_time;
float rec_bytes;
float rec_fake_bytes;
//                                        //
////////////////////////////////////////////



// ############### FUNCTION DECLARATIONS #################	

void proc_xl3_rslt(XL3_Packet *packet, int crate_location,int numbytes);
int process_command(char *buffer);
int store_mega_bundle(int nbundles);
// various utilities
int get_xl3_location(int value, int array[]);
void set_delay_values(int seconds, int useconds);
void sigint_func(int sig);
void start_logging();
void stop_logging();
void SwapLongBlock(void* p, int32_t n);
void SwapShortBlock(void* p, int32_t n);

// from crate_init.c
int crate_init(char *buffer);
// from readout_test.c
int readout_test(char *buffer);
int readout_add_crate(char *buffer);
int readout_add_mtc(char *buffer);
int end_readout(char *buffer);
int change_pulser(char *buffer);
// from ped_run.c
int ped_run(char *buffer);
// from cgt_test_1.c
int cgt_test_1(char *buffer);
int mb_stability_test(char *buffer);
int fifo_test(char *buffer);
static void checkfifo(uint32_t *thediff, int crate_num, uint32_t select_reg,char* msg_buff);
// from cmos_m_gtvalid.c
int cmos_m_gtvalid(char *buffer);
// from cald_test.c
int cald_test(char *buffer);
// from chinj_test.c
int chinj_test(char *buffer);
// from final_test.c
int final_test(char *buffer);
// from disc_check.c
int disc_check(char *buffer);
#endif

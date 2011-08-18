#include <sys/socket.h>

struct sockaddr_in serv_addr;
// packet for MTC/SBC commands
SBC_Packet gPacket;

int sbc_control(int portno, struct hostent *server, char *buffer);
int mtc_reg_write(uint32_t address, uint32_t data);
int mtc_reg_read(uint32_t address, uint32_t *data);
int mtc_multi_write(uint32_t address, uint32_t data, int num_writes);
int do_mtc_cmd(SBC_Packet *aPacket);
int do_mtc_xilinx_cmd(SBC_Packet *aPacket);
int multi_softgt(uint32_t num);
int get_caen_data(char *buffer);

typedef struct {                                                                // structure required for card
    uint32_t hw_type_id;                // unique hardware identifier code
    uint32_t hw_mask[10];                           // hardware identifier mask to OR into data word
    uint32_t slot;                                          // slot identifier
    uint32_t crate;                                         // crate identifier
    uint32_t base_add;                                      // base addresses for each card
    uint32_t add_mod;                                       // address modifier (if needed)
    uint32_t deviceSpecificData[256];       // a card can use this block as needed.
    uint32_t next_Card_Index;                       // next card_info index to be read after this one.        
    uint32_t num_Trigger_Indexes;           // number of triggers for this card
    uint32_t next_Trigger_Index[3];         //card_info index for device specific trigger
} SBC_card_info;

typedef struct {
    uint32_t header;
    uint32_t total_cards;                   // total sum of all cards
    SBC_card_info
        card_info[2]; // Only 2 cards to read from, MTC and CAEN add more if needed
} SBC_crate_config;

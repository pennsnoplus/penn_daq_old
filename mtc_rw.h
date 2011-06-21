#include <sys/socket.h>

struct sockaddr_in serv_addr;
// packet for MTC/SBC commands
SBC_Packet gPacket;

int kill_SBC_process();
int connect_to_SBC(int portno, struct hostent *server);
int mtc_reg_write(uint32_t address, uint32_t data);
int mtc_reg_read(uint32_t address, uint32_t *data);
int mtc_multi_write(uint32_t address, uint32_t data, int num_writes);
int do_mtc_cmd(SBC_Packet *aPacket);
int do_mtc_xilinx_cmd(SBC_Packet *aPacket);
int multi_softgt(uint32_t num);

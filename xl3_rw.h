
int xl3_rw(uint32_t address, uint32_t data, uint32_t *result, int crate_num);
int do_xl3_cmd_no_response(XL3_Packet *aPacket, int xl3_num);
int do_xl3_cmd(XL3_Packet *aPacket, int xl3_num);
int receive_data(int num_cmds, int packet_num, int xl3_num, uint32_t *buffer);
//int sleep_with_messages(int xl3_num, char* history);
//int wait_while_messages(int xl3_num,int expected_type,char *history);
int receive_cald(int xl3_num, uint16_t *point_buf, uint16_t *adc_buf);
int read_from_tut(char* result);

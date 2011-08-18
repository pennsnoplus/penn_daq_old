
int debugging_mode(char *buffer, uint32_t onoff);
int deselect_fecs(int crate_num);
void send_pong(int xl3_num);
int change_mode(char *buffer);
int spec_cmd(char *buffer);
int add_cmd(char *buffer);
int sm_reset(char *buffer);
int hv_readback(char *buffer);
int read_local_voltage(char *buffer);
int hv_ramp_map(char *buffer);


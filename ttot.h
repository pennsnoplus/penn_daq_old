
#define RMPDEFAULT  120
#define RMPUPDEFAULT  115 //100 different than crate init default value //FIXME
#define VSIDEFAULT 120
#define VLIDEFAULT 120
#define MAX_RMP_VALUE 180
#define MIN_VSI_VALUE 50

#define NUM_PEDS 20
#define MAXTIME 1100
#define TUB_DELAY 60 // in ns

int get_ttot(char * buffer);
int set_ttot(char * buffer);
int disc_s_ttot(int crate, uint32_t slot_mask, int goal_time, uint16_t *allrmps,uint16_t *allvsis, uint16_t *times, int *errors);
int disc_m_ttot(int crate, uint32_t slot_mask, int start_time, uint16_t *disc_times);

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "penn_daq.h"
#include "mtc_util.h"
#include "net_util.h"
#include "db.h"
//#include "pouch.h"
//#include "json.h"


int mtc_init(char *buffer)
{

	if (sbc_is_connected == 0){
		sprintf(psb,"SBC not connected.\n");
		print_send(psb, view_fdset);
		return -1;
	}


	int result = 0;

	u_short lkwidth, pscale, pwid, coarse_delay;
	float freq, fine_delay, fdelay_set;
	int i;
	u_short raw_dacs[14];

	char *word;
	int x_init = 0;
	word = strtok(buffer, " ");
	while (word != NULL){
		if (word[0] == '-'){
			if (word[1] == 'x')
				x_init = 1;
			if (word[1] == 'h'){
				sprintf(psb,"Usage: mtc_init -x (enables xilinx load)\n");
				print_send(psb, view_fdset);
				return 0;
			}
		}
		word = strtok(NULL, " ");
	}

	printf("made it here fine\n");
	if (x_init == 1)
		mtc_xilinxload();

	mtc_t *mtc = ( mtc_t * ) malloc( sizeof(mtc_t));
	if ( mtc == ( mtc_t *) NULL )
	{
		print_send("error: malloc in mtc_init\n", view_fdset);
		free(mtc);
		return -1;
	}

	;
	pouch_request *response = pr_init();
	char get_db_address[500];
	sprintf(get_db_address,"http://%s:%s/%s/MTC_doc",DB_ADDRESS,DB_PORT,DB_BASE_NAME);
	pr_set_method(response, GET);
	pr_set_url(response, get_db_address);
	pr_do(response);
	if (response->httpresponse != 200){
		printf("Unable to connect to database. error code %d\n",(int)response->httpresponse);
		return -1;
	}
	JsonNode *doc = json_decode(response->resp.data);
	parse_mtc(doc,mtc);
	json_delete(doc);





	//unset all masks
	unset_gt_mask(MASKALL);
	unset_ped_crate_mask(MASKALL);
	unset_gt_crate_mask(MASKALL);

	// load the dacs
	mtc_cons *mtc_cons_ptr = ( mtc_cons * ) malloc( sizeof(mtc_cons));
	if ( mtc_cons_ptr == ( mtc_cons *) NULL )
	{
		print_send("error: malloc in mtc_init\n", view_fdset);
		free(mtc);
		free(mtc_cons_ptr);
		return -1;
	}

	for (i=0; i<=13; i++)
	{ 
		raw_dacs[i]=
			(u_short) mtc->mtca.triggers[i].threshold;
		mtc_cons_ptr->mtca_dac_values[i]=
			(((float)raw_dacs[i]/2048) * 5000.0) - 5000.0;
	}

	load_mtc_dacs(mtc_cons_ptr);

	// clear out the control register
	mtc_reg_write(MTCControlReg,0x0);

	// set lockout width
	lkwidth = (u_short)((~((u_short) mtc->mtcd.lockout_width) & 0xff)*20);
	result += set_lockout_width(lkwidth);

	// zero out gt counter
	set_gt_counter(0);

	// load prescaler
	pscale = ~((u_short)(mtc->mtcd.nhit100_lo_prescale)) + 1;
	result += set_prescale(pscale);

	// load pulser
	freq = 781250.0/(float)((u_long)(mtc->mtcd.pulser_period) + 1);
	result += set_pulser_frequency(freq);

	// setup pedestal width
	pwid = (u_short)(((~(u_short)(mtc->mtcd.pedestal_width)) & 0xff) * 5);
	result += set_pedestal_width(pwid);

	// setup PULSE_GT delays
	print_send("Setting up PULSE_GT delays...\n", view_fdset);
	coarse_delay = (u_short)(((~(u_short)(mtc->mtcd.coarse_delay))
				& 0xff) * 10);
	result += set_coarse_delay(coarse_delay);
	fine_delay = (float)(mtc->mtcd.fine_delay)*
		(float)(mtc->mtcd.fine_slope);
	fdelay_set = set_fine_delay(fine_delay);
	sprintf(psb, "PULSE_GET total delay has been set to %f\n",
			(float) coarse_delay+fine_delay+
			(float)(mtc->mtcd.min_delay_offset));
	print_send(psb, view_fdset);

	// load 10 MHz counter???? guess not

	// reset memory
	reset_memory();

	free(mtc);
	free(mtc_cons_ptr);

	if (result < 0) {
		print_send("errors in the MTC initialization!\n", view_fdset);
		return -1;
	}

	print_send("MTC finished initializing\n", view_fdset);
	pr_free(response);
	return 0;
}

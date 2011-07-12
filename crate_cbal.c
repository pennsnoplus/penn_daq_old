#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "include/dacNumber.h"
#include "include/xl_regs.h"

#include "penn_daq.h"
#include "fec_util.h"
#include "mtc_util.h"
#include "crate_cbal.h"
#include "net_util.h"
//#include "pouch.h"
//#include "json.h"


uint32_t *pmt_buf;


int crate_cbal(char * buffer)
{
	if (sbc_is_connected == 0){
		sprintf(psb,"SBC not connected.\n");
		print_send(psb, view_fdset);
		return -1;
	}
	int crate = 2;
	uint32_t slot_mask = 0x2000;
	uint32_t theDACs[50];
	uint32_t theDAC_Values[50];
	int num_dacs = 0;
	uint32_t chan_mask = 0xFFFFFFFF;
	int update_db = 0;
	int final_test = 0;
	char ft_ids[16][50];
	int i;
	char *words,*words2;
	words = strtok(buffer, " ");
	while (words != NULL){
		if (words[0] == '-'){
			if (words[1] == 'c'){
				words2 = strtok(NULL, " ");
				crate = atoi(words2);
			}else if (words[1] == 's'){
				words2 = strtok(NULL, " ");
				slot_mask = strtoul(words2,(char**)NULL,16);
			}else if (words[1] == 'd'){
				update_db = 1;
			}else if (words[1] == '#'){
				final_test = 1;
				for (i=0;i<16;i++){
					if ((0x1<<i) & slot_mask){
						words2 = strtok(NULL, " ");
						sprintf(ft_ids[i],"%s",words2);
					}
				}
			}else if (words[1] == 'l'){
				words2 = strtok(NULL, " ");
				chan_mask = strtoul(words2,(char**)NULL,16);
			}else if (words[1] == 'h'){
				sprintf(psb,"Usage: crate_cbal -c"
						" [crate_num] -s [slot_mask (hex)] -l [chan_mask] -d (update debug db)\n");
				print_send(psb, view_fdset);
				return 0;
			}
		}
		words = strtok(NULL, " ");
	}
	if (!update_db)
		print_send("Not writing balance values to DB.\n", view_fdset);
	else
		;

	int j,it,im,is;
	int error_flags[32];
	for (i=0;i<32;i++){
		error_flags[i] = 0;
	}

	//constants
	const short Max_channels = 32;
	const short Max_iterations = 40;
	// test value to set timing dacs to during test
	// roughly corresponds to 1.5 us GTV, 0.8-1.2us reset/sample.
	// also set VLI, RMP to values that are defaults
	// these setup values are also stored in DB-Skey 2.
	const short vsitestval = 0;
	const short isetmtestval = 85;
	const short rmp1testval = 100;
	const short vlitestval = 120;
	const short rmp2testval = 155;
	const short vmaxtestval = 203;
	const short tacreftestval = 72;


	uint32_t BalancedChs = 0x0; // which channels have been balanced;
	uint32_t activeChannels = chan_mask;
	uint32_t orig_activeChannels;

	//create pedestal structures
	struct pedestal x1[MAXCHAN], x2[MAXCHAN], tmp[MAXCHAN];
	struct pedestal x1l[MAXCHAN], x2l[MAXCHAN], tmpl[MAXCHAN];

	//three locations to store channel info
	struct pedestal *x1_ch, *x2_ch, *tmp_ch, *x1l_ch, *x2l_ch, *tmpl_ch;

	//pointer to dac values for each channel
	short *x1_bal, *x2_bal, *tmp_bal, *bestguess_bal;
	short ax1_bal[MAXCHAN], ax2_bal[MAXCHAN], atmp_bal[MAXCHAN],
		  abestguess_bal[MAXCHAN];

	float *f1, *f2; // the actual function
	float af1[MAXCHAN], af2[MAXCHAN];

	//keep individual channel info here.
	struct channelParams *ChParams;
	struct channelParams ChParam[32];

	//keep track of how often we loop through.
	short iterations = 0, returnValue = 0;

	//max acceptable diff btw qhl and qhs in same
	float acceptableDiff = 10;

	//other stuff
	float fmean1, fmean2;
	const float numcells = 16;
	int vbal_temp[2][32*16];	

	hware_vals_t theHWconf[FECSLOTS];

	//packet stuff
	uint32_t select_reg;
	uint32_t result;
	XL3_Packet packet;
	uint32_t *p;
	p = (uint32_t *) packet.payload;

	// END variables --------------

	print_send("-------------------------------\n",view_fdset);
	print_send("Balancing channels now.\n"
			"High gain balance first.\n", view_fdset);

	//initialization
	x1_ch = &x1[0];
	x2_ch = &x2[0];
	tmp_ch = &tmp[0];
	x1l_ch = &x1l[0];
	x2l_ch = &x2l[0];
	tmpl_ch = &tmpl[0];
	x1_bal = &ax1_bal[0];
	x2_bal = &ax2_bal[0];
	tmp_bal = &atmp_bal[0];
	bestguess_bal = &abestguess_bal[0];
	f1 = &af1[0];
	f2 = &af2[0];
	ChParams = &ChParam[0];

	//setup pulser for soft GT mode by setting frequency to zero
	setup_pedestals(0.0,50,125,0);
	unset_gt_crate_mask(MASKALL);
	unset_ped_crate_mask(MASKALL); // unmask all crates
	//set_gt_crate_mask(0x1<<crate);
	//set_ped_crate_mask(0x1<<crate); // add our crate to mask
	set_gt_crate_mask(MASKALL);
	set_ped_crate_mask(MASKALL); // add our crate to mask
	set_gt_crate_mask(MSK_TUB);
	set_ped_crate_mask(MSK_TUB); // add TUB to mask


	// malloc some stuff	
	pmt_buf = (uint32_t *) malloc( TWOTWENTY*sizeof(u_long));

	//END initialization --------


	//loop over slots
	for (is=0;is<16;is++){
		if ((0x1<<is) & slot_mask){

			select_reg = FEC_SEL*is;
			xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0xF,&result,crate); // clear fec

			sprintf(psb, "--------------------------------\n");
			sprintf(psb+strlen(psb), "Balancing Crate %hu, Slot %hu. \n",crate,is);
			print_send(psb, view_fdset);

			//////////////////////////
			// INITIALIZE VARIABLES //
			//////////////////////////

			//initialize structs -- pedestal struct initialized in getPedestal.
			iterations = 0;
			BalancedChs = 0x0;
			for (i=0;i<32;i++){
				ChParam[i].test_status = 0x0; // zero means test not passed
				ChParam[i].hiBalanced = 0; // set to false
				ChParam[i].loBalanced = 0; // set to false
				ChParam[i].highGainBalance = 0; // reset
				ChParam[i].lowGainBalance = 0; // reset
			}

			for (i=0;i<32;i++){
				f1[i] = f2[i] = 0;
				// I will assume that a large setting of dac balance conditions
				// gives the balance a positive slope. Therefore the x2 series
				// will have Qhl > Qhs and f = qhl - qhs > 0, and the x1 series
				// will have Qhl < Qhs and therefore f < 0.

				*(x1_bal + i) = 0x32; // low initial setting
				*(x2_bal + i) = 0xBE; // high initial setting
			}

			// init other stuff
			orig_activeChannels = activeChannels;

			// timing setup for test.
			// set VSI, VLI to a long time during test.
			// see variable section above for details.
			if (DEBUG){
				printf(SNTR_TOOLS_DIALOG_DIVIDER);
				printf("Setting up timing for test.\n");
			}
			for (i=0;i<8;i++){
				theDACs[num_dacs] = d_rmp[i];
				theDAC_Values[num_dacs] = rmp2testval;
				num_dacs++;
				theDACs[num_dacs] = d_rmpup[i];
				theDAC_Values[num_dacs] = rmp1testval;
				num_dacs++;
				theDACs[num_dacs] = d_vsi[i];
				theDAC_Values[num_dacs] = vsitestval;
				num_dacs++;
				theDACs[num_dacs] = d_vli[i];
				theDAC_Values[num_dacs] = vlitestval;
				num_dacs++;
			}

			// now CMOS timing for GTValid
			for (i = 0; i < 2; i++) {
				theDACs[num_dacs] = d_isetm[i];
				theDAC_Values[num_dacs] = isetmtestval;
				num_dacs++;
			}
			theDACs[num_dacs] = d_tacref;
			theDAC_Values[num_dacs] = tacreftestval;
			num_dacs++;
			theDACs[num_dacs] = d_vmax;
			theDAC_Values[num_dacs] = vmaxtestval;
			num_dacs++;
			if (multiloadsDac(num_dacs,theDACs,theDAC_Values,crate,select_reg) != 0){
				print_send("Error loading Dacs. Aborting.\n",view_fdset);
				free(pmt_buf);
				return REG_ERROR;
			}
			num_dacs = 0;


			////////////////////////////
			// CALCULATE ZERO BALANCE //
			////////////////////////////

			// done setting up the timing.
			// first need to calculate initial conditions

			if (DEBUG) {
				print_send(SNTR_TOOLS_DIALOG_DIVIDER, view_fdset);
				print_send("Starting testing run.\n", view_fdset);
			}
			// get data for balance at zero
			for (i = 0; i <= 31; i++) {
				// only do active channels
				if ((activeChannels & (0x1L << i)) != 0) {
					theDACs[num_dacs] = d_vbal_hgain[i];
					theDAC_Values[num_dacs] = x1_bal[i];
					num_dacs++;
				}
			}
			if (multiloadsDac(num_dacs,theDACs,theDAC_Values,crate,select_reg) != 0){
				print_send("Error loading Dacs. Aborting.\n",view_fdset);
				free(pmt_buf);
				return REG_ERROR;
			}
			num_dacs = 0;

			// x1 will be the low data point.
			// grab pedestal data with these settings.
			if (getPedestal(x1_ch, ChParams, crate, select_reg) != 0) {
				print_send(PED_ERROR_MESSAGE, view_fdset);
				//SNO_printerr(5, INIT_FAC, err_str);
				free(pmt_buf);
				return PED_ERROR;
			}


			///////////////////////////
			// CALCULATE MAX BALANCE //
			///////////////////////////

			// get data for balance at 0xff
			for (i = 0; i <= 31; i++) {
				// only do active channels
				if ((activeChannels & (0x1L << i)) != 0) {
					theDACs[num_dacs] = d_vbal_hgain[i];
					theDAC_Values[num_dacs] = x2_bal[i];
					num_dacs++;
				}
			}
			if (multiloadsDac(num_dacs,theDACs,theDAC_Values,crate,select_reg) != 0){
				print_send("Error loading Dacs. Aborting.\n",view_fdset);
				free(pmt_buf);
				return REG_ERROR;
			}
			num_dacs = 0;

			// x2 will be the high data point.
			if (getPedestal(x2_ch, ChParams, crate,select_reg) != 0) {
				print_send(PED_ERROR_MESSAGE, view_fdset);
				//SNO_printerr(5, INIT_FAC, err_str);
				free(pmt_buf);
				return PED_ERROR;
			}
			// end initial conditions.


			/////////////////////////
			// LOOP UNTIL BALANCED //
			/////////////////////////

			// loop until the balance is set.
			do {
				// some checking to make sure we don't do this forever.
				if (iterations++ > (Max_iterations)) {
					if (DEBUG) {
						print_send("Too many iterations, "
								"aborting with some channels unbalanced.\n",
								view_fdset);
						//SNO_printerr(5, INIT_FAC, err_str);
						print_send("Making best guess for unbalanced channels.\n",
								view_fdset);
						//SNO_printerr(5, INIT_FAC, err_str);
					}
					// get best guess from those channels that remain unbalanced
					for (it = 0; it < 32; it++) {
						// if this channel is unbalanced
						if (ChParam[it].hiBalanced == 0) {	
							// then we take the best guess and use that.
							ChParam[it].highGainBalance = bestguess_bal[it];
						}
					}
					break;
				}

				// loop over channels
				for (j = 0; j <= 31; j++) {
					// if this channel is active and was not declared balanced before
					if (((activeChannels & (0x1L << j)) != 0) &&
							(ChParam[j].hiBalanced == 0)) {
						// then calculate the two data points, high and low.

						// average data over all cells to reduce effects of badly
						// matched cells -RGV
						fmean1= 0;
						fmean2= 0;
						for (im = 0; im < 16; im++) {
							fmean1 += x1_ch[j].thiscell[im].qhlbar 
								- x1_ch[j].thiscell[im].qhsbar;
							fmean2 += x2_ch[j].thiscell[im].qhlbar 
								- x2_ch[j].thiscell[im].qhsbar;
						}
						f1[j] = fmean1 / numcells;
						f2[j] = fmean2 / numcells;

						if (DEBUG) {
							sprintf(psb, "Iteration %d, Ch(%2i): f1 = %+7.1f, x1 = %3i, f2 = %+7.1f,"
									" x2 = %3i\n", iterations, j, f1[j], x1_bal[j], f2[j], x2_bal[j]);

							print_send(psb, view_fdset);
							//SNO_printerr(5, INIT_FAC, err_str);
						}

						// check to assure we straddle root, i.e. channel is balanceable
						// i.e. they both have the same sign on first run
						if (((f1[j] * f2[j]) > 0.0) && (iterations == 1)) {	 
							if (DEBUG) {
								printf("Error:  Ch(%2i) does not appear"
										" balanceable.\n", j);
								//SNO_printerr(5, INIT_FAC, err_str);
							}
							// turn off this one and go on.
							activeChannels &= ~(0x1UL << j);	

							returnValue += 100; 
							// if returnvalue is gt. 100, we know some channels were 
							// unbalanceable

						}
						if (fabs(f2[j]) < acceptableDiff) { // we found bal w/ f2[j] 

							BalancedChs |= 0x1L << j;		// turn on bit since balanced.

							ChParam[j].hiBalanced = 1;
							ChParam[j].highGainBalance = x2_bal[j];
							// turn this off to speed up retrieving data.
							activeChannels &= ~(0x1UL << j);	
							// this will not enable the pedestal on this channel.

						}
						else if (fabs(f1[j]) < acceptableDiff) { // we found bal w/ f1[j]

							BalancedChs |= 0x1L << j; // turn on bit since it's balanced.

							ChParam[j].hiBalanced = 1;
							ChParam[j].highGainBalance = x1_bal[j];
							activeChannels &= ~(0x1UL << j);	// turn off to speed up
							// this will not enable the pedestal on this channel.

						}
						else {		// we haven't found balance.
							// data point between the two locations -- false
							// position method.  this is new dac value.  false
							// position, x_guess = x1 + dx* f(x1) / (f(x1) - f(x2))

							tmp_bal[j] = x1_bal[j] + (x2_bal[j] - x1_bal[j])
								* (f1[j] / (f1[j] - f2[j]));

							// keep track of best guess
							if (fabs(f1[j]) < fabs(f2[j]))	
								// then make best guess for which one we should take
								bestguess_bal[j] = x1_bal[j];
							else
								bestguess_bal[j] = x2_bal[j];

							if (tmp_bal[j] == x2_bal[j]) {	// i.e., we are stuck in a rut

								//		short kick = (short) (rand() % 35) + 15;
								short kick = (short) (rand() % 35) + 150;
								tmp_bal[j] = (tmp_bal[j] >= 45) ? 
									(tmp_bal[j] - kick) : (tmp_bal[j] + kick);
								if (DEBUG) {
									sprintf(psb, "Ch(%2i) in local trap.  "
											"Nudging by %2i\n", j, kick);
									print_send(psb, view_fdset);
									//SNO_printerr(5, INIT_FAC, err_str);
								}
							}
							// this algorithm can "run away" to infinity so make sure
							// we stay in bounds
							if (tmp_bal[j] > 255)
								tmp_bal[j] = 255;
							else if (tmp_bal[j] < 0)
								tmp_bal[j] = 0;
							theDACs[num_dacs] = d_vbal_hgain[j];
							theDAC_Values[num_dacs] = tmp_bal[j];
							num_dacs++;
							// now process rest of this loop before running
							// pedestal once to get all the rest of the
							// information.
						}
					} // end loop over active channels
				} // end loop over channels


				if (multiloadsDac(num_dacs,theDACs,theDAC_Values,crate,select_reg) != 0){
					print_send("Error loading Dacs. Aborting.\n",view_fdset);
					free(pmt_buf);
					return REG_ERROR;
				}
				num_dacs = 0;

				// do a pedestal run with new balance unless all chs are balanced, 
				// i.e. none are active
				if (activeChannels != 0x0) {
					if (DEBUG) {
						sprintf(psb, "Next step, iteration %i.\n", iterations);
						print_send(psb, view_fdset);
						//SNO_printerr(7, INIT_FAC, err_str);
					}
					if (getPedestal(tmp_ch, ChParams, crate,select_reg) != 0) {
						printf(PED_ERROR_MESSAGE);
						//SNO_printerr(5, INIT_FAC, err_str);
						free(pmt_buf);
						return PED_ERROR;
					}

					for (j = 0; j <= 31; j++) {
						if ((activeChannels & (0x1L << j)) != 0) {
							// secant method -- always keep last two points.
							x1_ch[j] = x2_ch[j];
							x1_bal[j] = x2_bal[j];
							x2_ch[j] = tmp_ch[j];
							x2_bal[j] = tmp_bal[j];
							// now we're ready go on to next channel.....  
						}
					}
				}
			} while (BalancedChs != orig_activeChannels); // loop utl the bal'd chs 
			// equal the active channels.      

			// let people know what's going on
			if (DEBUG) {
				print_send("End of high gain balancing.\n", view_fdset);
				//SNO_printerr(7, INIT_FAC, err_str);
				print_send(SNTR_TOOLS_DIALOG_DIVIDER, view_fdset);
				//SNO_printerr(7, INIT_FAC, err_str);
				print_send("Starting low gain balance run.\n", view_fdset);
				//SNO_printerr(7, INIT_FAC, err_str);
			}
			// reset this
			activeChannels = orig_activeChannels;

			// now do low gain loop. basic copy of above, with slight exception
			// that there is an additional step (toggle LGI_SEL bit) to set the
			// second balance point.  Even slower.  yikes.
			// need additional info here

			// rezero these guys
			for (i = 0; i <= 31; i++) {
				f1[i] = f2[i] = 0;
				// I will assume that a large setting of dac balance conditions
				// gives the balance a positive slope. Therefore the x2 series
				// will have Qhl > Qhs and f = qhl - qhs > 0, and the x1 series
				// will have Qhl < Qhs and therefore f < 0.

				//Hacked by cK.  Original settings were 0 and FF
				*(x1_bal + i) = 0x32;	// low initial setting
				*(x2_bal + i) = 0xBE;	// high initial setting
			}

			// get data for balance at zero
			for (i = 0; i <= 31; i++) {
				// only do active channels
				if ((activeChannels & (0x1L << i)) != 0) {
					theDACs[num_dacs] = d_vbal_lgain[i];
					theDAC_Values[num_dacs] = x1_bal[i];
					num_dacs++;
				}
			}
			if (multiloadsDac(num_dacs,theDACs,theDAC_Values,crate,select_reg) != 0){
				print_send("Error loading Dacs. Aborting.\n",view_fdset);
				free(pmt_buf);
				return REG_ERROR;
			}
			num_dacs = 0;


			// x1,x1l  will be the low data point.
			// grab pedestal data with these settings.
			if (getPedestal(x1_ch, ChParams, crate,select_reg) != 0) {
				print_send(PED_ERROR_MESSAGE, view_fdset);
				//SNO_printerr(5, INIT_FAC, err_str);
				free(pmt_buf);
				return PED_ERROR;
			}
			// now switch LGI sel bit
			xl3_rw(CMOS_LGISEL_R + select_reg + WRITE_REG,0x1,&result,crate);

			// now get low gain long integrate
			if (getPedestal(x1l_ch, ChParams, crate,select_reg) != 0) {
				print_send(PED_ERROR_MESSAGE, view_fdset);
				//SNO_printerr(5, INIT_FAC, err_str);
				free(pmt_buf);
				return PED_ERROR;
			}
			// now switch LGI sel bit back
			xl3_rw(CMOS_LGISEL_R + select_reg + WRITE_REG,0x0,&result,crate);

			// get data for balance at 0xff
			for (i = 0; i <= 31; i++) {
				// only do active channels
				if ((activeChannels & (0x1L << i)) != 0) {
					theDACs[num_dacs] = d_vbal_lgain[i];
					theDAC_Values[num_dacs] = x2_bal[i];
					num_dacs++;
				}
			}
			if (multiloadsDac(num_dacs,theDACs,theDAC_Values,crate,select_reg) != 0){
				print_send("Error loading Dacs. Aborting.\n",view_fdset);
				free(pmt_buf);
				return REG_ERROR;
			}
			num_dacs = 0;


			// x2 will be the high data point.
			if (getPedestal(x2_ch, ChParams, crate,select_reg) != 0) {
				print_send(PED_ERROR_MESSAGE, view_fdset);
				//SNO_printerr(5, INIT_FAC, err_str);
				free(pmt_buf);
				return PED_ERROR;
			}
			// now switch LGI sel bit
			xl3_rw(CMOS_LGISEL_R + select_reg + WRITE_REG,0x1,&result,crate);

			// now get low gain long integrate
			if (getPedestal(x2l_ch, ChParams, crate,select_reg) != 0) {
				print_send(PED_ERROR_MESSAGE, view_fdset);
				//SNO_printerr(5, INIT_FAC, err_str);
				free(pmt_buf);
				return PED_ERROR;
			}
			// now switch LGI sel bit back
			xl3_rw(CMOS_LGISEL_R + select_reg + WRITE_REG,0x0,&result,crate);

			// end initial conditions.
			iterations = 0;		// reset this

			BalancedChs = 0x0;

			///////////////////////////////////////
			// LOOP AGAIN UNTIL BALANCED FOR LGI //
			///////////////////////////////////////

			// loop until the balance is set.
			do {
				// some checking to make sure we don't do this forever.
				if (iterations++ > (Max_iterations)) {
					if (DEBUG) {
						print_send("Too many iterations -- aborting with some"
								" channels unbalanced.\n", view_fdset);
						//SNO_printerr(7, INIT_FAC, err_str);
						print_send("Making best guess for unbalanced channels.\n", view_fdset);
						//SNO_printerr(7, INIT_FAC, err_str);
					}
					// get best guess from those channels that remain unbalanced
					for (it = 0; it < 32; it++) {
						if (ChParam[it].loBalanced == 0) {	// if this ch is unbalanced
							// then we take the best guess and use that.
							ChParam[it].lowGainBalance = bestguess_bal[it];
						}
					}
					break;
				}

				// loop over channels
				for (j = 0; j <= 31; j++) {
					// if this channel is active and was not declared balanced before
					if (((activeChannels & (0x1L << j)) != 0) &&
							(ChParam[j].loBalanced == 0)) {

						// then calculate the two data points, high and low.
						//average data over all cells to reduce effects of badly 
						// matched cells -RGV
						fmean1= 0;
						fmean2= 0;
						for (im = 0; im < 16; im++) {
							fmean1 += (x1_ch[j].thiscell[im].qlxbar 
									- x1l_ch[j].thiscell[im].qlxbar);
							fmean2 += (x2_ch[j].thiscell[im].qlxbar 
									- x2l_ch[j].thiscell[im].qlxbar);
						}
						f1[j] = fmean1 / numcells;
						f2[j] = fmean2 / numcells;

						if (DEBUG) {
							sprintf(psb, "Ch(%2i): f1 = %+7.1f, x1 = %3i, f2 = %+7.1f"
									", x2 = %3i\n", j, f1[j], x1_bal[j], f2[j], x2_bal[j]);
							print_send(psb, view_fdset);
							//SNO_printerr(7, INIT_FAC, err_str);
						}
						// check to assure we straddle root, i.e. channel is balanceable
						if (((f1[j] * f2[j]) > 0.0) && (iterations == 1)) { 
							//they both have the same sign on first run

							if (DEBUG) {
								sprintf(psb, "Error:  Ch(%2i) does not appear"
										" balanceable.\n", j);
								print_send(psb, view_fdset);
								//SNO_printerr(7, INIT_FAC, err_str);
							}
							activeChannels &= ~(0x1UL << j);	// turn off this one and go on.

							returnValue += 100; // if returnvalue is gt. 100, we know some 
							// channels were unbalanceable

						}
						if (fabs(f2[j]) < acceptableDiff) { // we found bal using f2[j] 

							BalancedChs |= 0x1L << j;		// turn on bit as it's balc'd.

							ChParam[j].loBalanced = 1;
							ChParam[j].lowGainBalance = x2_bal[j];
							// turn this off to speed up retrieving data.
							activeChannels &= ~(0x1UL << j);	
							// this will not enable the pedestal on this channel.

						}
						else if (fabs(f1[j]) < acceptableDiff) {	// we found  balance using f1[j]

							BalancedChs |= 0x1L << j;		// turn on this bit since it's balanced.

							ChParam[j].loBalanced = 1;
							ChParam[j].lowGainBalance = x1_bal[j];
							activeChannels &= ~(0x1UL << j);	// turn this off to speed up retrieving data.
							// this will not enable the pedestal on this channel.

						}
						else {		// we haven't found balance.
							// data point between the two locations -- false
							// position method.  this is new dac value.  false
							// position, x_guess = x1 + dx* f(x1) / (f(x1) - f(x2))
							// see numerical recipes.

							tmp_bal[j] = x1_bal[j] + 
								(x2_bal[j] - x1_bal[j]) * (f1[j] / (f1[j] - f2[j]));
							if (tmp_bal[j] == x2_bal[j]) {	// i.e., we are stuck in a rut

								//		short kick = (short) (rand() % 35) + 15;
								short kick = (short) (rand() % 35) + 150;
								tmp_bal[j] = (tmp_bal[j] >= 45) 
									? (tmp_bal[j] - kick) : (tmp_bal[j] + kick);
								if (DEBUG) {
									sprintf(psb, "Ch(%2i) in local trap.  Nudging by %2i\n", 
											j, kick);
									print_send(psb, view_fdset);
									//SNO_printerr(7, INIT_FAC, err_str);
								}

							}
							// keep track of best guess
							// then make best guess for which one we should take
							if (fabs(f1[j]) < fabs(f2[j]))	
								bestguess_bal[j] = x1_bal[j];
							else
								bestguess_bal[j] = x2_bal[j];
							// this algorithm can "run away" to infinity so make sure
							// we stay in bounds
							if (tmp_bal[j] > 255)
								tmp_bal[j] = 255;
							else if (tmp_bal[j] < 0)
								tmp_bal[j] = 0;
							theDACs[num_dacs] = d_vbal_lgain[j];
							theDAC_Values[num_dacs] = tmp_bal[j];
							num_dacs++;
							// now process rest of this loop before running
							// pedestal once to get all the rest of the
							// information.
						}
					}
				}
				if (multiloadsDac(num_dacs,theDACs,theDAC_Values,crate,select_reg) != 0){
					print_send("Error loading Dacs. Aborting.\n",view_fdset);
					free(pmt_buf);
					return REG_ERROR;
				}
				num_dacs = 0;


				// do a pedestal run with this new balance unless all 
				// channels are balanced, 
				// i.e. none are active
				if (activeChannels != 0x0) {
					if (DEBUG) {
						sprintf(psb, "Next step, iteration %i.\n", iterations + 1);
						print_send(psb, view_fdset);
						//SNO_printerr(7, INIT_FAC, err_str);
					}
					if (getPedestal(tmp_ch, ChParams, crate,select_reg) != 0) {
						print_send(PED_ERROR_MESSAGE, view_fdset);
						//SNO_printerr(5, INIT_FAC, err_str);
						free(pmt_buf);
						return PED_ERROR;
					}
					// now switch LGI sel bit
					xl3_rw(CMOS_LGISEL_R + select_reg + WRITE_REG,0x1,&result,crate);

					// now get low gain long integrate
					if (getPedestal(tmpl_ch, ChParams, crate,select_reg) != 0) {
						print_send(PED_ERROR_MESSAGE, view_fdset);
						//SNO_printerr(5, INIT_FAC, err_str);
						free(pmt_buf);
						return PED_ERROR;
					}
					// now switch LGI sel bit back
					xl3_rw(CMOS_LGISEL_R + select_reg + WRITE_REG,0x0,&result,crate);

					for (j = 0; j <= 31; j++) {
						if ((activeChannels & (0x1L << j)) != 0) {
							// secant method -- always keep last two points.
							x1_ch[j] = x2_ch[j];
							x1l_ch[j] = x2l_ch[j];
							x1_bal[j] = x2_bal[j];
							x2_ch[j] = tmp_ch[j];
							x2l_ch[j] = tmpl_ch[j];
							x2_bal[j] = tmp_bal[j];
						}
					}
				}
			} while (BalancedChs != orig_activeChannels); // loop until 
			// the balanced channels equal the active channels.      


			// ----------------------------------------
			if (DEBUG) {
				print_send("End of balancing loops.\n", view_fdset);
				//SNO_printerr(5, INIT_FAC, err_str);
			}
			// reset this
			activeChannels = orig_activeChannels;
			print_send("\nFinal VBAL table.\n", view_fdset);


			///////////////////////
			// PRINT OUT RESULTS //
			///////////////////////

			// now report some results, and store in DB struct if requested
			for (j = 0; j <= 31; j++) {
				if ((activeChannels & (0x1L << j)) != 0) {
					if ((ChParam[j].hiBalanced == 1) && (ChParam[j].loBalanced == 1)) {
						sprintf(psb, "Ch %2i High: %3i. low: %3i -> balanced \n", j,
								ChParam[j].highGainBalance, ChParam[j].lowGainBalance);
						print_send(psb, view_fdset);
						//SNO_printerr(5, INIT_FAC, err_str);

						if ((ChParam[j].highGainBalance == 255) 
								|| (ChParam[j].highGainBalance == 0)
								|| (ChParam[j].lowGainBalance == 255) 
								|| (ChParam[j].lowGainBalance == 0)) {

							if(ChParam[j].highGainBalance == 255) 
								ChParam[j].highGainBalance = 150;
							if(ChParam[j].highGainBalance == 0) 
								ChParam[j].highGainBalance = 150;
							if(ChParam[j].lowGainBalance == 255) 
								ChParam[j].lowGainBalance = 150;
							if(ChParam[j].lowGainBalance == 0) 
								ChParam[j].lowGainBalance = 150;

							print_send(" >>Extreme balance, setting to 150 ", view_fdset);
							error_flags[j] = 1;
						}
						if ((ChParam[j].highGainBalance > 225) 
								|| (ChParam[j].highGainBalance < 50)
								|| (ChParam[j].lowGainBalance > 225) 
								|| (ChParam[j].lowGainBalance < 50)) {
							print_send(" >>Warning: extreme balance value. ", view_fdset);
							error_flags[j] = 2;
						}

						// store for db
						vbal_temp[0][j] = ChParam[j].highGainBalance;
						vbal_temp[1][j] = ChParam[j].lowGainBalance;
					}
					// partially balanced
					else if ((ChParam[j].hiBalanced == 1) 
							|| (ChParam[j].loBalanced == 1)) {
						error_flags[j] = 3;
						sprintf(psb, "Ch %2i High: %3i. low: %3i -> part balanced, set to 150 if extreme\n",
								j, ChParam[j].highGainBalance, ChParam[j].lowGainBalance);
						print_send(psb, view_fdset);
						//SNO_printerr(5, INIT_FAC, err_str);

						//set to 150 if extreme
						if(ChParam[j].highGainBalance == 255) 
							ChParam[j].highGainBalance = 150;
						if(ChParam[j].highGainBalance == 0) 
							ChParam[j].highGainBalance = 150;
						if(ChParam[j].lowGainBalance == 255) 
							ChParam[j].lowGainBalance = 150;
						if(ChParam[j].lowGainBalance == 0) 
							ChParam[j].lowGainBalance = 150;

						// store for db
						vbal_temp[0][j] = ChParam[j].highGainBalance;
						vbal_temp[1][j] = ChParam[j].lowGainBalance;
						returnValue += 1;	// i.e. failure

					}else {		// unbalanced
						error_flags[j] = 4;

						sprintf(psb, "Ch %2i                     -> unbalanced, set to 150\n", j);
						print_send(psb, view_fdset);
						//SNO_printerr(5, INIT_FAC, err_str);
						//if failed, set to 150
						ChParam[j].highGainBalance = 150; 
						ChParam[j].lowGainBalance  = 150;

						// store for db
						vbal_temp[0][j] = ChParam[j].highGainBalance;
						vbal_temp[1][j] = ChParam[j].lowGainBalance;

						returnValue += 1;	// i.e. failure
					}//end of switch over balanced state
				}//end of loop for masked channels
			}//end of loop over channels for printout, putting results in db struct
			xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0xF,&result,crate); //golden rule #3
			deselect_fecs(crate);	//golden rule #2

			///////////////
			// UPDATE DB //
			///////////////

			//now lets update the database entry for this slot
			if (update_db){
				printf("updating the database\n");
				JsonNode *newdoc = json_mkobject();
				json_append_member(newdoc,"type",json_mkstring("crate_cbal"));
				JsonNode* vbal_high_new = json_mkarray();
				JsonNode* vbal_low_new = json_mkarray();
				JsonNode* error_new = json_mkarray();
				int fail_flag = 0;
				for (i=0;i<32;i++){
					json_append_element(vbal_high_new,json_mknumber((double)vbal_temp[0][i]));
					json_append_element(vbal_low_new,json_mknumber((double)vbal_temp[1][i]));
					if (error_flags[i] == 0)
						json_append_element(error_new,json_mkstring("none"));
					else if (error_flags[i] == 1)
						json_append_element(error_new,json_mkstring("Extreme balance set to 150"));
					else if (error_flags[i] == 2)
						json_append_element(error_new,json_mkstring("Extreme balance values"));
					else if (error_flags[i] == 3)
						json_append_element(error_new,json_mkstring("Partially balanced"));
					else if (error_flags[i] == 4)
						json_append_element(error_new,json_mkstring("Unbalanced, set to 150"));
					if (error_flags[i] != 0)
						fail_flag = 1;

				}
				json_append_member(newdoc,"vbal_low",vbal_low_new);
				json_append_member(newdoc,"vbal_high",vbal_high_new);
				json_append_member(newdoc,"errors",error_new);
				if (fail_flag == 0){
					json_append_member(newdoc,"pass",json_mkstring("yes"));
				}else{
					json_append_member(newdoc,"pass",json_mkstring("no"));
				}
				if (final_test)
					json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[is]));	
				post_debug_doc(crate,is,newdoc);
				json_delete(newdoc); // delete the head
			}

		}//end of masked slot loop
	}// end loop over slots


	print_send("End of balanceChannels.\n", view_fdset);
	//SNO_printerr(7, INIT_FAC, err_str);
	print_send("**********************************\n",view_fdset);
	//SNO_printerr(7, INIT_FAC, err_str);

	free(pmt_buf);

	return returnValue;

}				// end balanceChannels












////////////////////////////////////////////////////////////
//   CRATE_CBAL UTILITIES                                 //
////////////////////////////////////////////////////////////





// getPedestal -- returns pedestal in form of channels struct.    
// error checking and returns should be done in the calling
// function.
//short getPedestal(pedestal_t *pedestals )
short getPedestal(struct pedestal *pedestals,
		struct channelParams *ChParams, int crate, uint32_t select_reg)
{
	// ----------------------> variables
	u_long i, j;
	uint32_t regAddress, dataValue;
	uint32_t WordsInMem, currentWord, Max_errors;
	uint16_t dram_error=0;
	int FirstWord = 1;
	short num_errors = 0, num_events = 0;
	FEC32PMTBundle PMTdata;
	XL3_Packet packet;
	uint32_t activeChannels;
	uint32_t NumPulses;
	uint32_t Min_num_words;
	double x;

	// max acceptable RMS for test.  
	float Max_RMS_Qlx;
	float Max_RMS_Qhl;
	float Max_RMS_Qhs;
	float Max_RMS_TAC;

	Max_RMS_Qlx = 2.0;
	Max_RMS_Qhl = 2.0;
	Max_RMS_Qhs = 10.0;
	Max_RMS_TAC = 3.0;
	NumPulses = 25 * 16; //want a multiple of number of cells (16)

	printf(".");
	fflush(stdout);

	Min_num_words = (NumPulses - 25) * 3UL * 32UL; //check number of bundles

	activeChannels = 0xffffffff;	//enable all channels

	Max_errors = 250; // max number of errors before aborting DRAM readout.

	uint32_t result;

	// ----------------------> end variables

	// make sure pedestal pointer is not null and initalize structs.
	if (pedestals == 0) {
		print_send("Error:  null pointer passed to getPedestal! Aborting.\n", view_fdset);
		return 666;			// the return code of the beast.
	}
	for (i = 0; i <= 31; i++) {
		// pedestal struct
		pedestals[i].channelnumber = i;	// channel numbers start at 0 !!!

		pedestals[i].per_channel = 0;

		for (j = 0; j <= 15; j++) {
			pedestals[i].thiscell[j].cellno = j;
			pedestals[i].thiscell[j].per_cell = 0;
			pedestals[i].thiscell[j].qlxbar = 0;
			pedestals[i].thiscell[j].qlxrms = 0;
			pedestals[i].thiscell[j].qhlbar = 0;
			pedestals[i].thiscell[j].qhlrms = 0;
			pedestals[i].thiscell[j].qhsbar = 0;
			pedestals[i].thiscell[j].qhsrms = 0;
			pedestals[i].thiscell[j].tacbar = 0;
			pedestals[i].thiscell[j].tacrms = 0;
		}
	}
	// end initalization

	// reset board -- don't do full reset, just do CMOS and fifo reset
	dataValue = 0xeUL;
	xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,dataValue,&result,crate);

	// must clear this
	dataValue = 0x0UL;
	xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,dataValue,&result,crate);

	// enable the appropriate pedestals
	xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,activeChannels,&result,crate);


	// pulse pulser NumPulses times.
	if (DEBUG) {
		sprintf(psb, "Firing ~ %08x pedestals.\n", NumPulses);
		print_send(psb, view_fdset);
	}
	multi_softgt(NumPulses);

	// now collect data.
	// first, delay CPU so sequencer can process data
	usleep(5000);

	// first check to assure that we have enough data in memory 
	// -- read out fifo registers.
	xl3_rw(FIFO_DIFF_PTR_R + select_reg + READ_REG,0x0,&dataValue,crate);

	// mask out top 12 bits of dataValue here 
	// -- difference pointer is only 20 bits.
	dataValue &= 0x000FFFFF;

	// if there are less than (NumPulses * 32) 3 word records in the memory
	if (dataValue <= 32UL * 3UL * NumPulses) {
		// then just read out what's there minus a fudge factor
		WordsInMem = dataValue > 100UL ? dataValue - 100UL : dataValue;
	}
	else {
		WordsInMem = 32UL * 3L * NumPulses; // else read out 500 hits on 32 chs.
	}

	if (WordsInMem < Min_num_words) {
		sprintf(psb, "Less than %08x bundles in memory (there are %08x).  Aborting.\n", 
				Min_num_words,WordsInMem);
		print_send(psb, view_fdset);
		return 10;
	}

	// now we are reading out the memory
	if (DEBUG) {
		print_send("Reading out FEC32 memory.\n", view_fdset);
	}


	// New way
#ifdef FIRST_WORD_BUG
	if ((WordsInMem > 3) )
	{
		print_send("This is a hack until the sequencer is fixed\n", view_fdset);
		xl3_rw(select_reg + READ_MEM,0x0,pmt_buf,crate);
		xl3_rw(select_reg + READ_MEM,0x0,pmt_buf,crate);
		xl3_rw(select_reg + READ_MEM,0x0,pmt_buf,crate);
	} // throw out the first word of data because it is currently garbage
#endif //FIRST_WORD_BUG

	// lets use the new function!
	// first we attempt to load up the xl3 with 'diff' # of reads to memory
	packet.cmdHeader.packet_type = READ_PEDESTALS_ID;
	int slot;
	for (i=0;i<16;i++)
		if (FEC_SEL*i == select_reg)
			slot = i;

	*(uint32_t *) packet.payload = slot;
	int reads_left = WordsInMem;
	int this_read;
	while(reads_left != 0){
		if (reads_left > MAX_FEC_COMMANDS-1000)
			this_read = MAX_FEC_COMMANDS-1000;
		else
			this_read = reads_left;

		packet.cmdHeader.packet_type = READ_PEDESTALS_ID;
		*(uint32_t *) packet.payload = slot;
		*(uint32_t *) (packet.payload+4) = this_read;;
		SwapLongBlock(packet.payload,2);
		do_xl3_cmd(&packet,crate);
		receive_data(this_read,command_number-1,crate,pmt_buf+(WordsInMem-reads_left));
		reads_left -= this_read;
	}

	for (i=0;i<WordsInMem;i+=3){
		PMTdata = MakeBundleFromData(pmt_buf+i);
		pedestals[PMTdata.ChannelID].thiscell[PMTdata.CMOSCellID].qlxbar += PMTdata.ADC_Qlx;
		pedestals[PMTdata.ChannelID].thiscell[PMTdata.CMOSCellID].qhlbar += PMTdata.ADC_Qhl;
		pedestals[PMTdata.ChannelID].thiscell[PMTdata.CMOSCellID].qhsbar += PMTdata.ADC_Qhs;
		pedestals[PMTdata.ChannelID].thiscell[PMTdata.CMOSCellID].tacbar += PMTdata.ADC_TAC;

		// to calculate RMS in one go-around, use the xxx_rms^2 = (N/N-1) [ <x^2> - <x>^2] 
		// method.
		// calculate <xxx^2>*N in the step below.
		pedestals[PMTdata.ChannelID].thiscell[PMTdata.CMOSCellID].qlxrms +=
			pow(PMTdata.ADC_Qlx, 2);
		pedestals[PMTdata.ChannelID].thiscell[PMTdata.CMOSCellID].qhlrms +=
			pow(PMTdata.ADC_Qhl, 2);
		pedestals[PMTdata.ChannelID].thiscell[PMTdata.CMOSCellID].qhsrms +=
			pow(PMTdata.ADC_Qhs, 2);
		pedestals[PMTdata.ChannelID].thiscell[PMTdata.CMOSCellID].tacrms +=
			pow(PMTdata.ADC_TAC, 2);

		pedestals[PMTdata.ChannelID].thiscell[PMTdata.CMOSCellID].per_cell++;
		pedestals[PMTdata.ChannelID].per_channel++;
	}






	// final step of calculation
	for (i = 0; i <= 31; i++) {

		if (pedestals[i].per_channel > 0) {

			// ok, so there is data on this channel, so let's turn on all the test bits right now.
			// we'll have to turn them off whenever there is a failed test.
			ChParams[i].test_status |= PED_TEST_TAKEN | PED_CH_HAS_PEDESTALS |
				PED_RMS_TEST_PASSED | PED_PED_WITHIN_RANGE;
			ChParams[i].test_status &= ~(PED_DATA_NO_ENABLE | PED_TOO_FEW_PER_CELL);
			for (j = 0; j <= 15; j++) {
				num_events = pedestals[i].thiscell[j].per_cell;		// number of events in this cell
				// we know how many events per cell we expect.

				if (((x = num_events * 16.0 / NumPulses) > 1.1) || (x < 0.9)) {

					ChParams[i].test_status |= PED_TOO_FEW_PER_CELL;

					//continue; // I need to break out of two levels here, how can I do that????
				}

				if (num_events > 1) {	// don't do anything if there is no data here or n=1 since 
					// that gives divide by zero below.

					// now x_avg = sum(x) / N -- so now xxx_bar is calculated
					pedestals[i].thiscell[j].qlxbar =
						pedestals[i].thiscell[j].qlxbar / num_events;
					pedestals[i].thiscell[j].qhlbar =
						pedestals[i].thiscell[j].qhlbar / num_events;
					pedestals[i].thiscell[j].qhsbar =
						pedestals[i].thiscell[j].qhsbar / num_events;
					pedestals[i].thiscell[j].tacbar =
						pedestals[i].thiscell[j].tacbar / num_events;

					// now x_rms^2 = n/(n-1) * ( <xxx^2>*N/N - xxx_bar^2)
					pedestals[i].thiscell[j].qlxrms = num_events / (num_events - 1)
						* (pedestals[i].thiscell[j].qlxrms / num_events
								- pow(pedestals[i].thiscell[j].qlxbar, 2));
					pedestals[i].thiscell[j].qhlrms = num_events / (num_events - 1)
						* (pedestals[i].thiscell[j].qhlrms / num_events
								- pow(pedestals[i].thiscell[j].qhlbar, 2));
					pedestals[i].thiscell[j].qhsrms = num_events / (num_events - 1)
						* (pedestals[i].thiscell[j].qhsrms / num_events
								- pow(pedestals[i].thiscell[j].qhsbar, 2));
					pedestals[i].thiscell[j].tacrms = num_events / (num_events - 1)
						* (pedestals[i].thiscell[j].tacrms / num_events
								- pow(pedestals[i].thiscell[j].tacbar, 2));

					// finally x_rms = sqrt (x_rms^2)
					pedestals[i].thiscell[j].qlxrms =
						sqrt(pedestals[i].thiscell[j].qlxrms);
					pedestals[i].thiscell[j].qhlrms =
						sqrt(pedestals[i].thiscell[j].qhlrms);
					pedestals[i].thiscell[j].qhsrms =
						sqrt(pedestals[i].thiscell[j].qhsrms);
					pedestals[i].thiscell[j].tacrms =
						sqrt(pedestals[i].thiscell[j].tacrms);

					// now do some error checking
					if ((pedestals[i].thiscell[j].qlxrms > Max_RMS_Qlx)
							|| (pedestals[i].thiscell[j].qhlrms > Max_RMS_Qhl)
							|| (pedestals[i].thiscell[j].qhsrms > Max_RMS_Qhs)
							|| (pedestals[i].thiscell[j].tacrms > Max_RMS_TAC))
						ChParams[i].test_status &= ~PED_RMS_TEST_PASSED;
					// turn off the RMS test passed bit

				}
				else {
					// turn off "channel has  pedestals" passed bit
					ChParams[i].test_status &= ~PED_CH_HAS_PEDESTALS;

				}
			}
		}
	}
	return 0;			// successful return. 
	//-- what does successsfull return mean here? hmm.

}
// end getPedestal


FEC32PMTBundle MakeBundleFromData(uint32_t *buffer)
{
	short i;
	unsigned short triggerWord2;

	unsigned short ValADC0, ValADC1, ValADC2, ValADC3;
	unsigned short signbit0, signbit1, signbit2, signbit3;
	FEC32PMTBundle GetBundle;

	/*initialize PMTBundle to all zeros */
	// display the lower and the upper bits separately
	GetBundle.GlobalTriggerID = 0;
	GetBundle.GlobalTriggerID2 = 0;
	GetBundle.ChannelID = 0;
	GetBundle.CrateID = 0;
	GetBundle.BoardID = 0;
	GetBundle.CMOSCellID = 0;
	GetBundle.ADC_Qlx = 0;
	GetBundle.ADC_Qhs = 0;
	GetBundle.ADC_Qhl = 0;
	GetBundle.ADC_TAC = 0;
	GetBundle.CGT_ES16 = 0;
	GetBundle.CGT_ES24 = 0;
	GetBundle.Missed_Count = 0;
	GetBundle.NC_CC = 0;
	GetBundle.LGI_Select = 0;
	GetBundle.CMOS_ES16 = 0;

	GetBundle.BoardID = sGetBits(*buffer, 29, 4);		// FEC32 card ID

	GetBundle.CrateID = sGetBits(*buffer, 25, 5);		// Crate ID

	GetBundle.ChannelID = sGetBits(*buffer, 20, 5);	// CMOS Channel ID


	// lower 16 bits of global trigger ID
	//triggerWord = sGetBits(TempVal,15,16);  

	// lower 16 bits of the                       
	GetBundle.GlobalTriggerID = sGetBits(*buffer, 15, 16);
	// global trigger ID
	GetBundle.CGT_ES16 = sGetBits(*buffer, 30, 1);
	GetBundle.CGT_ES24 = sGetBits(*buffer, 31, 1);

	// now get ADC output and peel off the corresponding values and
	// convert to decimal
	// ADC0= Q_low gain,  long integrate (Qlx)
	// ADC1= Q_high gain, short integrate time (Qhs)
	// ADC2= Q_high gain, long integrate time  (Qhl)
	// ADC3= TAC

	GetBundle.CMOSCellID = sGetBits(*(buffer+1), 15, 4);	// CMOS Cell number

	signbit0 = sGetBits(*(buffer+1), 11, 1);
	signbit1 = sGetBits(*(buffer+1), 27, 1);
	ValADC0 = sGetBits(*(buffer+1), 10, 11);
	ValADC1 = sGetBits(*(buffer+1), 26, 11);

	// ADC values are in 2s complement code 
	if (signbit0 == 1)
		ValADC0 = ValADC0 - 2048;
	if (signbit1 == 1)
		ValADC1 = ValADC1 - 2048;

	//Add 2048 offset to ADC0-1 so range is from 0 to 4095 and unsigned
	GetBundle.ADC_Qlx = (unsigned short) (ValADC0 + 2048);
	GetBundle.ADC_Qhs = (unsigned short) (ValADC1 + 2048);

	GetBundle.Missed_Count = sGetBits(*(buffer+1), 28, 1);;
	GetBundle.NC_CC = sGetBits(*(buffer+1), 29, 1);;
	GetBundle.LGI_Select = sGetBits(*(buffer+1), 30, 1);;
	GetBundle.CMOS_ES16 = sGetBits(*(buffer+1), 31, 1);;

	signbit2 = sGetBits(*(buffer+2), 11, 1);
	signbit3 = sGetBits(*(buffer+2), 27, 1);
	ValADC2 = sGetBits(*(buffer+2), 10, 11);
	ValADC3 = sGetBits(*(buffer+2), 26, 11);

	// --------------- the full concatanated global trigger ID --------------
	//            for (i = 4; i >= 1 ; i--){
	//                    if ( sGetBits(TempVal,(15 - i + 1),1) )
	//                            triggerWord |= ( 1UL << (19 - i + 1) );
	//            }
	//            for (i = 4; i >= 1 ; i--){
	//                    if ( sGetBits(TempVal,(31 - i + 1),1) )
	//                            triggerWord |= ( 1UL << (23 - i + 1) );
	//            }
	// --------------- the full concatanated global trigger ID --------------

	triggerWord2 = sGetBits(*(buffer+2), 15, 4);	// Global Trigger bits 19-16

	for (i = 4; i >= 1; i--) {
		if (sGetBits(*(buffer+2), (31 - i + 1), 1))
			triggerWord2 |= (1UL << (7 - i + 1));
	}

	// upper 8 bits of Global Trigger ID 5/27/96
	GetBundle.GlobalTriggerID2 = triggerWord2;

	// ADC values are in 2s complement code 
	if (signbit2 == 1)
		ValADC2 = ValADC2 - 2048;
	if (signbit3 == 1)
		ValADC3 = ValADC3 - 2048;

	// Add 2048 offset to ADC2-3 so range is from 0 to 4095 and unsigned
	GetBundle.ADC_Qhl = (unsigned short) (ValADC2 + 2048);
	GetBundle.ADC_TAC = (unsigned short) (ValADC3 + 2048);

	//calculate number of bus errors
	//FIXME

	return GetBundle;
}


// PMT Bundle bit strip function. The data is passed 
// in the FEC32PMTBundle structure
FEC32PMTBundle GetPMTBundle(int crate, uint32_t select_reg)
{
	short i;
	unsigned short triggerWord2;

	unsigned short ValADC0, ValADC1, ValADC2, ValADC3;
	unsigned short signbit0, signbit1, signbit2, signbit3;
	uint32_t dval, TempVal;
	unsigned long err1, err2, err3;
	u_long *memory;
	FEC32PMTBundle GetBundle;

	/*initialize PMTBundle to all zeros */
	// display the lower and the upper bits separately
	GetBundle.GlobalTriggerID = 0;
	GetBundle.GlobalTriggerID2 = 0;
	GetBundle.ChannelID = 0;
	GetBundle.CrateID = 0;
	GetBundle.BoardID = 0;
	GetBundle.CMOSCellID = 0;
	GetBundle.ADC_Qlx = 0;
	GetBundle.ADC_Qhs = 0;
	GetBundle.ADC_Qhl = 0;
	GetBundle.ADC_TAC = 0;
	GetBundle.CGT_ES16 = 0;
	GetBundle.CGT_ES24 = 0;
	GetBundle.Missed_Count = 0;
	GetBundle.NC_CC = 0;
	GetBundle.LGI_Select = 0;
	GetBundle.CMOS_ES16 = 0;

	xl3_rw(select_reg + READ_MEM,0x0,&dval,crate);		//read first data word
	err1 = dval;  

	TempVal = dval;
	GetBundle.BoardID = sGetBits(TempVal, 29, 4);		// FEC32 card ID

	GetBundle.CrateID = sGetBits(TempVal, 25, 5);		// Crate ID

	GetBundle.ChannelID = sGetBits(TempVal, 20, 5);	// CMOS Channel ID

	// lower 16 bits of global trigger ID
	//triggerWord = sGetBits(TempVal,15,16);  

	// lower 16 bits of the                       
	GetBundle.GlobalTriggerID = sGetBits(TempVal, 15, 16);
	// global trigger ID
	GetBundle.CGT_ES16 = sGetBits(TempVal, 30, 1);
	GetBundle.CGT_ES24 = sGetBits(TempVal, 31, 1);

	// now get ADC output and peel off the corresponding values and
	// convert to decimal
	// ADC0= Q_low gain,  long integrate (Qlx)
	// ADC1= Q_high gain, short integrate time (Qhs)
	// ADC2= Q_high gain, long integrate time  (Qhl)
	// ADC3= TAC

	//read second data word
	xl3_rw(select_reg + READ_MEM,0x0,&dval,crate);
	err2 = dval;

	TempVal = dval;
	GetBundle.CMOSCellID = sGetBits(TempVal, 15, 4);	// CMOS Cell number

	signbit0 = sGetBits(TempVal, 11, 1);
	signbit1 = sGetBits(TempVal, 27, 1);
	ValADC0 = sGetBits(TempVal, 10, 11);
	ValADC1 = sGetBits(TempVal, 26, 11);

	// ADC values are in 2s complement code 
	if (signbit0 == 1)
		ValADC0 = ValADC0 - 2048;
	if (signbit1 == 1)
		ValADC1 = ValADC1 - 2048;

	//Add 2048 offset to ADC0-1 so range is from 0 to 4095 and unsigned
	GetBundle.ADC_Qlx = (unsigned short) (ValADC0 + 2048);
	GetBundle.ADC_Qhs = (unsigned short) (ValADC1 + 2048);

	GetBundle.Missed_Count = sGetBits(TempVal, 28, 1);;
	GetBundle.NC_CC = sGetBits(TempVal, 29, 1);;
	GetBundle.LGI_Select = sGetBits(TempVal, 30, 1);;
	GetBundle.CMOS_ES16 = sGetBits(TempVal, 31, 1);;

	//read third data word
	xl3_rw(select_reg + READ_MEM,0x0,&dval,crate);
	err3 = dval;

	TempVal = dval;
	signbit2 = sGetBits(TempVal, 11, 1);
	signbit3 = sGetBits(TempVal, 27, 1);
	ValADC2 = sGetBits(TempVal, 10, 11);
	ValADC3 = sGetBits(TempVal, 26, 11);

	// --------------- the full concatanated global trigger ID --------------
	//            for (i = 4; i >= 1 ; i--){
	//                    if ( sGetBits(TempVal,(15 - i + 1),1) )
	//                            triggerWord |= ( 1UL << (19 - i + 1) );
	//            }
	//            for (i = 4; i >= 1 ; i--){
	//                    if ( sGetBits(TempVal,(31 - i + 1),1) )
	//                            triggerWord |= ( 1UL << (23 - i + 1) );
	//            }
	// --------------- the full concatanated global trigger ID --------------

	triggerWord2 = sGetBits(TempVal, 15, 4);	// Global Trigger bits 19-16

	for (i = 4; i >= 1; i--) {
		if (sGetBits(TempVal, (31 - i + 1), 1))
			triggerWord2 |= (1UL << (7 - i + 1));
	}

	// upper 8 bits of Global Trigger ID 5/27/96
	GetBundle.GlobalTriggerID2 = triggerWord2;

	// ADC values are in 2s complement code 
	if (signbit2 == 1)
		ValADC2 = ValADC2 - 2048;
	if (signbit3 == 1)
		ValADC3 = ValADC3 - 2048;

	// Add 2048 offset to ADC2-3 so range is from 0 to 4095 and unsigned
	GetBundle.ADC_Qhl = (unsigned short) (ValADC2 + 2048);
	GetBundle.ADC_TAC = (unsigned short) (ValADC3 + 2048);

	//calculate number of bus errors
	//FIXME

	return GetBundle;
}

// Take the input value, which is a hex number, and returns num_bits 
// to the right of the bit bit_start, where bit_start=0 is the rightmost bit.
// >>NUM_BITS must be less than or equal to 16<< 
unsigned int sGetBits(unsigned long value, unsigned int bit_start,
		unsigned int num_bits)
{
	unsigned int bits;
	bits = (value >> (bit_start + 1 - num_bits)) & ~(~0 << num_bits);
	return bits;
}


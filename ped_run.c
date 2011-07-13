#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "include/Record_Info.h"
#include "include/xl_regs.h"

#include "penn_daq.h"
#include "crate_cbal.h"
#include "fec_util.h"
#include "mtc_util.h"
#include "net_util.h"
//#include "pouch.h"
//#include "json.h"


#define PED_WIDTH	    25
#define GT_DELAY	    150
#define GT_FINE_DELAY	    0
#define NUM_CELLS	    16
#define TWOTWENTY	    0x100000
#define SLOT_MASK_DEFAULT   0xFFFF


void usage();

int ped_run(char *buffer)
{
    if (sbc_is_connected == 0){
	sprintf(psb,"SBC not connected.\n");
	print_send(psb, view_fdset);
	return -1;
    }
    int i,j,k,errors=0,count,ch,cell,crateID,num_events,slot_iter;
    uint32_t *pmt_buffer, *pmt_iter;
    int error_flag[32];

    int32_t num_pedestals = 50;
    int ped_low = 400;
    int ped_high = 700;
    u_long pattern = 0xFFFFFFFF;
    float frequency = 1000.0, wtime;
    uint32_t gtdelay = GT_DELAY;
    uint16_t ped_width = PED_WIDTH;
    time_t now;
    FILE *file = stdout; // default file descriptor

    struct pedestal *ped;

    uint32_t select_reg;
    uint32_t slot_mask = 0x2000;
    int update_db = 0;
    int final_test = 0;
    int balanced = 0;
    char ft_ids[16][50];
    int crate = 2;

    char *words,*words2;
    ;

    // lets get the parameters from command line
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c'){
		words2 = strtok(NULL, " ");
		crate = atoi(words2);
	    }else if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		slot_mask = strtoul(words2,(char**)NULL,16);
	    }else if (words[1] == 'f'){
		words2 = strtok(NULL, " ");
		frequency =  atof(words2);
	    }else if (words[1] == 'n'){
		words2 = strtok(NULL, " ");
		num_pedestals = atoi(words2); // num pedestals per cell
	    }else if (words[1] == 'p'){
		words2 = strtok(NULL, " ");
		pattern = strtoul(words2, (char **) NULL, 16);
	    }else if (words[1] == 't'){
		words2 = strtok(NULL, " ");
		gtdelay = atoi(words2);
	    }else if (words[1] == 'w'){
		words2 = strtok(NULL, " ");
		ped_width = atoi(words2);
	    }else if (words[1] == 'l'){
		words2 = strtok(NULL, " ");
		ped_low = atoi(words2);
	    }else if (words[1] == 'u'){
		words2 = strtok(NULL, " ");
		ped_high = atoi(words2);
	    }else if (words[1] == 'd'){
		update_db = 1;
	    }else if (words[1] == 'b'){
		balanced = 1;
	    }else if (words[1] == '#'){
		final_test = 1;
		for (i=0;i<16;i++){
		    if ((0x1<<i) & slot_mask){
			words2 = strtok(NULL, " ");
			sprintf(ft_ids[i],"%s",words2);
		    }
		}
	    }else if (words[1] == 'o'){
		words2 = strtok(NULL, " ");
		file = fopen(words2,"w");
		if (file == (FILE *) NULL){
		    sprintf(psb, "Unable to open file for output: %s\n",words2);
		    print_send(psb, view_fdset);
		    //perror("fopen");
		    sprintf(psb,"Dumping to screen as backup.\n");
		    print_send(psb, view_fdset);
		    file = stdout;
		}
	    }else if (words[1] == 'h'){
		usage();
		return -1;
	    }
	}
	words = strtok(NULL, " ");
    }
    (void) time(&now);
    if(file == stdout){
	sprintf(psb,"Pedestal Run Setup\n");
	sprintf(psb+strlen(psb),"-------------------------------------------\n");
	sprintf(psb+strlen(psb),"Crate:		    %2d\n",crate);
	sprintf(psb+strlen(psb),"Slot Mask:		    0x%4hx\n",slot_mask);
	sprintf(psb+strlen(psb),"Pedestal Mask:	    0x%08lx\n",pattern);
	sprintf(psb+strlen(psb),"GT delay (ns):	    %3hu\n", gtdelay);
	sprintf(psb+strlen(psb),"Pedestal Width (ns):    %2d\n",ped_width);
	sprintf(psb+strlen(psb),"Pulser Frequency (Hz):  %3.0f\n",frequency);
	sprintf(psb+strlen(psb),"Lower/Upper pedestal check range: %d %d\n",ped_low,ped_high);
	//sprintf(psb+strlen(psb),"\nRun started at %.24s\n",ctime(&now));
	print_send(psb, view_fdset);
    }
    else
    {
	fprintf(file,"Pedestal Run Setup\n");
	fprintf(file,"-------------------------------------------\n");
	fprintf(file,"Crate:		    %2d\n",crate);
	fprintf(file,"Slot Mask:		    0x%4hx\n",slot_mask);
	fprintf(file,"Pedestal Mask:	    0x%08lx\n",pattern);
	fprintf(file,"GT delay (ns):	    %3hu\n", gtdelay);
	fprintf(file,"Pedestal Width (ns):    %2d\n",ped_width);
	fprintf(file,"Pulser Frequency (Hz):  %3.0f\n",frequency);
	fprintf(file,"Lower/Upper pedestal check range: %d %d\n",ped_low,ped_high);
	//fprintf(file,"\nRun started at %.24s\n",ctime(&now));
    }
    // wtime to run at chosen frequency for chosen num pedestals
    wtime = (float) (num_pedestals-1) * NUM_CELLS / frequency; //FIXME hack for mtc latency
    pmt_buffer = (uint32_t *) malloc( TWOTWENTY*sizeof(u_long));
    ped = (struct pedestal *) malloc( 32 * sizeof(struct pedestal));

    uint32_t result;

    for (slot_iter = 0; slot_iter < 16; slot_iter ++){
	if ((0x1 << slot_iter) & slot_mask){
	    select_reg = FEC_SEL*slot_iter;
	    xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0xFFFFFFFF,&result,crate);
	    xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x2,&result,crate);
	    xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0,&result,crate);
	    xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,~pattern,&result,crate);
	}
    }
    deselect_fecs(crate); 

    print_send("Reset FECs\n", view_fdset);

    errors = 0;
    errors += fec_load_crateadd(crate, slot_mask);
    errors += set_crate_pedestals(crate, slot_mask, pattern);
    deselect_fecs(crate);
    errors = setup_pedestals(frequency,ped_width,gtdelay,GT_FINE_DELAY);
    if (errors){
	print_send("Error setting up MTC for pedestals. Exiting\n", view_fdset);
	unset_ped_crate_mask(MASKALL);
	unset_gt_crate_mask(MASKALL);
	free(pmt_buffer);
	free(ped);
	return -1;
    }

    //enable GT/PED only for selected crate
    unset_ped_crate_mask(MASKALL);
    unset_gt_crate_mask(MASKALL);
    //set_ped_crate_mask(0x1<<crate);
    //set_gt_crate_mask(0x1<<crate);
    set_ped_crate_mask(MASKALL);
    set_gt_crate_mask(MASKALL);
    set_ped_crate_mask(MSK_TUB);
    set_gt_crate_mask(MSK_TUB);

    //wait for pedestals to arrive
    wtime = (wtime * 1E6); // set this to usec
    usleep((u_int) wtime);

    disable_pulser();
    // LOOP OVER SLOTS
    for (slot_iter = 0; slot_iter < 16; slot_iter ++){
	if ((0x1<<slot_iter) & slot_mask){

	    // initialize pedestal struct
	    for (i=0;i<32;i++){
		//pedestal struct
		ped[i].channelnumber = i; //channel numbers start at 0!!!
		ped[i].per_channel = 0;

		for (j=0;j<16;j++){
		    ped[i].thiscell[j].cellno = j;
		    ped[i].thiscell[j].per_cell = 0;
		    ped[i].thiscell[j].qlxbar = 0;
		    ped[i].thiscell[j].qlxrms = 0;
		    ped[i].thiscell[j].qhlbar = 0;
		    ped[i].thiscell[j].qhlrms = 0;
		    ped[i].thiscell[j].qhsbar = 0;
		    ped[i].thiscell[j].qhsrms = 0;
		    ped[i].thiscell[j].tacbar = 0;
		    ped[i].thiscell[j].tacrms = 0;
		}
	    }


	    /////////////////////
	    // READOUT BUNDLES //
	    /////////////////////

	    count = read_pmt(crate, slot_iter, num_pedestals*32*16,pmt_buffer);

	    //check for readout errors
	    if (count <= 0){
		print_send("there was an error in the count!\n", view_fdset);
		sprintf(psb, "Errors reading out MB(%2d) (errno %i)\n",slot_iter,count);
		print_send(psb, view_fdset);
		errors+=1;
		continue;
	    }else{
		sprintf(psb, "MB(%2d): %5d bundles read out.\n",slot_iter,count);
		print_send(psb, view_fdset);
	    }

	    if (count < num_pedestals*32*16)
		errors += 1;

	    //process data
	    pmt_iter = pmt_buffer;

	    for (i=0;i<count;i++){
		crateID = (int) UNPK_CRATE_ID(pmt_iter);
		if (crateID != crate){
		    sprintf(psb, "Invalid crate ID seen! (crate ID %2d, bundle %2i)\n",crateID,i);
		    print_send(psb, view_fdset);
		    pmt_iter+=3;
		    continue;
		}
		ch = (int) UNPK_CHANNEL_ID(pmt_iter);
		cell = (int) UNPK_CELL_ID(pmt_iter);
		ped[ch].thiscell[cell].qlxbar += (double) MY_UNPK_QLX(pmt_iter);
		ped[ch].thiscell[cell].qhsbar += (double) UNPK_QHS(pmt_iter);
		ped[ch].thiscell[cell].qhlbar += (double) UNPK_QHL(pmt_iter);
		ped[ch].thiscell[cell].tacbar += (double) UNPK_TAC(pmt_iter);

		ped[ch].thiscell[cell].qlxrms += pow((double) MY_UNPK_QLX(pmt_iter),2.0);
		ped[ch].thiscell[cell].qhsrms += pow((double) UNPK_QHS(pmt_iter),2.0);
		ped[ch].thiscell[cell].qhlrms += pow((double) UNPK_QHL(pmt_iter),2.0);
		ped[ch].thiscell[cell].tacrms += pow((double) UNPK_TAC(pmt_iter),2.0);

		ped[ch].per_channel++;
		ped[ch].thiscell[cell].per_cell++;

		pmt_iter += 3; //increment pointer
	    }

	    // do final step
	    // final step of calculation
	    for (i=0;i<32;i++){
		if (ped[i].per_channel > 0){
		    for (j=0;j<16;j++){
			num_events = ped[i].thiscell[j].per_cell;

			//don't do anything if there is no data here or n=1 since
			//that gives 1/0 below.
			if (num_events > 1){

			    // now x_avg = sum(x) / N, so now xxx_bar is calculated
			    ped[i].thiscell[j].qlxbar /= num_events;
			    ped[i].thiscell[j].qhsbar /= num_events;
			    ped[i].thiscell[j].qhlbar /= num_events;
			    ped[i].thiscell[j].tacbar /= num_events;

			    // now x_rms^2 = n/(n-1) * (<xxx^2>*N/N - xxx_bar^2)
			    ped[i].thiscell[j].qlxrms = num_events / (num_events -1)
				* ( ped[i].thiscell[j].qlxrms / num_events
					- pow( ped[i].thiscell[j].qlxbar, 2.0));
			    ped[i].thiscell[j].qhlrms = num_events / (num_events -1)
				* ( ped[i].thiscell[j].qhlrms / num_events
					- pow( ped[i].thiscell[j].qhlbar, 2.0));
			    ped[i].thiscell[j].qhsrms = num_events / (num_events -1)
				* ( ped[i].thiscell[j].qhsrms / num_events
					- pow( ped[i].thiscell[j].qhsbar, 2.0));
			    ped[i].thiscell[j].tacrms = num_events / (num_events -1)
				* ( ped[i].thiscell[j].tacrms / num_events
					- pow( ped[i].thiscell[j].tacbar, 2.0));

			    // finally x_rms = sqrt(x_rms^2)
			    ped[i].thiscell[j].qlxrms = sqrt(ped[i].thiscell[j].qlxrms);
			    ped[i].thiscell[j].qhsrms = sqrt(ped[i].thiscell[j].qhsrms);
			    ped[i].thiscell[j].qhlrms = sqrt(ped[i].thiscell[j].qhlrms);
			    ped[i].thiscell[j].tacrms = sqrt(ped[i].thiscell[j].tacrms);
			}
		    }
		}
	    }

	    ///////////////////
	    // PRINT RESULTS //
	    ///////////////////

	    if(file != stdout){
		fprintf(file,"########################################################\n");
		fprintf(file,"Slot (%2d)\n", slot_iter);
		fprintf(file,"########################################################\n");
	    }
	    else{
		sprintf(psb,"########################################################\n");
		sprintf(psb+strlen(psb),"Slot (%2d)\n", slot_iter);
		sprintf(psb+strlen(psb),"########################################################\n");
		print_send(psb, view_fdset);
	    }
	    for (i = 0; i<32; i++){
		error_flag[i] = 0;
		fprintf(file,"Ch Cell  #   Qhl         Qhs         Qlx         TAC\n");
		for (j=0;j<16;j++){
		    if(file != stdout){
			fprintf(file,"%2d %3d %4d %6.1f %4.1f %6.1f %4.1f %6.1f %4.1f %6.1f %4.1f\n",
				i,j,ped[i].thiscell[j].per_cell,
				ped[i].thiscell[j].qhlbar, ped[i].thiscell[j].qhlrms,
				ped[i].thiscell[j].qhsbar, ped[i].thiscell[j].qhsrms,
				ped[i].thiscell[j].qlxbar, ped[i].thiscell[j].qlxrms,
				ped[i].thiscell[j].tacbar, ped[i].thiscell[j].tacrms);
		    }
		    else{
			sprintf(psb,"%2d %3d %4d %6.1f %4.1f %6.1f %4.1f %6.1f %4.1f %6.1f %4.1f\n",
				i,j,ped[i].thiscell[j].per_cell,
				ped[i].thiscell[j].qhlbar, ped[i].thiscell[j].qhlrms,
				ped[i].thiscell[j].qhsbar, ped[i].thiscell[j].qhsrms,
				ped[i].thiscell[j].qlxbar, ped[i].thiscell[j].qlxrms,
				ped[i].thiscell[j].tacbar, ped[i].thiscell[j].tacrms);
			print_send(psb, view_fdset);
		    }
		    if (ped[i].thiscell[j].per_cell < num_pedestals*.8 || ped[i].thiscell[j].per_cell > num_pedestals*1.2) error_flag[i] = 2;
		    if (ped[i].thiscell[j].qhlbar < ped_low || 
			    ped[i].thiscell[j].qhlbar > ped_high ||
			    ped[i].thiscell[j].qhsbar < ped_low ||
			    ped[i].thiscell[j].qhsbar > ped_high ||
			    ped[i].thiscell[j].qlxbar < ped_low ||
			    ped[i].thiscell[j].qlxbar > ped_high) error_flag[i] = 1;
		    //printf("%d %d %d %d\n",ped[i].thiscell[j].qhlbar,ped[i].thiscell[j].qhsbar,ped[i].thiscell[j].qlxbar,ped[i].thiscell[j].tacbar);
		}
		if (error_flag[i] == 1){
		    if(file != stdout){
			fprintf(file,">>>Bad Q pedestal for this channel\n");
		    }
		    else{
			print_send(">>>Bad Q pedestal for this channel\n",
				view_fdset);
		    }
		}
		if (error_flag[i] == 2){
		    if(file != stdout){
			fprintf(file,">>>Wrong no of pedestals for this channel\n");
		    }
		    else{
			print_send(">>>Wrong no of pedestals for this channel\n",
				view_fdset);
		    }
		}
	    }


	    /////////////////////
	    // UPDATE DATABASE //
	    /////////////////////

	    if (update_db){
		printf("updating the database\n");
		JsonNode *newdoc = json_mkobject();
		JsonNode *num = json_mkarray();
		JsonNode *qhl = json_mkarray();
		JsonNode *qhlrms = json_mkarray();
		JsonNode *qhs = json_mkarray();
		JsonNode *qhsrms = json_mkarray();
		JsonNode *qlx = json_mkarray();
		JsonNode *qlxrms = json_mkarray();
		JsonNode *tac = json_mkarray();
		JsonNode *tacrms = json_mkarray();
		JsonNode *error_node = json_mkarray();
		for (i=0;i<32;i++){
		    JsonNode *numtemp = json_mkarray();
		    JsonNode *qhltemp = json_mkarray();
		    JsonNode *qhlrmstemp = json_mkarray();
		    JsonNode *qhstemp = json_mkarray();
		    JsonNode *qhsrmstemp = json_mkarray();
		    JsonNode *qlxtemp = json_mkarray();
		    JsonNode *qlxrmstemp = json_mkarray();
		    JsonNode *tactemp = json_mkarray();
		    JsonNode *tacrmstemp = json_mkarray();
		    for (j=0;j<16;j++){
			json_append_element(numtemp,json_mknumber(ped[i].thiscell[j].per_cell));
			json_append_element(qhltemp,json_mknumber(ped[i].thiscell[j].qhlbar));	
			json_append_element(qhlrmstemp,json_mknumber(ped[i].thiscell[j].qhlrms));	
			json_append_element(qhstemp,json_mknumber(ped[i].thiscell[j].qhsbar));	
			json_append_element(qhsrmstemp,json_mknumber(ped[i].thiscell[j].qhsrms));	
			json_append_element(qlxtemp,json_mknumber(ped[i].thiscell[j].qlxbar));	
			json_append_element(qlxrmstemp,json_mknumber(ped[i].thiscell[j].qlxrms));	
			json_append_element(tactemp,json_mknumber(ped[i].thiscell[j].tacbar));	
			json_append_element(tacrmstemp,json_mknumber(ped[i].thiscell[j].tacrms));	
		    }
		    json_append_element(num,numtemp);
		    json_append_element(qhl,qhltemp);
		    json_append_element(qhlrms,qhlrmstemp);
		    json_append_element(qhs,qhltemp);
		    json_append_element(qhsrms,qhlrmstemp);
		    json_append_element(qlx,qhltemp);
		    json_append_element(qlxrms,qhlrmstemp);
		    json_append_element(tac,qhltemp);
		    json_append_element(tacrms,qhlrmstemp);
		    if (error_flag[i] == 0){
			json_append_element(error_node,json_mkstring("none"));
		    }else{
			json_append_element(error_node,json_mknumber((double)error_flag[i])); //FIXME
		    }
		}
		json_append_member(newdoc,"type",json_mkstring("ped_run"));
		json_append_member(newdoc,"num",num);
		json_append_member(newdoc,"QHL",qhl);
		json_append_member(newdoc,"QHL_rms",qhlrms);
		json_append_member(newdoc,"QHS",qhs);
		json_append_member(newdoc,"QHS_rms",qhsrms);
		json_append_member(newdoc,"QLX",qlx);
		json_append_member(newdoc,"QLX_rms",qlxrms);
		json_append_member(newdoc,"TAC",tac);
		json_append_member(newdoc,"TAC_rms",tacrms);
		json_append_member(newdoc,"errors",error_node);
		int fail_flag = 0;
		for (j=0;j<32;j++){
		    if (error_flag[j] != 0)
			fail_flag = 1;
		}
		if (fail_flag == 0){
		    json_append_member(newdoc,"pass",json_mkstring("yes"));
		}else{
		    json_append_member(newdoc,"pass",json_mkstring("no"));
		}
		if (balanced){
		    json_append_member(newdoc,"balanced",json_mkstring("yes"));
		}else{
		    json_append_member(newdoc,"balanced",json_mkstring("no"));	
		}
		if (final_test){
		    json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[slot_iter]));	
		}
		post_debug_doc(crate,slot_iter,newdoc);
		json_delete(newdoc); // only need to delete the head node
	    }

	} // end loop over slot mask
    } // end loop over slots
    free(pmt_buffer);
    free(ped);

    //disable trigger enables
    unset_ped_crate_mask(MASKALL);
    unset_gt_crate_mask(MASKALL);

    //unset pedestalenable
    errors += set_crate_pedestals(crate, slot_mask, 0x0);

    deselect_fecs(crate);
    if (errors)
	sprintf(psb, "There were %d errors\n",errors);
    else
	sprintf(psb, "No errors seen\n");
    print_send(psb, view_fdset);
    print_send("*************************************\n",view_fdset);
    return errors;
}

void usage()
{
    sprintf(psb,"Usage: ped_run [-c crate] [-s slot_mask] \n");
    sprintf(psb+strlen(psb),"Where defaults are crate 2, slot_mask 0x2000.\n");
    sprintf(psb+strlen(psb),"Other available flags are (defaults in parents):\n");
    sprintf(psb+strlen(psb),"\t-l # (400) Lower Q pedestal check value\n");
    sprintf(psb+strlen(psb),"\t-u # (700) Upper Q pedestal check value\n");
    sprintf(psb+strlen(psb),"\t-f # (1KHz) pulser frequency\n");
    sprintf(psb+strlen(psb),"\t-n # (50) Number of pedestals per cell\n");
    sprintf(psb+strlen(psb),"\t-t # (150) GT delay\n");
    sprintf(psb+strlen(psb),"\t-w # (25) pedestal width\n");
    sprintf(psb+strlen(psb),"\t-o <name> (stdout) output file name\n");
    sprintf(psb+strlen(psb),"\t-d (update debug db)\n");
    print_send(psb, view_fdset);
    return;
}

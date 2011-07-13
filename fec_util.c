#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "include/Record_Info.h"
#include "include/xl_regs.h"

#include "penn_daq.h"
#include "fec_util.h"
#include "net_util.h"
#include "db.h"
#include "pillowtalk.h"

/*
int get_cmos_total_count(int crate,int slot,uint32_t* total_count)
{
    int errorbit;
    int errors = 0;
    int busstop;
    int i;
    int howmany;
    uint32_t done_mask = 0x0;
    uint32_t results[32];
    XL3_Packet packet;
    MultiFC *commands = (MultiFC *) packet.payload;
    do{
    howmany = 0;
    for (i=0;i<32;i++){
	if (((0x1<<i) & done_mask) == 0x0){
	    commands->cmd[howmany].address = CMOS_INTERN_TOTAL(i) + FEC_SEL*slot + READ_REG;
	    commands->cmd[howmany].data = 0x0;
	    SwapLongBlock(&(commands->cmd[howmany].address),1);
	    SwapLongBlock(&(commands->cmd[howmany].data),1);
	    howmany++;
	}
    }
    commands->howmany = howmany;
    SwapLongBlock(&(commands->howmany),1);
    packet.cmdHeader.packet_type = FEC_CMD_ID;
    do_xl3_cmd(&packet,crate);
    receive_data(howmany,command_number-1,crate,&results);
    howmany = 0;
    for (i=0;i<32;i++){
	if (((0x1<<i) & done_mask) == 0x0){
	    total_count[i] = results[howmany];
	    errorbit = (total_count[i] & 0x80000000) ? 1 : 0;
	    if (errorbit){
		errors++;
		printf("there was a cmos total count error\n");
		if (errors > 320)
		    return -1;
	    }else{
		total_count[i] &= 0x7FFFFFFF;
		done_mask |= (0x1<<i);
	    }
	    howmany++;
	}
    }
    }while(done_mask != 0xFFFFFFFF);
    return 0;
}

*/
int get_cmos_total_count(int crate,int slot,int channel,uint32_t* total_count)
{
    int errorbit;
    int busstop;
    int errors = 0;
    do{
    busstop = xl3_rw(CMOS_INTERN_TOTAL(channel) + FEC_SEL*slot + READ_REG,0x0,total_count,crate);
    errorbit = (*total_count & 0x80000000) ? 1 : 0;
    if (errorbit | busstop){
	errors++;
	if (errors > 10)
	    return -1;
    }
    *total_count &= 0x7FFFFFFF;
    }while(errorbit);
    return 0;
}

int get_cmos_total_count2(int crate, int slot, uint32_t* total_count)
{
    XL3_Packet packet;
    uint32_t *p = (uint32_t *) packet.payload;
    *p = slot;
    SwapLongBlock(p,1);
    packet.cmdHeader.packet_type = CHECK_TOTAL_COUNT_ID;
    do_xl3_cmd(&packet,crate);
    SwapLongBlock((p+1),32);
    int i;
    for (i=0;i<32;i++){
	total_count[i] = *(p+1+i);
    }
    return 0;
}

int get_crate_from_jumpers(uint16_t jumpers)
{
    int i;
    uint16_t jumper_array[] = { 0x1A, 0x19, 0x18, 0x17, 0x16, 0x16, 0x14, 0x13, 0x12, 0x10,
	0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0xFFFF};
    i = 0;
    jumpers = jumpers & 0x1F;
    while((jumper_array[i] != jumpers) && (jumper_array[i] != 0xFFFF))
	i++;
    if (jumper_array[i] == jumpers)
	return i;
    else
	return -1;
}

int zdisc(char *buffer)
{
    char *words,*words2;
    uint32_t crate_num=2, slot_mask=0x2000, offset;
    float rate;
    int i,j,result;
    int update_db = 0;
    int final_test = 0;
    char ft_ids[16][50];

    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c'){
		words2 = strtok(NULL, " ");
		crate_num = atoi(words2);
	    }
	    if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		slot_mask = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'o'){
		words2 = strtok(NULL, " ");
		offset = atoi(words2);
	    }
	    if (words[1] == 'r'){
		words2 = strtok(NULL, " ");
		rate = (float) strtod(words2,(char**)NULL);
	    }
	    if (words[1] == 'd'){
		update_db = 1;
	    }
	    if (words[1] == '#'){
		final_test = 1;
		for (i=0;i<16;i++){
		    if ((0x1<<i) & slot_mask){
			words2 = strtok(NULL, " ");
			sprintf(ft_ids[i],"%s",words2);
		    }
		}
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: zdisc -c"
			" [crate_num] -s [slot mask (hex)] -o [offset] -r [rate] -d (write results to db)\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }

    print_send("-------------------------------------\n\r",view_fdset);
    print_send("Discriminator Zero Finder. \n\r",view_fdset);
    sprintf(psb,"Desired rate:\t% 5.1f\n", rate);
    print_send(psb,view_fdset);
    sprintf(psb,"Offset      :\t%hu\n", offset);
    print_send(psb,view_fdset);
    print_send("-------------------------------------\n\r",view_fdset);

    XL3_Packet packet;
    hware_vals_t hware_vals_found[16];

    float *MaxRate,*UpperRate,*LowerRate;
    uint8_t *MaxDacSetting,*ZeroDacSetting,*UpperDacSetting,*LowerDacSetting;

    pt_init();

    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){

	    ///////////////////////
	    // TELL XL3 TO ZDISC //
	    ///////////////////////

	    uint32_t *p = (uint32_t *) packet.payload;
	    *p = i;
	    *(p+1) = offset;
	    float *q = (float *) (p+2);
	    *q = rate;
	    packet.cmdHeader.packet_type = ZERO_DISCRIMINATOR_ID;
	    SwapLongBlock(p,3);
	    do_xl3_cmd(&packet,crate_num);
	    result = *(uint32_t *) packet.payload;
	    SwapLongBlock((packet.payload+4),96);
	    MaxRate = (float *) (packet.payload+4);
	    UpperRate = (float *) (packet.payload+4+32*sizeof(float));
	    LowerRate = (float *) (packet.payload+4+64*sizeof(float));
	    MaxDacSetting = (uint8_t *) (packet.payload+4+96*sizeof(float));
	    ZeroDacSetting = (uint8_t *) (packet.payload+4+96*sizeof(float)+32*sizeof(uint8_t));
	    UpperDacSetting = (uint8_t *) (packet.payload+4+96*sizeof(float)+64*sizeof(uint8_t));
	    LowerDacSetting = (uint8_t *) (packet.payload+4+96*sizeof(float)+96*sizeof(uint8_t));


	    // printout stuff/////////
	    printf("channel    max rate,       lower,       upper\n\r");
	    printf("------------------------------------------\n\r");
	    for (j=0;j<32;j++)
	    {
		printf("ch (%2d)   %5.2f(MHz)  %6.1f(KHz)  %6.1f(KHz)\n\r",j,(float) MaxRate[j]/1E6,(float) LowerRate[j]/1E3,(float) UpperRate[j]/1E3);
	    }
	    printf("Dac Settings\n\r");
	    printf("channel     Max   Lower   Upper   U+L/2\n\r");
	    for (j=0;j<32;j++)
	    {
		printf("ch (%2i)   %5hu   %5hu   %5hu   %5hu\n", j, MaxDacSetting[j],LowerDacSetting[j],UpperDacSetting[j],ZeroDacSetting[j]);
		if (LowerDacSetting[j] > MaxDacSetting[j])
		{
		    printf(" <- lower > max! (MaxRate(MHz):%5.2f, lowrate(KHz):%5.2f\n",(float) MaxRate[j]/1E6,(float) LowerRate[j]/1000.0);
		}
	    }

	    // update the database
	    if (update_db){
		printf("updating the database\n");
		char hextostr[50];
		pt_node_t *newdoc = pt_map_new();
		pt_map_set(newdoc,"type",pt_string_new("zdisc"));

		pt_node_t *maxratenode = pt_array_new();
		pt_node_t *lowerratenode = pt_array_new();
		pt_node_t *upperratenode = pt_array_new();
		pt_node_t *maxdacnode = pt_array_new();
		pt_node_t *lowerdacnode = pt_array_new();
		pt_node_t *upperdacnode = pt_array_new();
		pt_node_t *zerodacnode = pt_array_new();
		pt_node_t *errorsnode = pt_array_new();
		for (j=0;j<32;j++){
		    pt_array_push_back(maxratenode,pt_double_new(MaxRate[j]));	
		    pt_array_push_back(lowerratenode,pt_double_new(LowerRate[j]));	
		    pt_array_push_back(upperratenode,pt_double_new(UpperRate[j]));	
		    pt_array_push_back(maxdacnode,pt_integer_new(MaxDacSetting[j]));	
		    pt_array_push_back(lowerdacnode,pt_integer_new(LowerDacSetting[j]));	
		    pt_array_push_back(upperdacnode,pt_integer_new(UpperDacSetting[j]));	
		    pt_array_push_back(zerodacnode,pt_integer_new(ZeroDacSetting[j]));	
		    pt_array_push_back(errorsnode,pt_integer_new(0));//FIXME	
		}
		pt_map_set(newdoc,"Max_rate",maxratenode);
		pt_map_set(newdoc,"Lower_rate",lowerratenode);
		pt_map_set(newdoc,"Upper_rate",upperratenode);
		pt_map_set(newdoc,"Max_Dac_setting",maxdacnode);
		pt_map_set(newdoc,"Lower_Dac_setting",lowerdacnode);
		pt_map_set(newdoc,"Upper_Dac_setting",upperdacnode);
		pt_map_set(newdoc,"Zero_Dac_setting",zerodacnode);
		pt_map_set(newdoc,"errors",errorsnode);
		pt_map_set(newdoc,"pass",pt_string_new("yes"));//FIXME
		if (final_test)
		    pt_map_set(newdoc,"final_test_id",pt_string_new(ft_ids[i]));	

		post_debug_doc(crate_num,i,newdoc);
	    }
	} // end loop over slot mask
    } // end loop over slots
    print_send("zero discriminator complete.\n",view_fdset);
    print_send("*******************************\n",view_fdset);
    return 0;
}

int ramp_voltage(char *buffer)
{
    char *words,*words2;
    uint32_t pattern = 0xf;
    int crate = 2;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate = atoi(strtok(NULL, " "));
	    if (words[1] == 'p'){
		words2 = strtok(NULL, " ");
		pattern = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: load_relays -c"
			" [crate_num] -s [slot mask (hex)] -d [update debug db]\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }
    int i,j;
    uint32_t result;
    xl3_rw(XL_HV_SP_R + WRITE_REG, current_hv_level + pattern,&result,crate);
    usleep(1000);
    usleep(1000);
    usleep(1000);
    usleep(1000);
    usleep(1000);
    current_hv_level += pattern;
    char hvreadbackcom[100];
    sprintf(hvreadbackcom,"hv_readback -c %d -a -b",crate);
    hv_readback(hvreadbackcom);

    return 0;
}


int load_relays(char *buffer)
{
    char *words,*words2;
    uint32_t pattern = 0xf;
    int crate = 2;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate = atoi(strtok(NULL, " "));
	    if (words[1] == 'p'){
		words2 = strtok(NULL, " ");
		pattern = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: load_relays -c"
			" [crate_num] -s [slot mask (hex)] -d [update debug db]\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }
    int i,j;
    uint32_t result;
    for (i=0;i<16;i++){
	for (j=0;j<4;j++){
	    if ((0x1<<j) & pattern){
		xl3_rw(XL_RELAY_R + WRITE_REG, 0x2,&result,crate);
		xl3_rw(XL_RELAY_R + WRITE_REG, 0xa,&result,crate);
		xl3_rw(XL_RELAY_R + WRITE_REG, 0x2,&result,crate);
	    }else{
		xl3_rw(XL_RELAY_R + WRITE_REG, 0x0,&result,crate);
		xl3_rw(XL_RELAY_R + WRITE_REG, 0x8,&result,crate);
		xl3_rw(XL_RELAY_R + WRITE_REG, 0x0,&result,crate);
	    }
	}
    }
    usleep(1000);
    xl3_rw(XL_RELAY_R + WRITE_REG, 0x4,&result,crate);

    return 0;
}


int fec_test(char *buffer)
{
    char *words,*words2;
    uint32_t slot_mask = 0x2000;
    int crate_num = 2;
    int update_db = 0;
    int i;
    char ft_ids[16][50];
    int final_test = 0;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate_num = atoi(strtok(NULL, " "));
	    if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		slot_mask = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'd'){
		update_db = 1;
	    }
	    if (words[1] == '#'){
		final_test = 1;
		for (i=0;i<16;i++){
		    if ((0x1<<i) & slot_mask){
			words2 = strtok(NULL, " ");
			sprintf(ft_ids[i],"%s",words2);
		    }
		}
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: fec_test -c"
			" [crate_num] -s [slot mask (hex)] -d [update debug db]\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }

    XL3_Packet packet;
    uint32_t *p = (uint32_t *) packet.payload;
    *p = slot_mask;
    packet.cmdHeader.packet_type = FEC_TEST_ID;

    SwapLongBlock(p,1);

    do_xl3_cmd(&packet,crate_num);

    if (update_db){
	printf("updating the database\n");
	uint32_t* results = (uint32_t*) packet.payload;
	// results layout is : [error flags] [slot 0 discrete] [slot 1 discrete] ... [slot 0 cmos] [slot 1 cmos] ...
	SwapLongBlock(results,65);
	int slot;
	pt_init();
	for (slot=0;slot<16;slot++){
	    if ((0x1<<slot) & slot_mask){
		printf("updating slot %d\n",slot);
		pt_node_t *newdoc = pt_map_new();
		pt_map_set(newdoc,"type",pt_string_new("fec_test"));
		if (results[1+slot] & 0x1)
		    pt_map_set(newdoc,"pedestal",pt_integer_new(1));
		else
		    pt_map_set(newdoc,"pedestal",pt_integer_new(0));
		if (results[1+slot] & 0x2)
		    pt_map_set(newdoc,"chip_disable",pt_integer_new(1));
		else
		    pt_map_set(newdoc,"chip_disable",pt_integer_new(0));
		if (results[1+slot] & 0x4)
		    pt_map_set(newdoc,"lgi_select",pt_integer_new(1));
		else
		    pt_map_set(newdoc,"lgi_select",pt_integer_new(0));
		if (results[1+slot] & 0x8)
		    pt_map_set(newdoc,"cmos_prog_low",pt_integer_new(1));
		else
		    pt_map_set(newdoc,"cmos_prog_low",pt_integer_new(0));
		if (results[1+slot] & 0x10)
		    pt_map_set(newdoc,"cmos_prog_high",pt_integer_new(1));
		else
		    pt_map_set(newdoc,"cmos_prog_high",pt_integer_new(0));
		pt_node_t *cmos_test_array = pt_array_new();
		for (i=0;i<32;i++){
		    if (results[17+slot] & (0x1<<i))
			pt_array_push_back(cmos_test_array,pt_integer_new(1));
		    else
			pt_array_push_back(cmos_test_array,pt_integer_new(0));
		}
		if (results[17+slot] == 0x0)
		    pt_array_push_back(cmos_test_array,pt_integer_new(0));
		else
		    pt_array_push_back(cmos_test_array,pt_integer_new(1));
		pt_map_set(newdoc,"cmos_test_reg",cmos_test_array);
		if ((results[1+slot] == 0x0) && (results[17+slot] == 0x0)){
		    pt_map_set(newdoc,"pass",pt_string_new("yes"));
		}else{
		    pt_map_set(newdoc,"pass",pt_string_new("no"));
		}
		if (final_test)
		    pt_map_set(newdoc,"final_test_id",pt_string_new(ft_ids[slot]));	
		post_debug_doc(crate_num,slot,newdoc);
	    }
	}
    }
    return 0;
}

int board_id(char *buffer)
{
    char *words,*words2;
    uint32_t slot_mask = 0x2000;
    int crate_num = 2;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate_num = atoi(strtok(NULL, " "));
	    if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		slot_mask = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: board_id -c"
			" [crate_num] -s [slot mask (hex)]\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }

    uint32_t mb_id, dc_id[4],hv_id;

    XL3_Packet packet;
    packet.cmdHeader.packet_type = BOARD_ID_READ_ID;
    uint32_t *result = (uint32_t *) packet.payload;
    uint32_t *slot = (uint32_t *) packet.payload;
    uint32_t *chip = (uint32_t *) (packet.payload+4);
    uint32_t *reg = (uint32_t *) (packet.payload+8);
    int i,j,k;
    sprintf(psb, "SLOT ID: MB	    DB1	    DB2	    DB3	    DB4	    HVC\n");
    for (i=0;i<16;i++){
	if (slot_mask & (0x01<<i)){
	    *slot = i;
	    *chip = 1;
	    *reg = 15;

	    SwapLongBlock(slot,3);

	    do_xl3_cmd(&packet,crate_num);

	    SwapLongBlock(slot,1);
	    mb_id = *result;

	    k=0;
	    for (j=2;j<6;j++){
		*slot = i;
		*chip = j;
		*reg = 15;
		SwapLongBlock(slot,3);
		do_xl3_cmd(&packet,crate_num);
		SwapLongBlock(slot,1);
		dc_id[k] = *result;
		k++;
	    }
	    *slot = i;
	    *chip = 6;
	    *reg = 15;
	    SwapLongBlock(slot,3);
	    do_xl3_cmd(&packet,crate_num);
	    SwapLongBlock(slot,1);
	    hv_id = *result;
	    sprintf(psb+strlen(psb), "%d	    0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n",
		    i,mb_id,dc_id[0],dc_id[1],dc_id[2],dc_id[3],hv_id);
	    print_send(psb, view_fdset);
	}
    }
    print_send("*******************************\n",view_fdset);
    deselect_fecs(crate_num);
    return 0;
}

int mem_test(char *buffer)
{
    char *words,*words2;
    uint32_t slot_num = 14;
    int crate_num = 2;
    int update_db = 0;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate_num = atoi(strtok(NULL, " "));
	    if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		slot_num = atoi(words2);
	    }
	    if (words[1] == 'd'){
		update_db = 1;
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: mem_test -c"
			" [crate_num] -s [slot number (int)] -d (update debug database)\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }

    if (slot_num > 15 || slot_num < 0){
	print_send("slot number not valid (its an integer!)\n", view_fdset);
	return -1;
    }

    XL3_Packet packet;
    uint32_t *p = (uint32_t *) packet.payload;
    *p = slot_num;
    packet.cmdHeader.packet_type = MEM_TEST_ID;

    SwapLongBlock(p,1);

    do_xl3_cmd(&packet,crate_num);

    if (update_db){
	printf("updating the database\n");
	uint32_t* results = (uint32_t*) packet.payload;
	// results layout is : [error flags] [address test bit failures] [pattern test first error location] [expected] [read]
	SwapLongBlock(results,65);
	char hextostr[50];
	pt_init();
	pt_node_t *newdoc = pt_map_new();
	pt_map_set(newdoc,"type",pt_string_new("mem_test"));

	sprintf(hextostr,"%05x",results[1]);
	pt_map_set(newdoc,"address_test",pt_string_new(hextostr));
	pt_node_t* patterntest = pt_array_new();
	sprintf(hextostr,"%08x",results[2]);
	pt_array_push_back(patterntest,pt_string_new(hextostr));
	sprintf(hextostr,"%08x",results[3]);
	pt_array_push_back(patterntest,pt_string_new(hextostr));
	sprintf(hextostr,"%08x",results[4]);
	pt_array_push_back(patterntest,pt_string_new(hextostr));
	pt_map_set(newdoc,"pattern_test",patterntest);
	if ((results[1] == 0x0) && (results[2] == 0xFFFFFFFF)){
	    pt_map_set(newdoc,"pass",pt_string_new("yes"));
	}else{
	    pt_map_set(newdoc,"pass",pt_string_new("no"));
	}

	post_debug_doc(crate_num,slot_num,newdoc);
    }
    return 0;
}

int vmon(char *buffer)
{
    char *words,*words2;
    char v_name[21][20] = {"neg_24","neg_15","Vee","neg_3_3","neg_2","pos_3_3","pos_4","Vcc","pos_6_5","pos_8","pos_15","pos_24","neg_2_ref","neg_1_ref","pos_0_8_ref","pos_1_ref","pos_4_ref","pos_5_ref","Temp","CalD","hvt"};
    uint32_t slot_mask = 0x2000;
    float voltages[16][21];
    float voltages_min[21] = {-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99.,-99};
    float voltages_max[21] = {99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99.,99};
    int update_db = 0;
    char ft_ids[16][50];
    int final_test = 0;
    int i,j;
    int crate_num = 2;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate_num = atoi(strtok(NULL, " "));
	    if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		slot_mask = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'd'){
		update_db = 1;
	    }
	    if (words[1] == '#'){
		final_test = 1;
		for (i=0;i<16;i++){
		    if ((0x1<<i) & slot_mask){
			words2 = strtok(NULL, " ");
			sprintf(ft_ids[i],"%s",words2);
		    }
		}
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: vmon -c"
			" [crate_num] -s [slot mask (hex)] -d (udpate debug db)\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }

    // initialize the variables
    for (i=0;i<16;i++)
	for(j=0;j<21;j++)
	    voltages[i][j] = 0;

    // send packets to xl3s to do the reads
    int slot_num;
    for (slot_num = 0;slot_num<16;slot_num++){
	if ((0x1<<slot_num) & slot_mask){
	    XL3_Packet packet;
	    uint32_t *p = (uint32_t *) packet.payload;
	    *p = slot_num;
	    packet.cmdHeader.packet_type = VMON_START_ID;
	    SwapLongBlock(p,1);
	    do_xl3_cmd(&packet,crate_num);

	    float *v = (float *) packet.payload;
	    SwapLongBlock(v,21);
	    for (i=0;i<21;i++){
		voltages[slot_num][i] = *(v+i);
	    }
	}
    }

    // now lets print out the results
    int k;
    for (k=0;k<2;k++){
	printf("slot             %2d     %2d     %2d     %2d     %2d     %2d     %2d     %2d\n",k*8,k*8+1,k*8+2,k*8+3,k*8+4,k*8+5,k*8+6,k*8+7);
	for (i=0;i<21;i++){
	    printf("%10s   ",v_name[i]);
	    for (j=0;j<8;j++){
		printf("%6.2f ",voltages[j+k*8][i]);
	    }
	    printf("\n");
	}
	printf("\n");
    }

    // update the database
    if (update_db){
	printf("updating the database\n");
	char hextostr[50];
	pt_init();
	for (slot_num=0;slot_num<16;slot_num++){
	    if ((0x1<<slot_num) & slot_mask){
		int fail_flag = 0;
		pt_node_t *newdoc = pt_map_new();
		pt_map_set(newdoc,"type",pt_string_new("vmon"));
		pt_node_t *verr = pt_array_new();
		for (j=0;j<21;j++){
		    pt_map_set(newdoc,v_name[j],pt_double_new((double)voltages[slot_num][j]));
		    if ((voltages[slot_num][j] < voltages_min[j]) || (voltages[slot_num][j] > voltages_max[j])){
			pt_array_push_back(verr,pt_integer_new(1));
			fail_flag = 1;
		    }else{
			pt_array_push_back(verr,pt_integer_new(0));
		    }
		}
		if (fail_flag == 0){
		    pt_map_set(newdoc,"pass",pt_string_new("yes"));
		}else{
		    pt_map_set(newdoc,"pass",pt_string_new("no"));
		}
		pt_map_set(newdoc,"errors",verr);
		if (final_test)
		    pt_map_set(newdoc,"final_test_id",pt_string_new(ft_ids[slot_num]));	
		post_debug_doc(crate_num,slot_num,newdoc);
	    }
	}
    }
    return 0;
}

int fec_load_crateadd(int crate, uint32_t slot_mask)
{
    int i;
    int errors;
    uint32_t csr_orig;
    uint32_t select_reg;
    uint32_t result;

    errors = 0;

    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    select_reg =FEC_SEL*i;
	    xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG, 0x0 | (crate << FEC_CSR_CRATE_OFFSET),&result,crate);
	}
    }
    deselect_fecs(crate);
    return errors;

    /* HERE BE THE SIMPLER WAY */
    /*
       XL3_Packet packet;
       packet.cmdHeader.cmdID = 0xD;
       uint32_t *p;
       p = (uint32_t *) packet.payload;
     *p = slot_mask;
     p++;
     *p = crate;
     do_xl3_cmd(&packet,crate_num);
     return 0;
     */
}

int set_crate_pedestals(int crate, uint32_t slot_mask, uint32_t pattern)
{
    /*  int i;
	uint32_t select_reg, result;

	for (i=0;i<16;i++){
	select_reg = FEC_SEL*i;
	if ((0x1<<i) & slot_mask){
	xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,pattern,&result,crate);
	}else{
	xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,0x0,&result,crate);
	}
	}
	deselect_fecs(crate);
	return 0;
     */
    /* HERE BE THE SIMPLER WAY */

    XL3_Packet packet;
    packet.cmdHeader.packet_type = 0xE;
    uint32_t *p;
    p = (uint32_t *) packet.payload;
    *p = slot_mask;
    p++;
    *p = pattern;
    SwapLongBlock(packet.payload,2);
    do_xl3_cmd(&packet,crate);
    return 0;


}

int32_t read_pmt(int crate_number, int32_t slot, int32_t limit, uint32_t *pmt_buf)
{
    XL3_Packet packet;
    MultiFC *commands = (MultiFC *) packet.payload;
#ifdef FIRST_WORD_BUG
    // this is a hack until PW fixes the sequencer
    static first[16]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
#endif //FIRST_WORD_BUG
    int count;
    uint32_t *memory;
    uint32_t diff;
    uint32_t select_reg, result;
    int i,j;
    int error;
#ifdef BIG_MOMMA
#define BIGDIFF	200 //max diff btw GT's for big momma
#define CNTXT	2
    int lastGT = 0;
    int theGT;
#endif //BIG_MOMMA
    error = 0;
    select_reg = FEC_SEL*slot;
    xl3_rw(FIFO_DIFF_PTR_R + select_reg + READ_REG,0x0,&diff,crate_number);
    diff &= 0xFFFFF;
    diff = diff - (diff%3);


    if ((3 * limit) < diff){
	if ((3*limit)*1.5 < diff){
	    printf("Memory level much higher than expected (%d > %d), possible fifo overflow\n",diff,3*limit);
	}else{
	    printf("Memory level over expected (%d > %d)\n",diff,3*limit);
	}
	diff = 3*limit; // make sure we do not read out more than limit pmt bundles
    }

#ifdef FIRST_WORD_BUG
    if ((diff > 3) && first[slot])
    {
	print_send("This is a hack until the sequencer is fixed\n", view_fdset);
	xl3_rw(select_reg + READ_MEM,0x0,pmt_buf,crate_number);
	xl3_rw(select_reg + READ_MEM,0x0,pmt_buf,crate_number);
	xl3_rw(select_reg + READ_MEM,0x0,pmt_buf,crate_number);
	first[slot] = 0;
    } // throw out the first word of data because it is currently garbage
#endif //FIRST_WORD_BUG

    // lets use the new function!
    // first we attempt to load up the xl3 with 'diff' # of reads to memory
    printf("attempting to read %d bundles\n",diff/3);
    packet.cmdHeader.packet_type = READ_PEDESTALS_ID;
    *(uint32_t *) packet.payload = slot;
    int reads_left = diff;
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
	do_xl3_cmd(&packet,crate_number);
	receive_data(this_read,command_number-1,crate_number,pmt_buf+(diff-reads_left));
	reads_left -= this_read;
    }

#ifdef BIG_MOMMA
    //make sure all other output is done
    fflush(stdout);
    //loop over data again
    if (!error){
	for (i=0;i<diff;i+=3){
	    theGT = (int) UNPK_FEC_GT_ID(pmt_buf+i);
	    if ((theGT > (lastGT + BIGDIFF)) ||
		    (theGT < (lastGT - BIGDIFF)) ){
		if (i == 0){
		    lastGT = theGT;
		    continue;
		}
		sprintf(psb, "Big Momma GT ID! iterator= %i\nGTID = %d (0x%06lx), lastGT = %d(0x%06lx)\n",
			i/3,theGT,theGT,lastGT,lastGT);
		//dump it and two previous, and one following
		sprintf(psb+strlen(psb), "Dumping GT and context (%d previous, 2 following)\n",CNTXT);
		print_send(psb, view_fdset);
		//dump_pmt_verbose(CNTXT + 3,(pmt_buf+i) - CNTXT*3); //RJB not in yet
	    }
	    lastGT = theGT;
	}
    }
#endif //BIG_MOMMA

    if (error){
	print_send("Bus error reading memory\n", view_fdset);
	error = 0;
    }
    count = diff / 3;
    deselect_fecs(crate_number);
    return count;
}

#define NUM_SLOTS 16


int update_crate_config(int crate, uint16_t slot_mask)
{
    XL3_Packet packet;
    packet.cmdHeader.packet_type = BUILD_CRATE_CONFIG_ID;
    uint32_t *p = (uint32_t *) packet.payload;
    *p = slot_mask;
    SwapLongBlock(p,1);
    do_xl3_cmd(&packet,crate);
    int errors;
    errors = *(uint32_t *) packet.payload;
    int j;
    for (j=0;j<16;j++){
	if ((0x1<<j) & slot_mask){
	    (crate_config[crate][j]) = *(hware_vals_t *) (packet.payload+4+j*sizeof(hware_vals_t));
	    SwapShortBlock(&(crate_config[crate][j].mb_id),1);
	    SwapShortBlock(crate_config[crate][j].dc_id,4);
	}
    }
    deselect_fecs(crate);
    return 0;
}

int multiloadsDac(int num_dacs, uint32_t *theDacs, uint32_t *theDAC_Values, int crate_number, uint32_t select_reg)
{
    XL3_Packet packet;
    packet.cmdHeader.packet_type = MULTI_LOADSDAC_ID;
    uint32_t *p;
    p = (uint32_t *) packet.payload;
    *p = num_dacs;
    p++;
    int i,j, errors = 0;
    for (i=0;i<16;i++){
	if ((i*FEC_SEL) == select_reg){
	    for (j=0;j<num_dacs;j++){
		*p = i;
		*(p+1) = *theDacs;
		*(p+2) = *theDAC_Values;
		p+=3;
		theDacs++;
		theDAC_Values++;
	    }
	    SwapLongBlock(packet.payload,num_dacs*3+1);
	    do_xl3_cmd(&packet,crate_number);
	    SwapLongBlock(packet.payload,1);
	    errors += *(uint32_t *) packet.payload;
	}
    }
    return errors;
}

int loadsDac(unsigned long theDAC, unsigned long theDAC_Value, int crate_number, uint32_t select_reg)
{
    XL3_Packet packet;
    packet.cmdHeader.packet_type = LOADSDAC_ID;
    uint32_t *p;
    p = (uint32_t *) packet.payload;
    int i, errors = 0;
    for (i=0;i<16;i++){
	if((i*FEC_SEL) == select_reg){
	    *p = i;
	    *(p+1) = theDAC;
	    *(p+2) = theDAC_Value;
	    SwapLongBlock(p,3);
	    do_xl3_cmd(&packet,crate_number);
	    SwapLongBlock(p,1);
	    errors += *p;
	}
    }
    return errors;
}

int read_bundle(char *buffer)
{
    uint32_t pmtword[3];
    char *words,*words2;
    int slot_num = 7;
    int crate_num = 2;
    int quiet = 0;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate_num = atoi(strtok(NULL, " "));
	    if (words[1] == 's')
		slot_num = atoi(strtok(NULL, " "));
	    if (words[1] == 'q')
		quiet = 1;
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: read_bundle -c"
			" [crate_num] -s [slot_num (int)] -q (enable quiet mode)\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }

    uint32_t crate,slot,chan,gt8,gt16,cmos_es16,cgt_es16,cgt_es8,nc_cc;
    int cell;
    double qlx,qhs,qhl,tac;
    xl3_rw(READ_MEM+slot_num*FEC_SEL,0x0,pmtword,crate_num);
    xl3_rw(READ_MEM+slot_num*FEC_SEL,0x0,pmtword+1,crate_num);
    xl3_rw(READ_MEM+slot_num*FEC_SEL,0x0,pmtword+2,crate_num);
    sprintf(psb,"%08x %08x %08x\n",pmtword[0],pmtword[1],pmtword[2]);
    print_send(psb,view_fdset);
    if (quiet == 0){
	crate = (uint32_t) UNPK_CRATE_ID(pmtword);
	slot = (uint32_t)  UNPK_BOARD_ID(pmtword);
	chan = (uint32_t)  UNPK_CHANNEL_ID(pmtword);
	cell = (int) UNPK_CELL_ID(pmtword);
	gt8 = (uint32_t)   UNPK_FEC_GT8_ID(pmtword);
	gt16 = (uint32_t)  UNPK_FEC_GT16_ID(pmtword);
	cmos_es16 = (uint32_t) UNPK_CMOS_ES_16(pmtword);
	cgt_es16 = (uint32_t)  UNPK_CGT_ES_16(pmtword);
	cgt_es8 = (uint32_t)   UNPK_CGT_ES_24(pmtword);
	nc_cc = (uint32_t) UNPK_NC_CC(pmtword);
	qlx = (double) MY_UNPK_QLX(pmtword);
	qhs = (double) UNPK_QHS(pmtword);
	qhl = (double) UNPK_QHL(pmtword);
	tac = (double) UNPK_TAC(pmtword);
	sprintf(psb,"crate %08x, slot %08x, chan %08x, cell %d, gt8 %08x, gt16 %08x, cmos_es16 %08x,"
		" cgt_es16 %08x, cgt_es8 %08x, nc_cc %08x, qlx %6.1f, qhs %6.1f, qhl %6.1f, tac %6.1f\n",
		crate,slot,chan,cell,gt8,
		gt16,cmos_es16,cgt_es16,cgt_es8,nc_cc,qlx,qhs,qhl,tac);
	print_send(psb,view_fdset);
    }
    return 0;
}


int changedelay(char *buffer)
{
    uint16_t i,j,k;
    uint16_t whichWord, bitposition;
    uint32_t cmosshiftdata[32][2]; // c is [row][column]
    uint32_t word, thisTimBit, temp;
    uint32_t delay_amount;
    int delay_per = 1;
    int cn = 2;
    int sn = 7;
    uint32_t select_reg;

    char *words,*words2;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c'){
		words2 = strtok(NULL, " ");
		cn = atoi(words2);
	    }else if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		sn = atoi(words2);
	    }else if (words[1] == 'd'){
		words2 = strtok(NULL, " ");
		delay_per = atoi(words2);
	    }else if (words[1] == 'h'){
		sprintf(psb,"Usage: change_delay -c [crate num] -s [slot num]"
			" -d [delay bits per channel]\n");
		print_send(psb,view_fdset);
		return -1;
	    }
	}
	words = strtok(NULL, " ");
    }

    select_reg = FEC_SEL*sn;

    // first build up the 35 bits of data for each channel.
    // build it up LSB to MSB
    for (j=0; j< 32; j++) {
	delay_amount = delay_per*j*0x0000001UL;
	if (delay_amount > 0x000000FUL)
	    delay_amount = 0x000000FUL;
	if (delay_per == 99)
	    delay_amount = 0x000000FUL;
	// 7 bits of tr100 width info -- see above for details
	//cmosshiftdata[j][0]  = (u_long) (0x00000040UL | delay_amount); 
	cmosshiftdata[j][0]  = (u_long) (0x0000007FUL); 
	// 4 bits of tr20 width info
	cmosshiftdata[j][0] |= (u_long) ((0 &  delay_amount) 
		<< TR20DELAYOFFSET);
	cmosshiftdata[j][0] |= (u_long) ((0 &  0x000000FUL) 
		<< TR20DELAYOFFSET);
	// 6 bits of tr20 delay info with mask bit
	cmosshiftdata[j][0] |= (u_long) ((0 &  0x0000003FUL) 
		<< TR20WIDTHOFFSET);
	// 8 bits of CMOS TAC trim info (2 x three twiddle bits plus two 
	// master mask bits.)
	// sprintf(psb, "tacbits: %hu \n", tacbits_ptr[j]);
	// print_send(psb, view_fdset);
	cmosshiftdata[j][0] |= (u_long) ((0 & 0xFFUL) 
		<< TACTRIMOFFSET); 
	// now rest of stuff is across boundry btw long words here
	// check these two.
	cmosshiftdata[j][0] |= (u_long) ((0 & 0x007F) 
		<< CMOSCRUFTOFFSETLOW);
	cmosshiftdata[j][1] = 0x0UL; // zero this first
	cmosshiftdata[j][1] |= (u_long)((0 & 0x0380) >> CMOSCRUFTOFFSETHIGH);
	// sprintf(psb, "cmosshift %08x \n", cmosshiftdata[j][0]);
	// print_send(psb, view_fdset);

    }

    // need to loop twice for 32 channels in two registers.
    for (k = 0; k < 2; k++) { // top, bottom halves
	// loop over data bits
	for (i = 0; i < 35; i++) { //35 data bits
	    // ugh, have to build data word 
	    word = 0x0UL | CMOS_PROG_SERSTOR;
	    for (j = 0; j< 16; j++) { // first build up data word
		// which of the two long words we want
		// remember that we load in the MSB first.
		whichWord = (i < 3) ? 1 : 0;
		// which bit of the two long words we want.
		if (whichWord == 1) 
		    bitposition = 2 - i; // top three bits
		else
		    bitposition = 34 - i; // bottom 32

		thisTimBit = cmosshiftdata[16 * k + j ][whichWord] 
		    & (0x1UL << bitposition );
		if (thisTimBit != 0 ) // bit was on
		    word |= 0x1UL <<  (j + CMOS_PROG_DATA_OFFSET ); // turn on bit
	    }
	    // now actually write the data

	    xl3_rw(CMOS_PROG(k) + select_reg + WRITE_REG,word,&temp,cn);
	    // now clock clock
	    word |= CMOS_PROG_CLOCK;
	    xl3_rw(CMOS_PROG(k) + select_reg + WRITE_REG,word,&temp,cn);
	}

	//now clock in SERSTOR
	xl3_rw(CMOS_PROG(k) + select_reg + WRITE_REG,(word & ~CMOS_PROG_SERSTOR) | CMOS_PROG_CLOCK,&temp,cn);
    }

    // do we need to wait a usec here?
    //sprintf(err_str, "Loaded CMOS shift registers ");
    //if ( BusStop == 0 )
    //  strcat(err_str, "successfully.\n");
    //else
    //  strcat(err_str, "unsuccessfully.\n");
    //SNO_printerr(9, INIT_FAC, err_str);
    return 0;
}

void dump_pmt_verbose(int n, uint32_t *pmt_buf, char* msg_buf)
{
    int i,j;
    char msg[10000];
    sprintf(msg,"\0");
    for (i=0;i<n*3;i+=3)
    {
	if (!(i%32)){
	    sprintf(msg+strlen(msg),"No\tCh\tCell\tGT\tQlx\tQhs\tQhl\tTAC\tES\tMC\tLGI\tNC/CC\tCr\tBd\n");
	    sprintf(msg+strlen(msg),"----------------------------------------------------------------\n");
	}
	sprintf(msg+strlen(msg),"% 4d\t%2u\t%2u\t%8u\t%4u\t%4u\t%4u\t%4u\t%u%u%u\t%1u\t%1u\t%1u\t%2u\t%2u\n",
		i/3,
		(uint32_t) UNPK_CHANNEL_ID(pmt_buf+i),
		(uint32_t) UNPK_CELL_ID(pmt_buf+i),
		(uint32_t) UNPK_FEC_GT_ID(pmt_buf+i),
		(uint32_t) UNPK_QLX(pmt_buf+i),
		(uint32_t) UNPK_QHS(pmt_buf+i),
		(uint32_t) UNPK_QHL(pmt_buf+i),
		(uint32_t) UNPK_TAC(pmt_buf+i),
		(uint32_t) UNPK_CMOS_ES_16(pmt_buf+i),
		(uint32_t) UNPK_CGT_ES_16(pmt_buf+i),
		(uint32_t) UNPK_CGT_ES_24(pmt_buf+i),
		(uint32_t) UNPK_MISSED_COUNT(pmt_buf+i),
		(uint32_t) UNPK_LGI_SELECT(pmt_buf+i),
		(uint32_t) UNPK_NC_CC(pmt_buf+i),
		(uint32_t) UNPK_CRATE_ID(pmt_buf+i),
		(uint32_t) UNPK_BOARD_ID(pmt_buf+i));
    }
    sprintf(msg_buf+strlen(msg_buf),"%s",msg);
    printf("%s",msg);
    return;
}

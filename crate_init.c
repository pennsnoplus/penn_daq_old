#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "penn_daq.h"
#include "fec_util.h"
#include "net_util.h"
#include "pillowtalk.h"
#include "db.h"


int crate_init(char *buffer)
{
    char *words,*words2;
    int crate_num=2;
    uint32_t xilinx_load=0,hv_reset=0;
    uint32_t slot_mask=0x80;
    int use_cbal=0,use_zdisc=0,use_ttot=0,use_all=0,use_hw=0;
    int result;

    int i,j,k,l;
    int crate,card;
    XL3_Packet packet;
    uint32_t *mb_num;
    char get_db_address[500];
    char ctc_address[500];
    hware_vals_t *hware_flip;


    // lets get the parameters from command line
    words = strtok(buffer, " ");
    while (words != NULL)
    {
	if (words[0] == '-')
	{
	    if (words[1] == 'c'){
		words2 = strtok(NULL, " ");
		crate_num=atoi(words2);
	    }else if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		slot_mask = strtoul(words2,(char**)NULL,16);
	    }else if (words[1] == 'x'){xilinx_load = 1;
	    }else if (words[1] == 'X'){xilinx_load = 2;
	    }else if (words[1] == 'v'){hv_reset = 1;
	    }else if (words[1] == 'b'){use_cbal = 1;
	    }else if (words[1] == 'd'){use_zdisc = 1;
	    }else if (words[1] == 't'){use_ttot = 1;
	    }else if (words[1] == 'a'){use_all = 1;
	    }else if (words[1] == 'w'){use_hw = 1;
	    }else if (words[1] == 'h'){
		sprintf(psb, "Usage: crate_init -c [crate_num] -s [slot_mask]\n");
		sprintf(psb+strlen(psb),
			"		  -x (load xilinx) -X (load cald xilinx) -v (reset HV dac)\n");
		sprintf(psb+strlen(psb),
			"		  -b (load cbal from db) -d (load zdisc from db) -t (load ttot from db) -a (load all from db) -w (use crate/card specific values from db)\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }

    if (connected_xl3s[crate_num] == -999){
	sprintf(psb, "Crate %d's XL3 is not connected\n",crate_num);
	print_send(psb, view_fdset);
	return -1;
    }

    sprintf(psb, "Initializing crate %d, slots %08x, xl:%d, hv:%d\n",crate_num,slot_mask,xilinx_load,hv_reset);
    print_send(psb, view_fdset);

    print_send("Sending database to XL3s\n",view_fdset);
    pt_init();
    pt_response_t* hw_response = NULL;
    pt_node_t* hw_rows = NULL;
    pt_response_t* debug_response = NULL;
    pt_node_t* debug_doc = NULL;

    if (use_hw == 1){
	sprintf(get_db_address,"http://%s:%s/%s/%s/get_fec?startkey=[%d,0]&endkey=[%d,15]",DB_ADDRESS,DB_PORT,DB_BASE_NAME,DB_VIEWDOC,crate_num,crate_num);
	hw_response = pt_get(get_db_address);
	if (hw_response->response_code != 200){
	    printf("Unable to connect to database. error code %d\n",(int)hw_response->response_code);
	    return -1;
	}
	pt_node_t *hw_doc = hw_response->root;
	pt_node_t* totalrows = pt_map_get(hw_doc,"total_rows");
	if (pt_integer_get(totalrows) != 16){
	    printf("Database error: not enough FEC entries\n");
	    return -1;
	}
	hw_rows = pt_map_get(hw_doc,"rows");
    }else{
	sprintf(get_db_address,"http://%s:%s/%s/CRATE_INIT_DOC",DB_ADDRESS,DB_PORT,DB_BASE_NAME);
	debug_response = pt_get(get_db_address);
	if (debug_response->response_code != 200){
	    printf("Unable to connect to database. error code %d\n",(int)debug_response->response_code);
	    return -1;
	}
	debug_doc = debug_response->root;
    }
    
    mb_num = (uint32_t *) packet.payload;

    // make sure crate_config is up to date
    if (use_cbal || use_zdisc || use_ttot || use_all)
	update_crate_config(crate_num,slot_mask);

    // GET ALL FEC DATA FROM DB
    for (i=0;i<16;i++){

	mb_t* mb_consts = (mb_t *) (packet.payload+4);
	packet.cmdHeader.packet_type = CRATE_INIT_ID;

	///////////////////////////
	// GET DEFAULT DB VALUES //
	///////////////////////////

	if (use_hw == 1){
	    pt_node_t* next_row = pt_array_get(hw_rows,i);
	    pt_node_t* key = pt_map_get(next_row,"key");
	    pt_node_t* value = pt_map_get(next_row,"value");
	    pt_node_t* hw = pt_map_get(value,"hw");
	    crate = pt_integer_get(pt_array_get(key,0));
	    card = pt_integer_get(pt_array_get(key,1));
	    if (crate != crate_num || card != i){
		printf("Database error : incorrect crate or card num (%d,%d)\n",crate,card);
		return -1;
	    }

	    parse_fec_hw(value,mb_consts);
	}else{
	    parse_fec_debug(debug_doc,mb_consts);
	}

	//////////////////////////////
	// GET VALUES FROM DEBUG DB //
	//////////////////////////////

	if ((use_cbal || use_all) && ((0x1<<i) & slot_mask)){
	    if (crate_config[crate_num][i].mb_id == 0x0000){
		printf("Warning: mb_id unknown. Using default values. Make sure to load xilinx before attempting to use debug db values.\n");
	    }else{
		char config_string[500];
		sprintf(config_string,"\"%04x\",\"%04x\",\"%04x\",\"%04x\",\"%04x\"",crate_config[crate_num][i].mb_id,crate_config[crate_num][i].dc_id[0],crate_config[crate_num][i].dc_id[1],crate_config[crate_num][i].dc_id[2],crate_config[crate_num][i].dc_id[3]);
		sprintf(get_db_address,"http://%s:%s/%s/%s/get_crate_cbal?startkey=[%s,9999999999]&endkey=[%s,0]&descending=true",DB_ADDRESS,DB_PORT,DB_BASE_NAME,DB_VIEWDOC,config_string,config_string);
		pt_response_t* cbal_response = pt_get(get_db_address);
		if (cbal_response->response_code != 200){
		    printf("Unable to connect to database. error code %d\n",(int)cbal_response->response_code);
		    return -1;
		}
		pt_node_t *viewdoc = cbal_response->root;
		pt_node_t* viewrows = pt_map_get(viewdoc,"rows");
		int n = pt_array_len(viewrows);
		if (n == 0){
		    printf("No crate_cbal documents for this configuration (%s). Continuing with default values.\n",config_string);
		}else{
		    pt_node_t* cbal_doc = pt_map_get(pt_array_get(viewrows,0),"value");
		    pt_node_t* vbal_low = pt_map_get(cbal_doc,"vbal_low");
		    pt_node_t* vbal_high = pt_map_get(cbal_doc,"vbal_high");
		    for (j=0;j<32;j++){
			mb_consts->vbal[0][j] = pt_integer_get(pt_array_get(vbal_low,j));
			mb_consts->vbal[1][j] = pt_integer_get(pt_array_get(vbal_high,j));
		    }
		}
	    }
	}

	if ((use_zdisc || use_all) && ((0x1<<i) & slot_mask)){
	    if (crate_config[crate_num][i].mb_id == 0x0000){
	    }else{
		char config_string[500];
		sprintf(config_string,"\"%04x\",\"%04x\",\"%04x\",\"%04x\",\"%04x\"",crate_config[crate_num][i].mb_id,crate_config[crate_num][i].dc_id[0],crate_config[crate_num][i].dc_id[1],crate_config[crate_num][i].dc_id[2],crate_config[crate_num][i].dc_id[3]);
		sprintf(get_db_address,"http://%s:%s/%s/%s/get_zdisc?startkey=[%s,9999999999]&endkey=[%s,0]&descending=true",DB_ADDRESS,DB_PORT,DB_BASE_NAME,DB_VIEWDOC,config_string,config_string);
		pt_response_t* zdisc_response = pt_get(get_db_address);
		if (zdisc_response->response_code != 200){
		    printf("Unable to connect to database. error code %d\n",(int)zdisc_response->response_code);
		    return -1;
		}
		pt_node_t *viewdoc = zdisc_response->root;
		pt_node_t* viewrows = pt_map_get(viewdoc,"rows");
		int n = pt_array_len(viewrows);
		if (n == 0){
		    printf("No zdisc documents for this configuration (%s). Continuing with default values.\n",config_string);
		}else{
		    pt_node_t* zdisc_doc = pt_map_get(pt_array_get(viewrows,0),"value");
		    pt_node_t* vthr = pt_map_get(zdisc_doc,"Zero_Dac_setting");
		    for (j=0;j<32;j++){
			mb_consts->vthr[j] = pt_integer_get(pt_array_get(vthr,j));
		    }
		}
	    }
	}


	if ((use_ttot || use_all) && ((0x1<<i) & slot_mask)){
	    if (crate_config[crate_num][i].mb_id == 0x0000){
		printf("Warning: mb_id unknown. Using default values. Make sure to load xilinx before attempting to use debug db values.\n");
	    }else{
		char config_string[500];
		sprintf(config_string,"\"%04x\",\"%04x\",\"%04x\",\"%04x\",\"%04x\"",crate_config[crate_num][i].mb_id,crate_config[crate_num][i].dc_id[0],crate_config[crate_num][i].dc_id[1],crate_config[crate_num][i].dc_id[2],crate_config[crate_num][i].dc_id[3]);
		sprintf(get_db_address,"http://%s:%s/%s/%s/get_ttot?startkey=[%s,9999999999]&endkey=[%s,0]&descending=true",DB_ADDRESS,DB_PORT,DB_BASE_NAME,DB_VIEWDOC,config_string,config_string);
		pt_response_t* ttot_response = pt_get(get_db_address);
		if (ttot_response->response_code != 200){
		    printf("Unable to connect to database. error code %d\n",(int)ttot_response->response_code);
		    return -1;
		}
		pt_node_t *viewdoc = ttot_response->root;
		pt_node_t* viewrows = pt_map_get(viewdoc,"rows");
		int n = pt_array_len(viewrows);
		if (n == 0){
		    printf("No set_ttot documents for this configuration (%s). Continuing with default values.\n",config_string);
		}else{
		    pt_node_t* ttot_doc = pt_map_get(pt_array_get(viewrows,0),"value");
		    pt_node_t* rmp = pt_map_get(ttot_doc,"rmp");
		    pt_node_t* vsi = pt_map_get(ttot_doc,"vsi");
		    for (j=0;j<8;j++){
			mb_consts->tdisc.rmp[j] = pt_integer_get(pt_array_get(rmp,j));
			mb_consts->tdisc.vsi[j] = pt_integer_get(pt_array_get(vsi,j));
		    }
		}
	    }
	}


	///////////////////////////////
	// SEND THE DATABASE TO XL3s //
	///////////////////////////////

	*mb_num = i;
	
	SwapLongBlock(&(packet.payload),1);	
	swap_fec_db(mb_consts);
	do_xl3_cmd_no_response(&packet,crate_num);

    }

    // GET CTC DELAY FROM CTC_DOC IN DB
    pt_response_t *ctc_response;
    sprintf(ctc_address,"http://%s:%s/%s/CTC_doc",DB_ADDRESS,DB_PORT,DB_BASE_NAME);
    ctc_response = pt_get(ctc_address);
    if (ctc_response->response_code != 200){
	printf("Error getting ctc document, error code %d\n",(int)ctc_response->response_code);
	return -1;
    }
    pt_node_t *ctc_doc = ctc_response->root;
    pt_node_t *ctc_delay_a = pt_map_get(ctc_doc,"delay");
    uint32_t ctc_delay = strtoul(pt_string_get(pt_array_get(ctc_delay_a,crate_num)),(char**) NULL,16);


    // START CRATE_INIT ON ML403
    print_send("Beginning crate_init.\n",view_fdset);

    packet.cmdHeader.packet_type = CRATE_INIT_ID;
    *mb_num = 666;

    uint32_t *p;
    p = (uint32_t *) (packet.payload+4);
    *p = xilinx_load;
    p++;
    *p = hv_reset;
    p++;
    *p = slot_mask;
    p++;
    *p = ctc_delay;
    p++;
    *p = 0x0;

    SwapLongBlock(&(packet.payload),5);

    do_xl3_cmd(&packet,crate_num); 

    // NOW PROCESS RESULTS AND POST TO DB
    hware_flip = (hware_vals_t *) (packet.payload+4);

    for (i=0;i<16;i++){
	SwapShortBlock(&(hware_flip->mb_id),1);
	SwapShortBlock(&(hware_flip->dc_id),4);
	hware_flip++;
    }

    //update_crate_configuration(crate_num,hware_flip);  //FIXME
    print_send("Crate configuration updated.\n",view_fdset);
    print_send("*******************************\n",view_fdset);
    pt_free_response(ctc_response);
    pt_cleanup();

    return 0;
}

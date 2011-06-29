#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "include/Record_Info.h"
#include "include/xl_regs.h"

#include "penn_daq.h"
#include "xl3_util.h"
#include "net_util.h"
#include "db.h"

int final_test(char *buffer)
{
    uint32_t slot_mask = 0x2000;
    int crate_num = 12;

    int i;

    char *words,*words2;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'p'){
		printf("location set to penn test stand\n");
		current_location = 2;
	    }
	    if (words[1] == 'u'){
		printf("location set to underground\n");
		current_location = 1;
	    }
	    if (words[1] == 'a'){
		printf("location set to above ground test stand\n");
		current_location = 0;
	    }
	    if (words[1] == 'c')
		crate_num = atoi(strtok(NULL, " "));
	    if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		slot_mask = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: final_test -c [crate number] -s [slot mask (hex)]"
			"-a (above ground) -u (under ground) -p (penn)\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }

    
    pt_init();
    char ft_ids[16][250]; 
    char id_string[16*250];
    pt_node_t *ft_docs[16];
    char command_buffer[100];
    char comments[1000];
    memset(comments, '\0', 1000);
    memset(id_string,'\0',16*50);

    system("clear");

    printf("-------------------------------------------\n");
    printf("Welcome to final test!\nHit enter to start\n");
    read_from_tut(comments);
    printf("-------------------------------------------\n");

    sprintf(command_buffer,"crate_init -s %04x -c %d -x",slot_mask,crate_num);
    crate_init(command_buffer);
    printf("-------------------------------------------\n");
    
    printf("now connecting to and initializing mtcd\n");
    connect_to_SBC(SBC_PORT, server);
    printf("-------------------------------------------\n");
    sprintf(command_buffer,"mtc_init -x");
    mtc_init(command_buffer);
    printf("-------------------------------------------\n");


    printf("If any boards could not initialize properly, type \"quit\" now to exit the test.\n Otherwise hit enter to continue.\n");
    read_from_tut(comments);
    if (strncmp("quit",comments,4) == 0){
	printf("Exiting final test\n");
	return -1;
    }

    // set up the final test documents
    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    get_new_id(ft_ids[i]);
	    ft_docs[i] = pt_map_new();
	    sprintf(id_string+strlen(id_string),"%s ",ft_ids[i]);
	    printf(".%s.\n",id_string);
	}
    }

    printf("Now starting board_id\n");
    sprintf(command_buffer,"board_id -c %d -s %04x",crate_num,slot_mask);
    board_id(command_buffer);
    printf("-------------------------------------------\n");
    
    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    printf("Please enter any comments for slot %i motherboard now.\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"mb_comments",pt_string_new(comments));
	    printf("Has this slot been refurbished? (y/n)\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"refurbished",pt_string_new(comments));
	    printf("Has this slot been cleaned? (y/n)\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"cleaned",pt_string_new(comments));
	    printf("Time to measure resistance across analog outs and cmos address lines. For the cmos address lines"
		    "it's easier if you do it during the fifo mod\n");
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"analog_out_res",pt_string_new(comments));
	    printf("Please enter any comments for slot %i db 0 now.\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"db_0_comments",pt_string_new(comments));
	    printf("Please enter any comments for slot %i db 1 now.\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"db_1_comments",pt_string_new(comments));
	    printf("Please enter any comments for slot %i db 2 now.\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"db_2_comments",pt_string_new(comments));
	    printf("Please enter any comments for slot %i db 3 now.\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"db_3_comments",pt_string_new(comments));
	    printf("Please enter dark matter measurements for slot %i db 0 now.\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"db_0_dm",pt_string_new(comments));
	    printf("Please enter dark matter measurements for slot %i db 1 now.\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"db_1_dm",pt_string_new(comments));
	    printf("Please enter dark matter measurements for slot %i db 2 now.\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"db_2_dm",pt_string_new(comments));
	    printf("Please enter dark matter measurements for slot %i db 3 now.\n",i);
	    read_from_tut(comments);
	    pt_map_set(ft_docs[i],"db_3_dm",pt_string_new(comments));
	}
    }
   

    printf("Enter N100 DC offset\n");
    read_from_tut(comments);
    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    pt_map_set(ft_docs[i],"N100_dc_offset",pt_string_new(comments));
	}
    }
    printf("Enter N20 DC offset\n");
    read_from_tut(comments);
    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    pt_map_set(ft_docs[i],"N20_dc_offset",pt_string_new(comments));
	}
    }
    printf("Enter esum hi DC offset\n");
    read_from_tut(comments);
    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    pt_map_set(ft_docs[i],"esumhi_dc_offset",pt_string_new(comments));
	}
    }
    printf("Enter esum lo DC offset\n");
    read_from_tut(comments);
    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    pt_map_set(ft_docs[i],"esumlo_dc_offset",pt_string_new(comments));
	}
    }

    printf("Thank you. Please hit enter to continue with the rest of final test. This may take a while.\n");
    read_from_tut(comments);

    
    // starting the tests
    printf("-------------------------------------------\n");
    sprintf(command_buffer,"fec_test -c %d -s %04x -d -# %s",crate_num,slot_mask,id_string);
    fec_test(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"vmon -c %d -s %04x -d -# %s",crate_num,slot_mask,id_string);
    vmon(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"cgt_test_1 -c %d -s %04x -d -# %s",crate_num,slot_mask,id_string);
    cgt_test_1(command_buffer);

    //////////////////////////////////////////////////////////////
    // now gotta turn pulser on and off to get rid of garbage
    sprintf(command_buffer,"readout_add_mtc -c %d -f 0",crate_num);
    readout_add_mtc(command_buffer);
    sprintf(command_buffer,"stop_pulser");
    stop_pulser(command_buffer);
    //////////////////////////////////////////////////////////////

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"ped_run -c %d -s %04x -d -# %s",crate_num,slot_mask,id_string);
    ped_run(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"crate_cbal -c %d -s %04x -d -# %s",crate_num,slot_mask,id_string);
    crate_cbal(command_buffer);

    //////////////////////////////////////////////////////////////
    // now gotta turn pulser on and off to get rid of garbage
    sprintf(command_buffer,"readout_add_mtc -c %d -f 0",crate_num);
    readout_add_mtc(command_buffer);
    sprintf(command_buffer,"stop_pulser");
    stop_pulser(command_buffer);
    //////////////////////////////////////////////////////////////

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"crate_init -c %d -s %04x -b",crate_num,slot_mask);
    crate_init(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"ped_run -c %d -s %04x -b -d -# %s",crate_num,slot_mask,id_string);
    ped_run(command_buffer);
    
    printf("-------------------------------------------\n");
    sprintf(command_buffer,"chinj_scan -c %d -s %04x -l 0 -u 5000 -w 100 -n 10 -d -# %s",crate_num,slot_mask,id_string);
    chinj_scan(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"set_ttot -c %d -s %04x -t 440 -d -# %s",crate_num,slot_mask,id_string);
    //set_ttot(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"crate_init -c %d -s %04x -t",crate_num,slot_mask);
    //crate_init(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"get_ttot -c %d -s %04x -t 440 -d -# %s",crate_num,slot_mask,id_string);
    //get_ttot(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"disc_check -c %d -s %04x -n 500000 -d -# %s",crate_num,slot_mask,id_string);
    disc_check(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"cmos_m_gtvalid -c %d -s %04x -g 400 -n -d -# %s",crate_num,slot_mask,id_string);
    cmos_m_gtvalid(command_buffer);

    // would put see_reflections here

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"zdisc -c %d -s %04x -o 0 -r 100 -d -# %s",crate_num,slot_mask,id_string);
    zdisc(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"mtc_init");
    mtc_init(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"crate_init -c %d -s %04x -x",crate_num,slot_mask);
    crate_init(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"mb_stability_test -c %d -s %04x -n 50 -d -# %s",crate_num,slot_mask,id_string);
    mb_stability_test(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"fifo_test -c %d -s %04x -d -# %s",crate_num,slot_mask,id_string);
    fifo_test(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"crate_init -c %d -s %04x -X",crate_num,slot_mask);
    crate_init(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"cald_test -c %d -s %04x -l 750 -u 3500 -n 200 -d -# %s",crate_num,slot_mask,id_string);
    cald_test(command_buffer);

    printf("-------------------------------------------\n");
    sprintf(command_buffer,"crate_init -c %d -s %04x -x",crate_num,slot_mask);
    crate_init(command_buffer);

    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    printf("-------------------------------------------\n");
	    sprintf(command_buffer,"mem_test -c %d -s %d -d -# %s",crate_num,i,ft_ids[i]);
	    mem_test(command_buffer);
	}
    }

    printf("-------------------------------------------\n");
    printf("Final test finished. Now updating the database.\n");

    // update the database
    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    pt_map_set(ft_docs[i],"type",pt_string_new("final_test"));
	    post_debug_doc_with_id(crate_num, i, ft_ids[i], ft_docs[i]);
	}
    }

    printf("-------------------------------------------\n");

    return 0;

}



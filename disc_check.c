#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "include/xl_regs.h"
#include "include/Record_Info.h"

#include "penn_daq.h"
#include "fec_util.h"
#include "mtc_util.h"
#include "net_util.h"

#define MAX_PER_PACKET 5000

static int setup_crate(int cn, uint32_t slot_mask);

static int setup_softgt(uint32_t crate_num);

int disc_check(char *buffer)
{
    int crate_num = 2;
    uint16_t slot_mask = 0x2000;
    int nped = 100000;
    int i,j,errors,slot,passflag;
    uint32_t ctemp;
    uint32_t count_i[16][32],count_f[16][32];
    uint32_t cdiff;
    int update_db = 0;
    int final_test = 0;
    char ft_ids[16][50];
    int chan_errors[16][32];
    int chan_diff[16][32];

    // get command line options
    char *words,*words2;

    // lets get the parameters from command line
    words = strtok(buffer, " ");
    while (words != NULL){
        if (words[0] == '-'){
            if (words[1] == 'c'){
                words2 = strtok(NULL, " ");
                crate_num = atoi(words2);
            }else if (words[1] == 's'){
                words2 = strtok(NULL, " ");
                slot_mask = strtoul(words2,(char**)NULL,16);
            }else if (words[1] == 'n'){
                words2 = strtok(NULL, " ");
                nped = atoi(words2);
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
            }else if (words[1] == 'h'){
                printsend("Usage: disc_check -c [crate number] -s [slot mask (hex)]"
                        " -n [num peds] -d (update db)\n");
                return -1;
            }
        }
        words = strtok(NULL, " ");
    }

   printsend("************************************************\n");
   printsend("Starting discriminator check for crate: %d\n",crate_num);
    setup_softgt(crate_num);
    setup_crate(crate_num,slot_mask);

    // get initial data
    for (i=0;i<16;i++){
        if ((0x1<<i) & slot_mask){
            get_cmos_total_count2(crate_num,i,count_i[i]);
        }
    } 

    // fire pedestals
    i = nped;
    char hello[1000];
    while(i>0){
        if (i>MAX_PER_PACKET){
            multi_softgt(5000);
            i-=MAX_PER_PACKET;
        }else{
            multi_softgt(i);
            i=0;
        }
        if (i%50000 == 0)
           printsend("%d\n",i);
    }

    // get final data
    for (i=0;i<16;i++){
        if ((0x1<<i) & slot_mask){
            get_cmos_total_count2(crate_num,i,count_f[i]);
        }
    } 

    // flag bad channels
    for (i=0;i<16;i++){
        if ((0x1<<i) & slot_mask){
            for (j=0;j<32;j++){
                chan_errors[i][j] = 0;
                chan_diff[i][j] = 0;
                cdiff = count_f[i][j]-count_i[i][j];
                if (cdiff != nped){
                   printsend("cmos_count != nped for slot %d chan %d. Nped: %d, cdiff: %d\n",i,j,nped,cdiff);
                    chan_errors[i][j] = 1;
                    chan_diff[i][j] = cdiff-nped;
                    errors++;
                }
            }
        }
    }


    if (update_db){
       printsend("updating the database\n");
        ;
        for (slot=0;slot<16;slot++){
            if ((0x1<<slot) & slot_mask){
                JsonNode *newdoc = json_mkobject();
                json_append_member(newdoc,"type",json_mkstring("disc_check"));
                JsonNode *diff_node = json_mkarray();
                JsonNode *error_node = json_mkarray();
                passflag = 0;
                for (i=0;i<32;i++){
                    if (chan_errors[slot][i] == 1)
                        passflag = 1;
                    json_append_element(error_node,json_mknumber((double)chan_errors[slot][i]));
                    json_append_element(diff_node,json_mknumber((double)chan_diff[slot][i]));
                }
                json_append_member(newdoc,"count_minus_peds",diff_node);
                json_append_member(newdoc,"errors",error_node);
                if (passflag == 0){
                    json_append_member(newdoc,"pass",json_mkstring("yes"));
                }else{
                    json_append_member(newdoc,"pass",json_mkstring("no"));
                }
                if (final_test)
                    json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[slot]));	
                post_debug_doc(crate_num,slot,newdoc);
                json_delete(newdoc); // only need to delete the head node
            }
        }
    }



   printsend("****************************************\n");
    return errors;
}

static int setup_crate(int cn, uint32_t slot_mask)
{
    int i;
    uint32_t select_reg, result,temp;
    printsend("Resetting fifos.\n");
    for (i=0;i<16;i++){ //FIXME 
        select_reg = FEC_SEL * i;

        // disable pedestals
        xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,0x0,&result,cn);

        if ((0x1<<i) & slot_mask){
            // reset fifo
            xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0xFFFFFFFF,&result,cn);
            // mask in crate_address and fifo reset
            xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0 | (cn << FEC_CSR_CRATE_OFFSET) | 0x6,&result,cn);
            xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0 | (cn << FEC_CSR_CRATE_OFFSET),&result,cn);
            xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0x0,&result,cn);
            // enable pedestals
            xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,0xFFFFFFFF,&result,cn);
        }
    }

    deselect_fecs(cn);
    return 0;
}

static int setup_softgt(uint32_t crate_num)
{
    uint32_t mtc_crate_mask;

    reset_memory();
    set_gt_counter(0);
    setup_pedestals(0,25,150,0);
    unset_gt_crate_mask(MASKALL);
    unset_ped_crate_mask(MASKALL);
    mtc_crate_mask = get_mtc_crate_mask(crate_num);
    //set_gt_crate_mask(mtc_crate_mask);
    //set_ped_crate_mask(mtc_crate_mask);
    set_gt_crate_mask(MASKALL);
    set_ped_crate_mask(MASKALL);
    //    set_gt_crate_mask(MSK_TUB);
    //    set_ped_crate_mask(MSK_TUB);
    return 0;
}


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "include/xl_regs.h"
#include "include/dacNumber.h"

#include "penn_daq.h"
#include "fec_util.h"
#include "mtc_util.h"
#include "net_util.h"
//#include "pouch.h"
//#include "json.h"
#include "ttot.h"

int get_ttot(char * buffer)
{
    if (sbc_is_connected == 0){
        sprintf(psb,"SBC not connected.\n");
        print_send(psb, view_fdset);
        return -1;
    }
    int errors;
    int targettime = 400;
    int crate = 2;
    uint32_t slot_mask = 0x2000;
    uint16_t times[32*16];
    int tot_errors[16][32];
    int result;
    int i,j,slot,passflag;
    int update_db = 0;
    int final_test = 0;
    char ft_ids[16][50];
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
            }else if (words[1] == 't'){
                words2 = strtok(NULL, " ");
                targettime = atoi(words2);
            }else if (words[1] == 'h'){
                sprintf(psb,"Usage: get_ttot -c"
                        " [crate_num] -s [slot_mask (hex)] -t [target time] -d (update debug db)\n");
                print_send(psb, view_fdset);
                return 0;
            }
        }
        words = strtok(NULL, " ");
    }

    unset_gt_mask(MASKALL);
    errors = setup_pedestals(0.0,60,100,0); //freq=0,width=60,delay=100+0
    unset_gt_crate_mask(MASKALL);
    set_gt_crate_mask(MSK_CRATE21); //turn on the TUB
    set_ped_crate_mask(MSK_CRATE21); //turn on the TUB
    unset_ped_crate_mask(MASKALL);
    set_ped_crate_mask(0x1<<crate | MSK_CRATE21); // leave TUB masked in

    result = disc_m_ttot(crate,slot_mask,150,times);

    printf("crate\t slot\t channel\t time\n");

    for (i=0;i<16;i++){
        if ((0x1<<i) & slot_mask){
            for(j=0;j<32;j++){
                tot_errors[i][j] = 0;
                printf("%d\t %d\t %d\t %d",crate,i,j,times[i*32+j]);
                if (targettime > times[i*32+j]){
                    printf(">>>Warning: time less than %d nsec",targettime);
                    tot_errors[i][j] = 1;
                }
                printf("\n");
            }
        }
    }

    if (update_db){
        printf("updating the database\n");
        ;
        for (slot=0;slot<16;slot++){
            if ((0x1<<slot) & slot_mask){
                JsonNode *newdoc = json_mkobject();
                json_append_member(newdoc,"type",json_mkstring("get_ttot"));
                json_append_member(newdoc,"targettime",json_mknumber((double)targettime));
                JsonNode *times_node = json_mkarray();
                JsonNode *error_node = json_mkarray();
                passflag = 0;
                for (i=0;i<32;i++){
                    if (tot_errors[slot][i] == 1)
                        passflag = 1;
                    json_append_element(error_node,json_mknumber((double)tot_errors[slot][i]));
                    json_append_element(times_node,json_mknumber((double)times[slot*32+i]));
                }
                json_append_member(newdoc,"times",times_node);
                json_append_member(newdoc,"errors",error_node);
                if (passflag == 0){
                    json_append_member(newdoc,"pass",json_mkstring("yes"));
                }else{
                    json_append_member(newdoc,"pass",json_mkstring("no"));
                }
                if (final_test)
                    json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[slot]));	
                post_debug_doc(crate,slot,newdoc);
                json_delete(newdoc); // delete the head ndoe
            }
        }
    }

    return 0;
}


int set_ttot(char * buffer)
{
    printf(".%s.\n",buffer);
    if (sbc_is_connected == 0){
        sprintf(psb,"SBC not connected.\n");
        print_send(psb, view_fdset);
        return -1;
    }
    int errors;
    int targettime = 400;
    int crate = 2;
    uint32_t slot_mask = 0x2000;
    int i,j;
    int slot, passflag;
    int update_db = 0;
    int final_test = 0;
    char ft_ids[16][50];
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
            }else if (words[1] == 't'){
                words2 = strtok(NULL, " ");
                targettime = atoi(words2);
            }else if (words[1] == 'h'){
                sprintf(psb,"Usage: set_ttot -c"
                        " [crate_num] -s [slot_mask (hex)] -t [target time] -d (update debug db)\n");
                print_send(psb, view_fdset);
                return 0;
            }
        }
        words = strtok(NULL, " ");
    }

    unset_gt_mask(MASKALL);
    errors = setup_pedestals(0.0,60,100,0); //freq=0,width=60,delay=100+0
    unset_gt_crate_mask(MASKALL);
    set_gt_crate_mask(MSK_CRATE21); //turn on the TUB
    set_ped_crate_mask(MSK_CRATE21); //turn on the TUB
    unset_ped_crate_mask(MASKALL);
    set_ped_crate_mask(0x1<<crate | MSK_CRATE21); // leave TUB masked in

    uint16_t allrmps[16*8],allvsis[16*8],alltimes[16*32];
    int tot_errors[16*8];
    disc_s_ttot(crate,slot_mask,targettime,allrmps,allvsis,alltimes,tot_errors);

    if (update_db){
        printf("updating the database\n");
        ;
        for (slot=0;slot<16;slot++){
            if ((0x1<<slot) & slot_mask){
                JsonNode *newdoc = json_mkobject();
                json_append_member(newdoc,"type",json_mkstring("set_ttot"));
                json_append_member(newdoc,"targettime",json_mknumber((double)targettime));
                JsonNode *rmp = json_mkarray();
                JsonNode *vsi = json_mkarray();
                JsonNode *times = json_mkarray();
                JsonNode *error_node = json_mkarray();
                passflag = 0;
                for (i=0;i<8;i++){
                    if (tot_errors[slot*8+i] == 1)
                        passflag = 1;
                    json_append_element(rmp,json_mknumber((double)allrmps[slot*8+i]));
                    json_append_element(vsi,json_mknumber((double)allvsis[slot*8+i]));
                    json_append_element(error_node,json_mknumber((double)tot_errors[slot*8+i]));
                    JsonNode *times_temp = json_mkarray();
                    for (j=0;j<4;j++){
                        json_append_element(times_temp,json_mknumber((double)alltimes[slot*32+i*4+j]));
                    }
                    json_append_element(times,times_temp);
                }
                json_append_member(newdoc,"rmp",rmp);
                json_append_member(newdoc,"vsi",vsi);
                json_append_member(newdoc,"times",times);
                json_append_member(newdoc,"errors",error_node);
                if (passflag == 0){
                    json_append_member(newdoc,"pass",json_mkstring("yes"));
                }else{
                    json_append_member(newdoc,"pass",json_mkstring("no"));
                }
                if (final_test)
                    json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[slot]));	
                post_debug_doc(crate,slot,newdoc);
                json_delete(newdoc); // head node needs deleting
            }
        }
    }

    return 0;
}

int disc_s_ttot(int crate, uint32_t slot_mask, int goal_time, uint16_t *allrmps,uint16_t *allvsis, uint16_t *times, int *errors)
{
    int i,j,k,l;
    uint16_t rmp[8];
    uint16_t vsi[8];
    uint16_t rmpup[8],vli[8];
    uint16_t chips_not_finished;
    int32_t diff[32];
    uint32_t theDACs[50];
    uint32_t theDAC_Values[50];
    uint32_t select_reg;
    int num_dacs;
    int update_db = 0;
    int result;
    for (i=0;i<16;i++){
        if ((0x1<<i) & slot_mask){
            // set default values
            for (j=0;j<8;j++){
                rmpup[j] = RMPUPDEFAULT;
                rmp[j] = RMPDEFAULT-10;
                vsi[j] = VSIDEFAULT;
                vli[j] = VLIDEFAULT;
            }
            // load default values
            num_dacs = 0;
            select_reg = FEC_SEL*i;
            for (j=0;j<8;j++){
                errors[i*8+j] = 0;
                theDACs[j*4+0] = d_rmpup[j]; 	
                theDAC_Values[j*4+0] = rmpup[j];
                theDACs[j*4+1] = d_rmp[j]; 	
                theDAC_Values[j*4+1] = rmp[j];
                theDACs[j*4+2] = d_vsi[j]; 	
                theDAC_Values[j*4+2] = vsi[j];
                theDACs[j*4+3] = d_vli[j]; 	
                theDAC_Values[j*4+3] = vli[j];
                num_dacs += 4;
            }
            multiloadsDac(num_dacs,theDACs,theDAC_Values,crate,FEC_SEL*i);

            printf("Working on crate/board %d %d, target time %d\n",crate,i,goal_time);
            chips_not_finished = 0xFF; // none finished

            while (chips_not_finished){
                // measure timing
                result = disc_m_ttot(crate,0x1<<i,150,times);
                for (j=0;j<8;j++){ // loop over disc chips
                    for (k=0;k<4;k++){ // loop over channels in chip
                        diff[4*j+k] = times[i*32+j*4+k] - goal_time;
                    } // end loop over channels in chip
                    if ((diff[4*j+0] > 0) && (diff[4*j+1] > 0) && (diff[4*j+2] > 0) && (diff[4*j+3] > 0)
                            && (chips_not_finished & (0x1<<j))){ // if diffs all positive
                        chips_not_finished &= ~(0x1<<j); // that chip is finished
                        printf("Fit Ch(%d) (RMP/VSI %d %d) Times:\t%d\t%d\t%d\t%d\n",j,rmp[j],vsi[j],times[i*32+j*4+0],times[i*32+j*4+1],times[i*32+j*4+2],times[i*32+j*4+3]);
                        allrmps[i*8+j] = rmp[j];
                        allvsis[i*8+j] = vsi[j];
                    }else{ // if not done, adjust DACs
                        if ((rmp[j] <= MAX_RMP_VALUE) && (vsi[j] > MIN_VSI_VALUE) && (chips_not_finished & (0x1<<j))){
                            rmp[j]++;
                        }
                        if ((rmp[j] > MAX_RMP_VALUE) && (vsi[j] > MIN_VSI_VALUE) && (chips_not_finished & (0x1<<j))){
                            rmp[j] = RMPDEFAULT-10;
                            vsi[j] -=2;
                        }
                        if ((vsi[j] <= MIN_VSI_VALUE) && (chips_not_finished & (0x1<<j))){
                            // out of bounds, end loop with error
                            printf("RMP/VSI is too big for disc chip %d! (%d %d)\n",j,rmp[j],vsi[j]);
                            printf("Aborting slot %d setup.\n",i);
                            errors[i*8+j] = 1;
                            for (l=0;l<8;l++){
                                if (chips_not_finished & (0x1<<l)){
                                    printf("Slot %d Chip %d\tRMP/VSI: %d %d <- unfinished\n",i,j,rmp[l],vsi[l]);
                                }
                            }
                            goto end; // oh the horror!
                        }
                    }
                } // end loop over disc chips

                // load new values
                num_dacs = 0;
                select_reg = FEC_SEL*i;
                for (j=0;j<8;j++){
                    theDACs[j*2+0] = d_rmp[j]; 	
                    theDAC_Values[j*2+0] = rmp[j];
                    theDACs[j*2+1] = d_vsi[j]; 	
                    theDAC_Values[j*2+1] = vsi[j];
                    num_dacs += 2;
                }
                multiloadsDac(num_dacs,theDACs,theDAC_Values,crate,select_reg);

            } // end while (chips not finished)
end:
            continue;

            if (update_db){
            }

        } // end if slot mask
    } // end loop over slots
    return 0;
}

int disc_m_ttot(int crate, uint32_t slot_mask, int start_time, uint16_t *disc_times)
{
    int i,j,k;
    int result;
    int increment = 1;
    int time;
    uint32_t init[32],fin[32];
    uint32_t chan_done_mask;
    float real_delay;

    for (i=0;i<16;i++){
        if ((0x1<<i) & slot_mask){
            result = set_crate_pedestals(crate,0x1<<i,0xFFFFFFFF);
            chan_done_mask = 0x0;
            for (time = start_time;time<=MAXTIME;time+=increment){
                // setup gt delay
                real_delay = set_gt_delay((float) time);
                result = get_cmos_total_count2(crate,i,init);
                for (j=0;j<32;j++){ // loop over channels
                    //initial conditions
                    //result = get_cmos_total_count(crate,i,j,&init[j]);
                }

                // send 20 soft gts
                mtc_multi_write(0xC,0x0,NUM_PEDS);

                // now read out the chips
                result = get_cmos_total_count2(crate,i,fin);
                for (j=0;j<32;j++){
                    //result = get_cmos_total_count(crate,i,j,&fin[j]);
                    fin[j] -= init[j];
                    // check if we got all the pedestals from the TUB too
                    if ((fin[j] >= 2*NUM_PEDS) & !(chan_done_mask & (0x1<<j))){
                        chan_done_mask |= 0x1<<j;
                        disc_times[i*32+j] = time+TUB_DELAY;
                    }
                } // end loop over channels
                if (chan_done_mask == 0xFFFFFFFF)
                    break; // no need to keep incrementing time
                if (time == MAXTIME){
                    for(j=0;j<32;j++){
                        if ((0x1<<j) & !chan_done_mask){
                            disc_times[i*32+j] = time+TUB_DELAY;
                        }
                    }
                }
            }// for time<=MAXTIME
        } // end if slot mask
    } // end loop over slots
    return 0;
}

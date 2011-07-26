// Main Program to test global trigger / synclear functionality of
// CMOS, CGT5000, and MTCD components

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
//#include "db.h"
//#include "pouch.h"
//#include "json.h"


#define TWO_16_M1	0xFFFF /* 2^16-1 */
#define TWO_24_M1	0xFFFFFF /* 2^24-1 */

static int setup_crate(int cn, uint32_t slot_mask);

static int setup_softgt(uint32_t crate_num);

int cgt_test_1(char* buffer)
{
    uint32_t crate_num, chan_mask, slot_mask;
    int i,j,k;
    int num_chans;
    uint32_t numPedestals;

    uint32_t select_reg, result;
    uint32_t bundles[3];
    uint32_t crate_id, slot_id, chan_id, nc_id, gt16_id, gt8_id, es16;

    int update_db = 0;
    int final_test = 0;
    char ft_ids[16][50];
    uint32_t badchanmask;
    int missing_bundles[16];
    int chan_errors[16][32];
    char error_history[16][50000];
    int max_errors[16];

    crate_num = 2;
    slot_mask = 0x1;
    chan_mask = 0xFFFFFFFF;
    num_chans = 32;

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
            }else if (words[1] == 'l'){
                words2 = strtok(NULL, " ");
                chan_mask = strtoul(words2,(char **)NULL,16);
               printsend("chan mask now %08x\n",chan_mask);
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
                printsend("Usage: cgt_test_1 -c [crate number] -s [slot mask (hex)]"
                        " -l [channel mask (hex)] -d (update db)\n");
                return -1;
            }
        }
        words = strtok(NULL, " ");
    }

    num_chans = 0;
    for (i=0;i<32;i++){
        if ((0x1<<i) & chan_mask)
            num_chans++;
    }
    for(i=0;i<16;i++){
        missing_bundles[i] = 0;
        max_errors[i] = 0;
        sprintf(error_history[i],"\0");
    }

    //setup mtc
    setup_softgt(crate_num);
    setup_crate(crate_num,slot_mask);

    printsend("** Starting GT increment Test **\n"
              "Crate number: %hu\n"
              "Slot and Channel mask: %08x %08x\n", crate_num, slot_mask,chan_mask);

    //select desired MB and channel
    for (i=0;i<16;i++){
        if ((slot_mask & (0x1<<i)) != 0){
            select_reg = FEC_SEL*i;

            //enable pedestals
            xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG, chan_mask, &result,crate_num);

            deselect_fecs(crate_num);
        }
    }

    //initialization

    numPedestals = (1*(TWO_16_M1) + 10000 );	
    printsend("Going to fire pulser %u times. \n",numPedestals);

    XL3_Packet packet;

    int total_pulses = 0;
    int numgt = 0;

    for (j=0;j<16;j++){
        // we skip 4999 gtids then check each 5000th one
        if (j != 13)
            numgt = 4999;
        else
            numgt = 534; 

        multi_softgt(numgt);

        // now loop over slots and make sure we got all the gts
        for (i=0;i<16;i++){
            if ((slot_mask & (0x1<<i)) && (max_errors[i] == 0)){
                select_reg = FEC_SEL*i;
                xl3_rw(FIFO_DIFF_PTR_R + select_reg + READ_REG,0x0,&result,crate_num);
                if ((result & 0x000FFFFF) != numgt*3*num_chans){
                   printsend("Not enough bundles slot %d, expected %d, found %u.\n",i,numgt*3*num_chans,result&0x000FFFFF);
                    sprintf(error_history[i]+strlen(error_history[i]),"Not enough bundles slot %d, expected %d, found %u.\n",i,numgt*3*num_chans,result&0x000FFFFF);
                    missing_bundles[i] = 1;
                }

                // reset the fifo
                packet.cmdHeader.packet_type = RESET_FIFOS_ID;
                *(uint32_t *) packet.payload = slot_mask;
                SwapLongBlock(packet.payload,1);
                do_xl3_cmd(&packet,crate_num);
                xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0 | (crate_num << FEC_CSR_CRATE_OFFSET),&result,crate_num);
            }
        }

        // do one more soft gt and now check that everything looks ok
        send_softgt();

        total_pulses += numgt+1;
        if (j == 13)
            total_pulses++; // rollover bug

        for (i=0;i<16;i++){
            if ((slot_mask & (0x1<<i)) && (max_errors[i] == 0)){
                select_reg = FEC_SEL*i;
                xl3_rw(FIFO_DIFF_PTR_R + select_reg + READ_REG,0x0,&result,crate_num);
                if ((result & 0x000FFFFF) != 3*num_chans){
                   printsend("Not enough bundles slot %d, expected %d, found %u.\n",i,3*num_chans,result&0x000FFFFF);
                    sprintf(error_history[i]+strlen(error_history[i]),"Not enough bundles slot %d, expected %d, found %u.\n",i,3*num_chans,result&0x000FFFFF);
                    missing_bundles[i] = 1;
                }

                // read out one bundle for each channel (hopefully)
                badchanmask = chan_mask;
                for (k=0;k<((result&0x000FFFFF)/3);k++){
                    xl3_rw(READ_MEM + select_reg,0x0,&bundles[0],crate_num);
                    xl3_rw(READ_MEM + select_reg,0x0,&bundles[1],crate_num);
                    xl3_rw(READ_MEM + select_reg,0x0,&bundles[2],crate_num);

                    crate_id = bundles[0]>>21 & 0x0000001F;
                    slot_id = bundles[0]>>26 & 0x0000000F;
                    chan_id = bundles[0]>>16 & 0x0000001F;
                    nc_id = bundles[1]>>29 & 0x1;
                    gt16_id = bundles[0] & 0x0000FFFF;
                    gt8_id = ((bundles[2] >> 24) & 0x000000F0)|((bundles[2] >> 12) & 0x0000000F);

                    badchanmask &= ~(0x1<<chan_id);

                    if (crate_id != crate_num){
                       printsend("Crate wrong for slot %d, chan %u. Read %u, expected %d\n",i,chan_id,crate_id,crate_num);
                        sprintf(error_history[i]+strlen(error_history[i]),"Crate wrong for slot %d, chan %u. Read %u, expected %d\n",i,chan_id,crate_id,crate_num);
                        chan_errors[i][chan_id] = 1;
                    }
                    if (slot_id != i){
                       printsend("Slot wrong for chan %u. Read %u, expected %d\n",chan_id,slot_id,i);
                        sprintf(error_history[i]+strlen(error_history[i]),"Slot wrong for chan %u. Read %u, expected %d\n",chan_id,slot_id,i);
                        chan_errors[i][chan_id] = 1;
                    }
                    if (nc_id != 0x0){
                       printsend("NC wrong for slot %d, chan %u. Read %u, expected %d\n",i,chan_id,nc_id,0);
                        sprintf(error_history[i]+strlen(error_history[i]),"NC wrong for slot %d, chan %u. Read %u, expected %d\n",i,chan_id,nc_id,0);
                        chan_errors[i][chan_id] = 1;
                    }
                    if ((gt16_id + (65536*gt8_id)) != total_pulses){
                       printsend("bad gtid slot %d, chan %d. Read %u instead of %d. (%08x %08x %08x)\n",i,chan_id,gt16_id+(65536*gt8_id),total_pulses,bundles[0],bundles[1],bundles[2]);
                        sprintf(error_history[i]+strlen(error_history[i]),"bad gtid slot %d, chan %d. Read %u instead of %d. (%08x %08x %08x)\n",i,chan_id,gt16_id+(65536*gt8_id),total_pulses,bundles[0],bundles[1],bundles[2]);
                        chan_errors[i][chan_id] = 1;
                    }
                    if (es16 != 0x0 && j >= 13){
                       printsend("Synclear Error slot %d, chan %u.\n",i,chan_id);
                        sprintf(error_history[i]+strlen(error_history[i]),"Synclear Error slot %d, chan %u.\n",i,chan_id);
                        chan_errors[i][chan_id] = 1;
                    }
                }
                for (k=0;k<32;k++){
                    if ((0x1<<k) & badchanmask){
                       printsend("No bundle found for channel %d.\n",k);
                        sprintf(error_history[i]+strlen(error_history[i]),"No bundle found for channel %d.\n",k);
                        chan_errors[i][k] = 1;
                    }
                }
            }
        }
        for (i=0;i<16;i++){
            if ((strlen(error_history[i]) > 25000) && (max_errors[i] == 0)){
               printsend("too many errors slot %d. Skipping that slot\n",i);
                max_errors[i] = 1;
            }
        }

       printsend("%d pulses\n",total_pulses);
        for (i=0;i<16;i++)
            sprintf(error_history[i]+strlen(error_history[i]),"%d pulses\n",total_pulses);
    }

    if (update_db){
       printsend("updating the database\n");
        int slot,passflag = 1;
        ;
        for (slot=0;slot<16;slot++){
            if ((0x1<<slot) & slot_mask){
               printsend("updating slot %d\n",slot);
                JsonNode *newdoc = json_mkobject();
                json_append_member(newdoc,"type",json_mkstring("cgt_test"));
                json_append_member(newdoc,"missing_bundles",json_mknumber((double)missing_bundles[slot]));
                if (missing_bundles[slot] > 0)
                    passflag = 0;
                JsonNode *chan_errs = json_mkarray();
                for (i=0;i<32;i++){
                    json_append_element(chan_errs,json_mknumber((double)chan_errors[slot][i]));
                    if (chan_errors[slot][i] > 0)
                        passflag = 0;
                }
                json_append_member(newdoc,"channel_errors",chan_errs);
                json_append_member(newdoc,"printout",json_mkstring(error_history[slot]));
                if (passflag == 1){
                    json_append_member(newdoc,"pass",json_mkstring("yes"));
                }else{
                    json_append_member(newdoc,"pass",json_mkstring("no"));
                }
                if (final_test)
                    json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[slot]));	
                post_debug_doc(crate_num,slot,newdoc);
                json_delete(newdoc); // only delete the head node
            }
        }
    }

    printsend("***Ending GT increment Test***\n");
    return 0;
}






int fifo_test(char* buffer)
{
    int i,j;
    uint32_t result,select_reg;
    uint32_t remainder, diff, write, read;
    int gtstofire;
    uint32_t GTs1[16],GTs2[16];
    uint32_t bundle[12];
    uint32_t *readout_data;
    int busstop;

    int crate_num;
    uint16_t slot_mask;

    crate_num = 2;
    slot_mask = 0x1;

    int update_db = 0;
    int final_test = 0;
    char ft_ids[16][50];
    char error_history[16][50000];
    int slot_errors[16];

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
                printsend("Usage: fifo_test -c [crate number] -s [slot mask (hex)]"
                        " -d (update db)\n");
                return -1;
            }
        }
        words = strtok(NULL, " ");
    }

    for(i=0;i<16;i++){
        slot_errors[i] = 0;
        sprintf(error_history[i],"\0");
    }

    setup_softgt(crate_num);

    for (i=0;i<16;i++){
        if ((0x1<<i) & slot_mask){

            // reset FEC
            select_reg = FEC_SEL*i;
            xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG, 0xf, &result,crate_num);
            xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG, 0x0 | (crate_num << FEC_CSR_CRATE_OFFSET), &result,crate_num);
        }
    }

    // mask in one channel on each fec
    set_crate_pedestals(crate_num,slot_mask,0x1);

    for (i=0;i<16;i++){
        if ((0x1<<i) & slot_mask){
            select_reg = FEC_SEL*i;

            // check initial conditions
            checkfifo(&diff,crate_num,select_reg,error_history[i]);

            //get initial gt count
            get_gt_count(&GTs1[i]);
        }
    }

    // now pulse the soft gts
    gtstofire = (0xFFFFF-32)/3;
   printsend("Now firing %d soft gts.\n",gtstofire);
    int gtcount = 0;
    i = 14;
    while (gtcount < gtstofire){
        if (gtstofire - gtcount > 5000){
            multi_softgt(5000);
            gtcount += 5000;
        }else{
            multi_softgt(gtstofire-gtcount);
            gtcount += gtstofire-gtcount;
        }
        if (gtcount%15000 == 0){
           printsend(".");
            fflush(stdout);
        }
    }
   printsend("\n");
   printsend("Number of GTs looped: %u\n",gtstofire);

    for (i=0;i<16;i++){
        if ((0x1<<i) & slot_mask){
            select_reg = FEC_SEL*i;
            //get gt count
            get_gt_count(&GTs2[i]);
           printsend("Slot %d - Number of GTs fired: %u\n",i,GTs2[i] - GTs1[i]);
           printsend("Slot %d - GT before: %u, after: %u\n",i,GTs1[i],GTs2[i]);
            sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Number of GTs fired: %u\n",i,GTs2[i] - GTs1[i]);
            sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - GT before: %u, after: %u\n",i,GTs1[i],GTs2[i]);

            checkfifo(&diff,crate_num,select_reg,error_history[i]);
            if (diff != 3*(GTs2[i]-GTs1[i])){
               printsend("Slot %d - Unexpected number of FIFO counts!\n",i);
               printsend("Slot %d - Based on MTCD GTs fired, should be 0x%05x (%u)\n",i,3*(GTs2[i]-GTs1[i]),3*(GTs2[i]-GTs1[i]));
               printsend("Slot %d - Based on times looped, 0x%05x (%u)\n",i,gtstofire*3,gtstofire*3);
                sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Unexpected number of FIFO counts!\n",i);
                sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Based on MTCD GTs fired, should be 0x%05x (%u)\n",i,3*(GTs2[i]-GTs1[i]),3*(GTs2[i]-GTs1[i]));
                sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Based on times looped, 0x%05x (%u)\n",i,gtstofire*3,gtstofire*3);
            }

            // turn off all but one slot at a time
            set_crate_pedestals(crate_num,0x1<<i,0x1);

            // now pulse the last soft GTs to fill memory
            remainder =  diff / 3;
           printsend("Slot %d - Now firing %d more soft gts\n",i,remainder);
            sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Now firing %d more soft gts\n",i,remainder);
            multi_softgt(remainder);

            checkfifo(&diff,crate_num,select_reg,error_history[i]);

            // now read out bundles
            for (j=0;j<12;j++){
                xl3_rw(READ_MEM + select_reg,0x0,&bundle[j],crate_num);
            }
           printsend("Slot %d - Read out %d longwords (%d bundles)\n",i,12,12/3);
            sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Read out %d longwords (%d bundles)\n",i,12,12/3);

            checkfifo(&diff,crate_num,select_reg,error_history[i]);
            remainder = diff / 3;
            dump_pmt_verbose(12/3,bundle,error_history[i]);

            // check overflow behavior
           printsend("Slot %d - Now overfill FEC (firing %d more soft GTs)\n",i,remainder+3);
            sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Now overfill FEC (firing %d more soft GTs)\n",i,remainder+3);
            multi_softgt(remainder+3);

            checkfifo(&diff,crate_num,select_reg,error_history[i]);
            uint32_t busy_bits,test_id;
            xl3_rw(CMOS_BUSY_BIT(0) + READ_REG + select_reg,0x0,&busy_bits,crate_num);
           printsend("Should see %d cmos busy bits set. Busy bits are -> 0x%04x\n",3,busy_bits & 0x0000FFFF);
           printsend("(Note that there might be one less than expected as it might be caught up in sequencing.)\n");
            sprintf(error_history[i]+strlen(error_history[i]),"Should see %d cmos busy bits set. Busy bits are -> 0x%04x\n",3,busy_bits & 0x0000FFFF);
            sprintf(error_history[i]+strlen(error_history[i]),"(Note that there might be one less than expected as it might be caught up in sequencing.)\n");
            xl3_rw(CMOS_INTERN_TEST(0) + READ_REG + select_reg,0x0,&test_id,crate_num);
           printsend("See if we can read out test reg: 0x%08x\n",test_id);
            sprintf(error_history[i]+strlen(error_history[i]),"See if we can read out test reg: 0x%08x\n",test_id);

            // now read out bundles
            for (j=0;j<12;j++){
                xl3_rw(READ_MEM + select_reg,0x0,&bundle[j],crate_num);
            }
           printsend("Slot %d - Read out %d longwords (%d bundles). Should have cleared all busy bits\n",i,12,12/3);
           printsend(error_history[i]+strlen(error_history[i]),"Slot %d - Read out %d longwords (%d bundles). Should have cleared all busy bits\n",i,12,12/3);
            dump_pmt_verbose(12/3,bundle,error_history[i]);
            checkfifo(&diff,crate_num,select_reg,error_history[i]);
            xl3_rw(CMOS_BUSY_BIT(0) + READ_REG + select_reg,0x0,&busy_bits,crate_num);
           printsend("Should see %d cmos busy bits set. Busy bits are -> 0x%04x\n",0,busy_bits & 0x0000FFFF);
            sprintf(error_history[i]+strlen(error_history[i]),"Should see %d cmos busy bits set. Busy bits are -> 0x%04x\n",0,busy_bits & 0x0000FFFF);

            // read out data and check the stuff around the wrap of the write pointer
            j = 30;
           printsend("Slot %d - Dumping all but the last %d events.\n",i,j);
            sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Dumping all but the last %d events.\n",i,j);
            readout_data = (uint32_t *) malloc( 0x000FFFFF * sizeof(uint32_t));
            read_pmt(crate_num, i, 0x000FFFFF/3-j-2,readout_data);

            checkfifo(&diff,crate_num,select_reg,error_history[i]);
            j = (0x000FFFFF-diff)/3;

           printsend("Slot %d - Dumping last %d events!\n",i,j);
            sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Dumping last %d events!\n",i,j);
            read_pmt(crate_num, i, j, readout_data);
            dump_pmt_verbose(j, readout_data,error_history[i]);
            checkfifo(&diff,crate_num,select_reg,error_history[i]);
           printsend("Slot %d - Trying to read past the end... should get %d bus errors\n",i,12);
            sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Trying to read past the end... should get %d bus errors\n",i,12);
            busstop = 0;
            for (j=0;j<12;j++){
                busstop += xl3_rw(READ_MEM + select_reg,0x0,&bundle[j],crate_num);
            }
            if (busstop){
               printsend("Slot %d - Got expected bus errors (%d).\n",i,busstop);
                sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Got expected bus errors (%d).\n",i,busstop);
            }else{
               printsend("Slot %d - Error! Read past end!\n",i);
                sprintf(error_history[i]+strlen(error_history[i]),"Slot %d - Error! Read past end!\n",i);
                slot_errors[i] = 1;
            }
            deselect_fecs(crate_num);
           printsend("Finished Slot %d\n",i);
            sprintf(error_history[i]+strlen(error_history[i]),"Finished Slot %d\n",i);
           printsend("**************************************************\n");
            sprintf(error_history[i]+strlen(error_history[i]),"**************************************************\n");

        } // end if slot mask
    } // end loop over slot

    if (update_db){
       printsend("updating the database\n");
        int slot;
        ;
        for (slot=0;slot<16;slot++){
            if ((0x1<<slot) & slot_mask){
               printsend("updating slot %d\n",slot);
                JsonNode *newdoc = json_mkobject();
                json_append_member(newdoc,"type",json_mkstring("fifo_test"));
                json_append_member(newdoc,"printout",json_mkstring(error_history[slot]));
                if (slot_errors[slot] == 0){
                    json_append_member(newdoc,"pass",json_mkstring("yes"));
                }else{
                    json_append_member(newdoc,"pass",json_mkstring("no"));
                }
                if (final_test)
                    json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[slot]));	
                post_debug_doc(crate_num,slot,newdoc);
                json_delete(newdoc);
            }
        }
    }


    return 0;
}







int mb_stability_test(char* buffer)
{
    uint32_t crate_num, slot_mask;
    uint32_t chan_mask_rand[4];
    uint32_t pmtword[3];
    uint32_t crate,slot,chan,nc_cc;
    uint32_t gt8,gt16,gtword;
    uint32_t cmos_es16, cgt_es16, cgt_es8;
    uint32_t fec_diff;
    uint32_t nfire,nfire_16,nfire_24,nfire_gtid;
    int num_chan, rd, numPrint;
    int numPedestals;
    int i,j,k;

    char temp_msg[5000];
    char error_history[16][50000];
    int slot_errors[16];

    uint32_t select_reg, result;
    int update_db = 0;
    int final_test = 0;
    char ft_ids[16][50];

    crate_num = 2;
    slot_mask = 0x1;
    numPedestals = 1000;

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
                numPedestals = atoi(words2);
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
                printsend("Usage: mb_stability_test -c [crate number] -s [slot mask (hex)]"
                        " -n [num pedestals] -d (update db)\n");
                return -1;
            }
        }
        words = strtok(NULL, " ");
    }

    //setup mtc
    setup_softgt(crate_num);
    setup_crate(crate_num,slot_mask);

    printsend("** Starting MB+DB stability Test **\n"
              "Crate number: %hu\n"
              "Slot mask: %08x \n",crate_num,slot_mask);
    rd = 0;
    num_chan = 8;
    nfire_16 = 0;
    nfire_24 = 0;
    numPrint = 10;
    for (i=0;i<16;i++){
        sprintf(error_history[i],"\0");
        slot_errors[i] = 0;
    }
    chan_mask_rand[0] = 0x11111111;
    chan_mask_rand[1] = 0x22222222;
    chan_mask_rand[2] = 0x44444444;
    chan_mask_rand[3] = 0x88888888;
   printsend("Channel mask used: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
            chan_mask_rand[0],chan_mask_rand[1],chan_mask_rand[2],chan_mask_rand[3]);

   printsend("going to fire %d times.\n",numPedestals);

    for (nfire=1;nfire<numPedestals;nfire++){
        nfire_16++;
        if (nfire_16 == 65535){
            nfire_16 = 0;
            nfire_24++;
        }
        nfire_gtid = nfire_24*0x10000 + nfire_16;

        // for selected slots, set semi-random pattern
        for (i=0;i<16;i++){
            if (((0x1<<i)& slot_mask) && (slot_errors[i] == 0)){
                select_reg = FEC_SEL*i;
                //enable pedestals
                xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG, chan_mask_rand[rd], &result,crate_num);
            }
        }
        deselect_fecs(crate_num);
        rd = (rd+1)%4;

        // fire pulser once
        if (nfire == numPrint){
           printsend("Pulser fired %u times.\n",nfire);
            for (i=0;i<16;i++){
                if (((0x1<<i) & slot_mask) && (slot_errors[i] == 0)){
                    sprintf(error_history[i]+strlen(error_history[i]),"Pulser fired %u times.\n",nfire);
                }
            }
            numPrint+=10;
        }
        usleep(1);
        send_softgt();
        usleep(1);

        for (j=0;j<16;j++){
            if (((0x1<<j) & slot_mask) && (slot_errors[j] == 0)){
                select_reg = FEC_SEL*j;

                // check fifo diff pointer
                xl3_rw(FIFO_DIFF_PTR_R + select_reg + READ_REG,0x0,&fec_diff,crate_num);
                fec_diff &= 0x000FFFFF;
                if (fec_diff != num_chan*3){
                    sprintf(temp_msg,">>>Error, fec_diff = %d, expected %d\n",fec_diff,num_chan*3);
                    sprintf(temp_msg+strlen(temp_msg),">>>testing crate %d, slot %d\n",crate_num,j);
                    sprintf(temp_msg+strlen(temp_msg),">>>stopping at pulser iteration %u\n",nfire);
                    sprintf(error_history[j]+strlen(error_history[j]),"%s",temp_msg);
                   printsend("%s",temp_msg);
                    slot_errors[j] = 1 ;
                }
            }
        }

        for (j=0;j<16;j++){
            if (((0x1<<j) & slot_mask) && (slot_errors[j] == 0)){
                select_reg = FEC_SEL*j;

                // readout loop, check fifo again while reading out
                int iter = 0;
                while(3 <= fec_diff){
                    iter++;
                    if (iter > num_chan*3){
                        sprintf(temp_msg,">>>Error, number of FEC reads exceeds %d, aborting!\n",num_chan*3);
                        sprintf(temp_msg+strlen(temp_msg),">>>testing crate %d, slot %d\n",crate_num,j);
                        sprintf(temp_msg+strlen(temp_msg),">>>stopping at pulser iteration %u\n",nfire);
                        sprintf(error_history[j]+strlen(error_history[j]),"%s",temp_msg);
                       printsend("%s",temp_msg);
                        slot_errors[j] = 1 ;
                        break;
                    }

                    //read out memory
                    xl3_rw(READ_MEM + select_reg,0x0,pmtword,crate_num);
                    xl3_rw(READ_MEM + select_reg,0x0,pmtword+1,crate_num);
                    xl3_rw(READ_MEM + select_reg,0x0,pmtword+2,crate_num);
                    xl3_rw(FIFO_DIFF_PTR_R + select_reg + READ_REG,0x0,&fec_diff,crate_num);
                    fec_diff &= 0x000FFFFF;

                    crate = (uint32_t) UNPK_CRATE_ID(pmtword);
                    slot = (uint32_t) UNPK_BOARD_ID(pmtword);
                    chan = (uint32_t) UNPK_CHANNEL_ID(pmtword);
                    gt8 = (uint32_t) UNPK_FEC_GT8_ID(pmtword);
                    gt16 = (uint32_t) UNPK_FEC_GT16_ID(pmtword);
                    cmos_es16 = (uint32_t) UNPK_CMOS_ES_16(pmtword);
                    cgt_es16 = (uint32_t) UNPK_CGT_ES_16(pmtword);
                    cgt_es8 = (uint32_t) UNPK_CGT_ES_24(pmtword);
                    nc_cc = (uint32_t) UNPK_NC_CC(pmtword);

                    // check crate, slot, nc_cc
                    if ((crate != crate_num) || (slot != j) || (nc_cc != 0)){
                        sprintf(temp_msg,"***************************************\n");
                        sprintf(temp_msg+strlen(temp_msg),"Crate/slot or Nc_cc error. Pedestal iter: %u\n",nfire);
                        sprintf(temp_msg+strlen(temp_msg),"Expected crate,slot,nc_cc: %d %d %d\n",crate_num,j,0);
                        sprintf(temp_msg+strlen(temp_msg),"Found crate,slot,chan,nc_cc: %d %d %d %d\n",crate,slot,chan,nc_cc);
                        sprintf(temp_msg+strlen(temp_msg),"Bundle 0,1,2: 0x%08x 0x%08x 0x%08x\n",pmtword[0],pmtword[1],pmtword[2]);
                        sprintf(temp_msg+strlen(temp_msg),"***************************************\n");
                        sprintf(temp_msg+strlen(temp_msg),">>>Stopping at pulser iteration %u\n",nfire);
                        sprintf(error_history[j]+strlen(error_history[j]),"%s",temp_msg);
                       printsend("%s",temp_msg);
                        slot_errors[j] = 1;
                        break;
                    }

                    // check gt increment
                    gtword = gt8*0x10000 + gt16;
                    if (gtword != nfire_gtid){
                        sprintf(temp_msg,"***************************************\n");
                        sprintf(temp_msg+strlen(temp_msg),"GT8/16 error, expect GTID: %u \n",nfire_gtid);
                        sprintf(temp_msg+strlen(temp_msg),"Crate,slot,chan: %d %d %d\n",crate,slot,chan);
                        sprintf(temp_msg+strlen(temp_msg),"Bundle 0,1,2: 0x%08x 0x%08x 0x%08x\n",pmtword[0],pmtword[1],pmtword[2]);
                        sprintf(temp_msg+strlen(temp_msg),"Found gt8, gt16, gtword: %d %d %08x\n",gt8,gt16,gtword);
                        sprintf(temp_msg+strlen(temp_msg),"***************************************\n");
                        sprintf(temp_msg+strlen(temp_msg),">>>Stopping at pulser iteration %u\n",nfire);
                        sprintf(error_history[j]+strlen(error_history[j]),"%s",temp_msg);
                       printsend("%s",temp_msg);
                        slot_errors[j] = 1;
                        break;
                    }


                    // check synclear bits
                    if ((cmos_es16 == 1) || (cgt_es16 == 1) || (cgt_es8 == 1)){
                        if (gt8 != 0) {
                            sprintf(temp_msg,"***************************************\n");
                            sprintf(temp_msg+strlen(temp_msg),"Synclear error, GTID: %u\n",nfire_gtid);
                            sprintf(temp_msg+strlen(temp_msg),"crate, slot, chan: %d %d %d\n",crate,slot,chan);
                            sprintf(temp_msg+strlen(temp_msg),"Bundle 0,1,2: 0x%08x 0x%08x 0x%08x\n",pmtword[0],pmtword[1],pmtword[2]);
                            sprintf(temp_msg+strlen(temp_msg),"Found cmos_es16,cgt_es16,cgt_es8: %d %d %d\n",cmos_es16,cgt_es16,cgt_es8);
                            sprintf(temp_msg+strlen(temp_msg),"***************************************\n");
                            sprintf(temp_msg+strlen(temp_msg),">>>Stopping at pulser iteration %u\n",nfire);
                            sprintf(error_history[j]+strlen(error_history[j]),"%s",temp_msg);
                           printsend("%s",temp_msg);
                            slot_errors[j] = 1;
                            break;
                        }
                    }

                } // while reading out

                deselect_fecs(crate_num);

            } //end if slot mask
        } // end loop over slots
    } // loop over nfire

    if (update_db){
       printsend("updating the database\n");
        int slot;
        ;
        for (slot=0;slot<16;slot++){
            if ((0x1<<slot) & slot_mask){
               printsend("updating slot %d\n",slot);
                JsonNode *newdoc = json_mkobject();
                json_append_member(newdoc,"type",json_mkstring("mb_stability_test"));
                json_append_member(newdoc,"printout",json_mkstring(error_history[slot]));
                if (slot_errors[slot] == 0){
                    json_append_member(newdoc,"pass",json_mkstring("yes"));
                }else{
                    json_append_member(newdoc,"pass",json_mkstring("no"));
                }
                if (final_test)
                    json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[slot]));	
                post_debug_doc(crate_num,slot,newdoc);
                json_delete(newdoc);
            }
        }
    }


   printsend("***** Ending MB stability test *****\n");
    return 0;
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
        if ((result != 0x0001abcd) && (((0x1<<i) & slot_mask) == 0x0)){
            // this slot probably empty
            continue;
        }

        // reset fifo
        xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0xFFFFFFFF,&result,cn);
        // mask in crate_address and fifo reset
        xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0 | (cn << FEC_CSR_CRATE_OFFSET) | 0x6,&result,cn);
        xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0 | (cn << FEC_CSR_CRATE_OFFSET),&result,cn);
        xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0x0,&result,cn);
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
    set_gt_crate_mask(mtc_crate_mask);
    set_ped_crate_mask(mtc_crate_mask);
    //set_gt_crate_mask(MASKALL);
    //set_ped_crate_mask(MASKALL);
    set_gt_crate_mask(MSK_TUB);
    set_ped_crate_mask(MSK_TUB);
    return 0;
}

static void checkfifo(uint32_t *thediff, int crate_num, uint32_t select_reg, char *msg_buff)
{
    uint32_t diff, read, write;
    float remainder;
    char msg[5000];
    xl3_rw(FIFO_DIFF_PTR_R + select_reg + READ_REG,0x0,&diff,crate_num);
    diff &= 0x000FFFFF;
    xl3_rw(FIFO_READ_PTR_R + select_reg + READ_REG,0x0,&read,crate_num);
    read &= 0x000FFFFF;
    xl3_rw(FIFO_WRITE_PTR_R + select_reg + READ_REG,0x0,&write,crate_num);
    write &= 0x000FFFFF;
    remainder = (float) 0x000FFFFF - (float) diff;

    sprintf(msg,"**********************************************\n");
    sprintf(msg+strlen(msg),"Fifo diff ptr is %05x\n",diff);
    sprintf(msg+strlen(msg),"Fifo write ptr is %05x\n",write);
    sprintf(msg+strlen(msg),"Fifo read ptr is %05x\n",read);
    sprintf(msg+strlen(msg),"Left over space is %2.1f (%2.1f bundles)\n",remainder,remainder/3.0);
    sprintf(msg+strlen(msg),"Total events in memory is %2.1f.\n",(float) diff / 3.0);

    sprintf(msg_buff+strlen(msg_buff),"%s",msg);
   printsend("%s",msg);
    *thediff = (uint32_t) remainder;
}

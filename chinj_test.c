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

#define HV_BIT_COUNT  40
#define HV_HVREF_DAC  136
#define HV_CSR_CLK    0x1
#define HV_CSR_DATIN  0x2
#define HV_CSR_LOAD   0x4
#define HV_CSR_DATOUT 0x8

int setup_chinj(int crate, uint16_t slot_mask, uint32_t default_ch_mask, uint16_t dacvalue);

int chinj_scan(char *buffer)
{
    if (sbc_is_connected == 0){
        sprintf(psb,"SBC not connected.\n");
        print_send(psb, view_fdset);
        return -1;
    }
    int i,j,k;
    int count,crateID,ch,cell,num_events;
    uint32_t slot_mask = 0x2000;
    int update_db = 0;
    int final_test = 0;
    char ft_ids[16][50];
    int crate = 2;
    u_long pattern = 0xFFFFFFFF;
    float frequency = 1000.0, wtime;
    uint32_t gtdelay = GT_DELAY;
    uint16_t ped_width = PED_WIDTH;
    uint16_t dacvalue = 0,q_select = 0, ped_on = 0;
    int num_pedestals = 50;
    float chinj_lower = 500, chinj_upper = 800;
    uint32_t *pmt_buffer, *pmt_iter;
    struct pedestal *ped;
    time_t now;
    uint32_t result;
    uint32_t select_reg;
    int errors;
    uint32_t default_ch_mask;
    int chinj_err[16];

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
            }else if (words[1] == 'd'){
                update_db = 1;
            }else if (words[1] == 'f'){
                words2 = strtok(NULL, " ");
                frequency =  atof(words2);
            }else if (words[1] == 'p'){
                words2 = strtok(NULL, " ");
                pattern = strtoul(words2, (char **) NULL, 16);
            }else if (words[1] == 't'){
                words2 = strtok(NULL, " ");
                gtdelay = atoi(words2);
            }else if (words[1] == 'w'){
                words2 = strtok(NULL, " ");
                ped_width = atoi(words2);
            }else if (words[1] == 'n'){
                words2 = strtok(NULL, " ");
                num_pedestals = atoi(words2);
            }else if (words[1] == 'l'){
                words2 = strtok(NULL, " ");
                chinj_lower = (float) atoi(words2);
            }else if (words[1] == 'u'){
                words2 = strtok(NULL, " ");
                chinj_upper = (float) atoi(words2);
            }else if (words[1] == 'a'){
                words2 = strtok(NULL, " ");
                q_select = (uint16_t) atoi(words2);
            }else if (words[1] == 'e'){
                words2 = strtok(NULL, " ");
                ped_on = (uint16_t) atoi(words2);
            }else if (words[1] == '#'){
                final_test = 1;
                for (i=0;i<16;i++){
                    if ((0x1<<i) & slot_mask){
                        words2 = strtok(NULL, " ");
                        sprintf(ft_ids[i],"%s",words2);
                    }
                }
            }else if (words[1] == 'h'){
               printsend("usage: chinj_test -c [crate num] -s [slot mask] + other stuff\n");
                return -1;
            }
        }
        words = strtok(NULL, " ");
    }

    (void) time(&now);
    sprintf(psb,"Charge Injection Run Setup\n");
    sprintf(psb+strlen(psb),"-------------------------------------------\n");
    sprintf(psb+strlen(psb),"Crate:		    %2d\n",crate);
    sprintf(psb+strlen(psb),"Slot Mask:		    0x%4hx\n",slot_mask);
    sprintf(psb+strlen(psb),"Pedestal Mask:	    0x%08lx\n",pattern);
    sprintf(psb+strlen(psb),"GT delay (ns):	    %3hu\n", gtdelay);
    sprintf(psb+strlen(psb),"Pedestal Width (ns):    %2d\n",ped_width);
    sprintf(psb+strlen(psb),"Pulser Frequency (Hz):  %3.0f\n",frequency);
    //sprintf(psb+strlen(psb),"\nRun started at %.24s\n",ctime(&now));
    print_send(psb, view_fdset);

    float qhls[16*32*2][26];
    float qhss[16*32*2][26];
    float qlxs[16*32*2][26];
    float tacs[16*32*2][26];
    int scan_errors[16*32*2][26];

    for (i=0;i<16;i++){
        chinj_err[i] = 0;
    }
    int dac_iter;

    for (dac_iter=0;dac_iter<26;dac_iter++){

        dacvalue = dac_iter*10;

        pmt_buffer = (uint32_t *) malloc( TWOTWENTY*sizeof(u_long));
        ped = (struct pedestal *) malloc( 32 * sizeof(struct pedestal));

        int slot_iter;
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

        //print_send("Reset FECs\n", view_fdset);

        errors = 0;
        errors += fec_load_crateadd(crate, slot_mask);
        if (ped_on == 1){
            //printsend("not enabling pedestals.\n");
        }else if (ped_on == 0){
            //printsend("enabling pedestals.\n");
            errors += set_crate_pedestals(crate, slot_mask, pattern);
        }
        deselect_fecs(crate);

        if (errors){
           printsend("error setting up FEC crate for pedestals. Exiting.\n");
            free(pmt_buffer);
            free(ped);
            return 1;
        }

        //setup charge injection
        default_ch_mask = pattern;
        setup_chinj(crate,slot_mask,default_ch_mask,dacvalue);

        errors = setup_pedestals(0,ped_width,gtdelay,GT_FINE_DELAY);
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

        // send the softgts
        multi_softgt(num_pedestals*NUM_CELLS);

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
                    //print_send(psb, view_fdset);
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

                sprintf(psb,"########################################################\n");
                sprintf(psb+strlen(psb),"Slot (%2d)\n", slot_iter);
                sprintf(psb+strlen(psb),"########################################################\n");
                //print_send(psb, view_fdset);
                for (i = 0; i<32; i++){
                    //printsend("Ch Cell  #   Qhl         Qhs         Qlx         TAC\n");
                    for (j=0;j<16;j++){
                        if (j == 0){
                            scan_errors[slot_iter*32+i*2][dac_iter] = 0;
                            qhls[slot_iter*32+i*2][dac_iter] = ped[i].thiscell[j].qhlbar;
                            qhss[slot_iter*32+i*2][dac_iter] = ped[i].thiscell[j].qhsbar;
                            qlxs[slot_iter*32+i*2][dac_iter] = ped[i].thiscell[j].qlxbar;
                            tacs[slot_iter*32+i*2][dac_iter] = ped[i].thiscell[j].tacbar;
                        }
                        if (j == 1){
                            scan_errors[slot_iter*32+i*2][dac_iter] = 0;
                            qhls[slot_iter*32+i*2+1][dac_iter] = ped[i].thiscell[j].qhlbar;
                            qhss[slot_iter*32+i*2+1][dac_iter] = ped[i].thiscell[j].qhsbar;
                            qlxs[slot_iter*32+i*2+1][dac_iter] = ped[i].thiscell[j].qlxbar;
                            tacs[slot_iter*32+i*2+1][dac_iter] = ped[i].thiscell[j].tacbar;
                        }
                        if (q_select == 0){
                            if (ped[i].thiscell[j].qhlbar < chinj_lower ||
                                    ped[i].thiscell[j].qhlbar > chinj_upper) {
                                chinj_err[slot_iter]++;
                                //printsend(">>>>>Qhl Extreme Value<<<<<\n");
                                if (j%2 == 0)
                                    scan_errors[slot_iter*32+i*2][dac_iter]++;
                                else
                                    scan_errors[slot_iter*32+i*2+1][dac_iter]++;
                            }
                        }
                        else if (q_select == 1){
                            if (ped[i].thiscell[j].qhsbar < chinj_lower ||
                                    ped[i].thiscell[j].qhsbar > chinj_upper) {
                                chinj_err[slot_iter]++;
                                //printsend(">>>>>Qhs Extreme Value<<<<<\n");
                                if (j%2 == 0)
                                    scan_errors[slot_iter*32+i*2][dac_iter]++;
                                else
                                    scan_errors[slot_iter*32+i*2+1][dac_iter]++;
                            }
                        }
                        else if (q_select == 2){
                            if (ped[i].thiscell[j].qlxbar < chinj_lower ||
                                    ped[i].thiscell[j].qlxbar > chinj_upper) {
                                chinj_err[slot_iter]++;
                                //printsend(">>>>>Qlx Extreme Value<<<<<\n");
                                if (j%2 == 0)
                                    scan_errors[slot_iter*32+i*2][dac_iter]++;
                                else
                                    scan_errors[slot_iter*32+i*2+1][dac_iter]++;
                            }
                        }
                        sprintf(psb,"%2d %3d %4d %6.1f %4.1f %6.1f %4.1f %6.1f %4.1f %6.1f %4.1f\n",
                                i,j,ped[i].thiscell[j].per_cell,
                                ped[i].thiscell[j].qhlbar, ped[i].thiscell[j].qhlrms,
                                ped[i].thiscell[j].qhsbar, ped[i].thiscell[j].qhsrms,
                                ped[i].thiscell[j].qlxbar, ped[i].thiscell[j].qlxrms,
                                ped[i].thiscell[j].tacbar, ped[i].thiscell[j].tacrms);
                        //	print_send(psb, view_fdset);
                    }
                }

            } // end if slotmask
        } // end loop over slots

        /*
           if (q_select == 0){
          printsend("Qhl lower, Upper bounds = %f %f\n",chinj_lower,chinj_upper);
          printsend("Number of Qhl overflows = %d\n",chinj_err[slot_iter]);
           }
           else if (q_select == 1){
          printsend("Qhs lower, Upper bounds = %f %f\n",chinj_lower,chinj_upper);
          printsend("Number of Qhs overflows = %d\n",chinj_err[slot_iter]);
           }
           else if (q_select == 2){
          printsend("Qlx lower, Upper bounds = %f %f\n",chinj_lower,chinj_upper);
          printsend("Number of Qlx overflows = %d\n",chinj_err[slot_iter]);
           }
         */

        free(pmt_buffer);
        free(ped);

        //disable trigger enables
        unset_ped_crate_mask(MASKALL);
        unset_gt_crate_mask(MASKALL);

        //unset pedestalenable
        errors += set_crate_pedestals(crate, slot_mask, 0x0);

        deselect_fecs(crate);
    } // end loop over dacvalue


    // lets update this database
    if (update_db){
       printsend("updating the database\n");
        for (i=0;i<16;i++)
        {
            if ((0x1<<i) & slot_mask){
                JsonNode *newdoc = json_mkobject();
                JsonNode *qhl_even = json_mkarray();
                JsonNode *qhl_odd = json_mkarray();
                JsonNode *qhs_even = json_mkarray();
                JsonNode *qhs_odd = json_mkarray();
                JsonNode *qlx_even = json_mkarray();
                JsonNode *qlx_odd = json_mkarray();
                JsonNode *tac_even = json_mkarray();
                JsonNode *tac_odd = json_mkarray();
                JsonNode *error_even = json_mkarray();
                JsonNode *error_odd = json_mkarray();
                for (j=0;j<32;j++){
                    JsonNode *qhleventemp = json_mkarray();
                    JsonNode *qhloddtemp = json_mkarray();
                    JsonNode *qhseventemp = json_mkarray();
                    JsonNode *qhsoddtemp = json_mkarray();
                    JsonNode *qlxeventemp = json_mkarray();
                    JsonNode *qlxoddtemp = json_mkarray();
                    JsonNode *taceventemp = json_mkarray();
                    JsonNode *tacoddtemp = json_mkarray();
                    JsonNode *erroreventemp = json_mkarray();
                    JsonNode *erroroddtemp = json_mkarray();
                    for (k=0;k<26;k++){
                        json_append_element(qhleventemp,json_mknumber(qhls[i*32+j*2][k]));	
                        json_append_element(qhloddtemp,json_mknumber(qhls[i*32+j*2+1][k]));	
                        json_append_element(qhseventemp,json_mknumber(qhss[i*32+j*2][k]));	
                        json_append_element(qhsoddtemp,json_mknumber(qhss[i*32+j*2+1][k]));	
                        json_append_element(qlxeventemp,json_mknumber(qlxs[i*32+j*2][k]));	
                        json_append_element(qlxoddtemp,json_mknumber(qlxs[i*32+j*2+1][k]));	
                        json_append_element(taceventemp,json_mknumber(tacs[i*32+j*2][k]));	
                        json_append_element(tacoddtemp,json_mknumber(tacs[i*32+j*2+1][k]));	
                        json_append_element(erroreventemp,json_mknumber((double)scan_errors[i*32+j*2][k]));	
                        json_append_element(erroroddtemp,json_mknumber((double)scan_errors[i*32+j*2+1][k]));	
                    }
                    json_append_element(qhl_even,qhleventemp);
                    json_append_element(qhl_odd,qhloddtemp);
                    json_append_element(qhs_even,qhseventemp);
                    json_append_element(qhs_odd,qhsoddtemp);
                    json_append_element(qlx_even,qlxeventemp);
                    json_append_element(qlx_odd,qlxoddtemp);
                    json_append_element(tac_even,taceventemp);
                    json_append_element(tac_odd,tacoddtemp);
                    json_append_element(error_even,erroreventemp);
                    json_append_element(error_odd,erroroddtemp);
                }
                json_append_member(newdoc,"type",json_mkstring("chinj_scan"));
                json_append_member(newdoc,"QHL_even",qhl_even);
                json_append_member(newdoc,"QHL_odd",qhl_odd);
                json_append_member(newdoc,"QHS_even",qhs_even);
                json_append_member(newdoc,"QHS_odd",qhs_odd);
                json_append_member(newdoc,"QLX_even",qlx_even);
                json_append_member(newdoc,"QLX_odd",qlx_odd);
                json_append_member(newdoc,"TAC_even",tac_even);
                json_append_member(newdoc,"TAC_odd",tac_odd);
                json_append_member(newdoc,"errors_even",error_even);
                json_append_member(newdoc,"errors_odd",error_odd);
                if (chinj_err[i] == 0){
                    json_append_member(newdoc,"pass",json_mkstring("yes"));
                }else{
                    json_append_member(newdoc,"pass",json_mkstring("no"));
                }
                if (final_test){
                    json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[i]));	
                }
                post_debug_doc(crate,i,newdoc);
            }
        }
    }


    if (errors)
        sprintf(psb, "There were %d errors\n",errors);
    else
        sprintf(psb, "No errors seen\n");
    print_send(psb, view_fdset);
    print_send("*************************************\n",view_fdset);
    return errors;
}



int chinj_test(char *buffer)
{
    if (sbc_is_connected == 0){
        sprintf(psb,"SBC not connected.\n");
        print_send(psb, view_fdset);
        return -1;
    }
    int i,j;
    int count,crateID,ch,cell,num_events;
    uint32_t slot_mask = 0x2000;
    int update_db = 0;
    int crate = 2;
    u_long pattern = 0xFFFFFFFF;
    float frequency = 1000.0, wtime;
    uint32_t gtdelay = GT_DELAY;
    uint16_t ped_width = PED_WIDTH;
    uint16_t dacvalue = 0,q_select = 0, ped_on = 0;
    int num_pedestals = 50;
    float chinj_lower = 500, chinj_upper = 800;
    uint32_t *pmt_buffer, *pmt_iter;
    struct pedestal *ped;
    time_t now;
    uint32_t result;
    uint32_t select_reg;
    int errors;
    uint32_t default_ch_mask;
    int chinj_err = 0;

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
            }else if (words[1] == 'd'){
                update_db = 1;
            }else if (words[1] == 'f'){
                words2 = strtok(NULL, " ");
                frequency =  atof(words2);
            }else if (words[1] == 'p'){
                words2 = strtok(NULL, " ");
                pattern = strtoul(words2, (char **) NULL, 16);
            }else if (words[1] == 't'){
                words2 = strtok(NULL, " ");
                gtdelay = atoi(words2);
            }else if (words[1] == 'w'){
                words2 = strtok(NULL, " ");
                ped_width = atoi(words2);
            }else if (words[1] == 'v'){
                words2 = strtok(NULL, " ");
                dacvalue = atoi(words2);
            }else if (words[1] == 'n'){
                words2 = strtok(NULL, " ");
                num_pedestals = atoi(words2);
            }else if (words[1] == 'l'){
                words2 = strtok(NULL, " ");
                chinj_lower = (float) atoi(words2);
            }else if (words[1] == 'u'){
                words2 = strtok(NULL, " ");
                chinj_upper = (float) atoi(words2);
            }else if (words[1] == 'a'){
                words2 = strtok(NULL, " ");
                q_select = (uint16_t) atoi(words2);
            }else if (words[1] == 'e'){
                words2 = strtok(NULL, " ");
                ped_on = (uint16_t) atoi(words2);
            }else if (words[1] == 'h'){
               printsend("usage: chinj_test -c [crate num] -s [slot mask] + other stuff\n");
                return -1;
            }
        }
        words = strtok(NULL, " ");
    }

    (void) time(&now);
    sprintf(psb,"Charge Injection Run Setup\n");
    sprintf(psb+strlen(psb),"-------------------------------------------\n");
    sprintf(psb+strlen(psb),"Crate:		    %2d\n",crate);
    sprintf(psb+strlen(psb),"Slot Mask:		    0x%4hx\n",slot_mask);
    sprintf(psb+strlen(psb),"Pedestal Mask:	    0x%08lx\n",pattern);
    sprintf(psb+strlen(psb),"GT delay (ns):	    %3hu\n", gtdelay);
    sprintf(psb+strlen(psb),"Pedestal Width (ns):    %2d\n",ped_width);
    sprintf(psb+strlen(psb),"Pulser Frequency (Hz):  %3.0f\n",frequency);
    sprintf(psb+strlen(psb),"Chinj Dac Value (d):    %hu\n",dacvalue);
    //sprintf(psb+strlen(psb),"\nRun started at %.24s\n",ctime(&now));
    print_send(psb, view_fdset);

    wtime = (float) (num_pedestals-1) * NUM_CELLS / frequency; //FIXME hack for mtc latency

    int slot_iter;
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
    if (ped_on == 1){
       printsend("not enabling pedestals.\n");
    }else if (ped_on == 0){
       printsend("enabling pedestals.\n");
        errors += set_crate_pedestals(crate, slot_mask, pattern);
    }
    deselect_fecs(crate);

    if (errors){
       printsend("error setting up FEC crate for pedestals. Exiting.\n");
        return 1;
    }

    //setup charge injection
    default_ch_mask = pattern;
    setup_chinj(crate,slot_mask,default_ch_mask,dacvalue);

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

    //wait for pedestals to arrive
    wtime = (wtime * 1E6); // set this to usec
    usleep((u_int) wtime);

    disable_pulser();

    // LOOP OVER SLOTS
    for (slot_iter = 0; slot_iter < 16; slot_iter ++){
        if ((0x1<<slot_iter) & slot_mask){

            pmt_buffer = (uint32_t *) malloc( TWOTWENTY*sizeof(u_long));
            ped = (struct pedestal *) malloc( 32 * sizeof(struct pedestal));

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

            sprintf(psb,"########################################################\n");
            sprintf(psb+strlen(psb),"Slot (%2d)\n", slot_iter);
            sprintf(psb+strlen(psb),"########################################################\n");
            print_send(psb, view_fdset);
            for (i = 0; i<32; i++){
               printsend("Ch Cell  #   Qhl         Qhs         Qlx         TAC\n");
                for (j=0;j<16;j++){
                    if (q_select == 0){
                        if (ped[i].thiscell[j].qhlbar < chinj_lower ||
                                ped[i].thiscell[j].qhlbar > chinj_upper) {
                            chinj_err++;
                           printsend(">>>>>Qhl Extreme Value<<<<<\n");
                        }
                    }
                    else if (q_select == 1){
                        if (ped[i].thiscell[j].qhsbar < chinj_lower ||
                                ped[i].thiscell[j].qhsbar > chinj_upper) {
                            chinj_err++;
                           printsend(">>>>>Qhs Extreme Value<<<<<\n");
                        }
                    }
                    else if (q_select == 2){
                        if (ped[i].thiscell[j].qlxbar < chinj_lower ||
                                ped[i].thiscell[j].qlxbar > chinj_upper) {
                            chinj_err++;
                           printsend(">>>>>Qlx Extreme Value<<<<<\n");
                        }
                    }
                    sprintf(psb,"%2d %3d %4d %6.1f %4.1f %6.1f %4.1f %6.1f %4.1f %6.1f %4.1f\n",
                            i,j,ped[i].thiscell[j].per_cell,
                            ped[i].thiscell[j].qhlbar, ped[i].thiscell[j].qhlrms,
                            ped[i].thiscell[j].qhsbar, ped[i].thiscell[j].qhsrms,
                            ped[i].thiscell[j].qlxbar, ped[i].thiscell[j].qlxrms,
                            ped[i].thiscell[j].tacbar, ped[i].thiscell[j].tacrms);
                    print_send(psb, view_fdset);
                }
            }

            free(pmt_buffer);
            free(ped);

        } // end if slotmask
    } // end loop over slots

    if (q_select == 0){
       printsend("Qhl lower, Upper bounds = %f %f\n",chinj_lower,chinj_upper);
       printsend("Number of Qhl overflows = %d\n",chinj_err);
    }
    else if (q_select == 1){
       printsend("Qhs lower, Upper bounds = %f %f\n",chinj_lower,chinj_upper);
       printsend("Number of Qhs overflows = %d\n",chinj_err);
    }
    else if (q_select == 2){
       printsend("Qlx lower, Upper bounds = %f %f\n",chinj_lower,chinj_upper);
       printsend("Number of Qlx overflows = %d\n",chinj_err);
    }


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

// sets up charge injection by clocking in a 40 bit stream. The last 32 bits set the channels.
// We then set dac 136, the chinj dac
int setup_chinj(int crate, uint16_t slot_mask, uint32_t default_ch_mask, uint16_t dacvalue)
{
    int slot_iter,bit_iter;
    uint32_t amask,word;
    uint32_t select_reg;
    uint32_t result;
    int error;

    amask = default_ch_mask;

    for (slot_iter=0;slot_iter<16;slot_iter++){
        if ((0x1<<slot_iter) & slot_mask){
            select_reg = FEC_SEL*slot_iter;
            for (bit_iter = HV_BIT_COUNT;bit_iter>0;bit_iter--){
                if (bit_iter > 32){
                    word = 0x0;
                }else{
                    // set bit iff it is set in amask
                    word = ((0x1 << (bit_iter -1)) & amask) ? HV_CSR_DATIN : 0x0;
                }
                xl3_rw(FEC_HV_CSR_R + select_reg + WRITE_REG,word,&result,crate);
                word |= HV_CSR_CLK;
                xl3_rw(FEC_HV_CSR_R + select_reg + WRITE_REG,word,&result,crate);
            } // end loop over bits

            // now toggle HVLOAD
            xl3_rw(FEC_HV_CSR_R + select_reg + WRITE_REG,0x0,&result,crate);
            xl3_rw(FEC_HV_CSR_R + select_reg + WRITE_REG,HV_CSR_LOAD,&result,crate);

            // now set the dac
            error = loadsDac(HV_HVREF_DAC,dacvalue,crate,select_reg);
            if (error){
               printsend("setup_chinj: error loading charge injection dac\n");
            }
        } // end if slot_mask
    } // end loop over slots
    deselect_fecs(crate);
    return 0;
}

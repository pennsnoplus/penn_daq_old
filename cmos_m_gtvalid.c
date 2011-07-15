/*
 *cmos_m_gtvalid.c
 * Main program to determine cmos gtvalid timing.
 * Enable secondary current source and adjust bits
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "include/Record_Info.h"
#include "include/xl_regs.h"

#include "penn_daq.h"
#include "fec_util.h"
#include "mtc_util.h"
#include "net_util.h"


#define PED_WIDTH 25
#define GT_FINE_DEL 0
#define NGTVALID 20 // number of events per iteration
#define GTMAX 1000 // maximum GTVALID width
#define HOWMANY 350
#define TRUE 1
#define FALSE 0
#define GTPED_DELAY 20 // delay between GT and PED due to disc delay
#define TDELAY_EXTRA 0 // extra delay to make target and scope measured GTVALID width equal to each other

//initial values for TAC reference voltages
#define VMAX 203 // upper TAC rail
#define TACREF 72 // lower TAC rail
#define ISETM_START 147 // start value primary current source
#define ISETM 138 // primary current source
#define ISETA 70 // secondary current source, using 75 is aggressive


static int setup_crate(int cn, uint16_t slot_mask),
           setup_mtc(uint32_t freq, uint16_t crate_num);

int get_gtdelay(uint16_t crate_num, int wt, float *get_gtchan, uint16_t isetm0, uint16_t isetm1, uint32_t select_reg);

/*
 * We can change the delay of the GT on the MTCD. Then on the FECs, we can change the length
 * of the GTVALID window for one of the two tacs by setting the ISETM dac. Then channel by
 * channel we can change the twiddle bits to further refine the widths. In this code we find the
 * length of the GTVALID window by changing the delay until a channel no longer sees triggers.
 * We first go channel by channel and determine the delay with the maximum gtvalid width. If
 * we are given a cutoff GTVALID delay, we then go back and change the ISETM dac until the
 * maximum delay where the channels can still see the trigger is less than our cutoff. We
 * then go back and change the twiddle bits until each channel is just over the cutoff. 
 *
 */

int cmos_m_gtvalid(char *buffer)
{
    int freq,result,done, i,j,k,n,ic,ip,ot,wt,error;
    int slot,chan;
    uint16_t ncrates, db_new;
    uint16_t chan_max, chan_max_sec, chan_min;
    uint16_t isetm_new[2], iseta, tacbits[32], tacbits_new[2][32], gtflag[2][32];
    uint16_t isetm_save[2], tacbits_save[2][32], dac_isetm[2], dac_iseta[2], isetm_start[2];
    uint32_t nfifo, nget;
    uint16_t chan_max_set[2],chan_min_set[2],cmax[2],cmin[2];;
    float gtchan[32], gtchan_set[2][32];
    float gtdelay, gtdelta, gtstart, corr_gtdelta, gt_max;
    float gt_temp, gt_max_sec, gt_min, gt_other;
    float gt_max_set[2], gt_min_set[2], gt_start[2][32];
    float ratio, best[32], gmax[2],gmin[2];
    float a=10.1, b=5.1, twait;
    FILE *fp;

    XL3_Packet packet;
    uint32_t *pl;

    pl = (uint32_t *) packet.payload;
    //SNO_seterr_level(7,MTC_FAC);
    //SNO_seterr_level(7,DAQ_FAC);
    freq = 0;

    // DB initialization
    hware_vals_t theHWconf[16];
    int update_db = 0;
    int final_test = 0;
    char ft_ids[16][50];

    //read in from command line
    uint16_t cn = 2;
    uint16_t slot_mask = 0xFFFF;
    uint32_t chan_mask = 0xFFFFFFFF;
    float gt_cutoff = 0;
    int do_twiddle = 1;

    char *words,*words2;
    words = strtok(buffer, " ");
    while (words != NULL){
        if (words[0] == '-'){
            if (words[1] == 'c'){
                words2 = strtok(NULL, " ");
                cn = atoi(words2);
            }else if (words[1] == 'g'){
                words2 = strtok(NULL, " ");
                gt_cutoff = atof(words2);
            }else if (words[1] == 'l'){
                words2 = strtok(NULL, " ");
                chan_mask = strtoul(words2,(char **)NULL,16);
            }else if (words[1] == 'n'){
                do_twiddle = 0;
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
            }else if (words[1] == 's'){
                words2 = strtok(NULL, " ");
                slot_mask = strtoul(words2,(char**)NULL,16);
            }else if (words[1] == 'h'){
                sprintf(psb,"Usage: cmos_m_gtvalid -c [crate num] -s [slot mask (hex)]"
                        " -g [gt cutoff] -l [channel mask (hex)] -n (no twiddle) -d (write to db)\n");
                print_send(psb,view_fdset);
                return -1;
            }
        }
        words = strtok(NULL, " ");
    }



    //setup crate
    setup_crate(cn, slot_mask);
    prepare_mtc_pedestals(freq,PED_WIDTH,10,0);
    enable_pulser();
    unset_gt_crate_mask(MASKALL);
    unset_ped_crate_mask(MASKALL);
    //set_gt_crate_mask(0x1<<cn);
    //set_ped_crate_mask(0x1<<cn);
    set_gt_crate_mask(MASKALL);
    set_ped_crate_mask(MASKALL);




    ;

    //initialize some constants, DAC address
    dac_isetm[0] = 132;
    dac_isetm[1] = 131;
    dac_iseta[0] = 130;
    dac_iseta[1] = 129;

    uint32_t select_reg, temp;

    // select fec
    for (slot=0;slot<16;slot++){
        if (slot_mask & (0x1<<slot)){


            select_reg = FEC_SEL*slot;

            //select which tac to work on
            for (wt=0;wt<2;wt++){
                if (wt==0)
                    ot = 1;
                else
                    ot = 0;

                //initialize some stuff
                for (i=0;i<32;i++)
                    gtchan[i] = 9999;

                //only set DAC's if request setting GTVALID
                if (gt_cutoff != 0){
                    //set TAC reference voltages
                    error = loadsDac(134,(u_short)VMAX,cn,select_reg);
                    error+= loadsDac(133,(u_short)TACREF,cn,select_reg);
                    error+= loadsDac(131,(u_short)ISETM_START,cn,select_reg);
                    error+= loadsDac(132,(u_short)ISETM_START,cn,select_reg);

                    //enable TAC secondary current source and adjust bits for
                    //all channels, the reason for this is so we can turn bits
                    //off to shorten the TAC time
                    error+= loadsDac(129,(u_short)ISETA,cn,select_reg);
                    error+= loadsDac(130,(u_short)ISETA,cn,select_reg);
                    if (error != 0){
                        print_send("Error in setting up TAC voltages, exiting...\n",view_fdset);
                        return 1;
                    }
                   printsend("Dacs loaded.\n");
                    //load cmos shift register to enable twiddle bits
                    packet.cmdHeader.packet_type = LOADTACBITS_ID;
                    *pl = cn;
                    *(pl+1) = select_reg;
                    for (j=0;j<32;j++){
                        tacbits[j] = 0x77;
                        *(uint16_t *)(packet.payload+8+j*2) = tacbits[j];
                    }
                    SwapLongBlock(packet.payload,2);
                    SwapShortBlock(packet.payload+8,32);
                    do_xl3_cmd(&packet,cn);
                   printsend("Tac bits loaded.\n");
                }

                // some board level initialization
                isetm_new[0] = ISETM;
                isetm_new[1] = ISETM;
                for (k=0;k<32;k++){
                    if (do_twiddle){
                        tacbits_new[0][k] = 0x7;
                        tacbits_new[1][k] = 0x7; // enable all tac bits
                    }else{
                        tacbits_new[0][k] = 0x0;
                        tacbits_new[1][k] = 0x0; // disable all tac bits
                    }
                }
                isetm_start[0] = ISETM_START;
                isetm_start[1] = ISETM_START;

                //main loop over channels
                sprintf(psb,"Measuring GTVALID for crate,slot,TAC: %d %d %d\n",cn,slot,wt);
                print_send(psb,view_fdset);
                //loop over channels to measure initial GTVALID
                for (chan=0;chan<32;chan++){
                    if (chan_mask & (0x1<<chan)){
                        //set pedestal enable for channel
                        xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,1<<chan,&temp,cn);
                        error = get_gtdelay(cn, wt, &gt_temp,isetm_start[0],isetm_start[1],select_reg);
                        gtchan[chan] = gt_temp;
                        gt_start[wt][chan] = gtchan[chan];
                        if (error != 0){
                            sprintf(psb,"Error at slot, chan = %d %d\n",slot,chan);
                            print_send(psb,view_fdset);
                            return -1;
                        }
                    }// vv
                }// end loop over channels
                print_send("Measured initial GTVALIDS\n",view_fdset);

                //find maximum gtvalid time
                gmax[wt] = 0.0;
                cmax[wt] = 0;
                for (chan=0;chan<32;chan++){
                    if (chan_mask & (0x1<<chan)){
                        if (gtchan[chan] > gmax[wt]){
                            gmax[wt] = gtchan[chan];
                            cmax[wt] = chan;
                        }
                    }
                }
                //find minimum gtvalid time
                gmin[wt] = 9999.0;
                cmin[wt] = 0;
                for (chan=0;chan<32;chan++){
                    if (chan_mask & (0x1<<chan)){
                        if (gtchan[chan] < gmin[wt]){
                            gmin[wt] = gtchan[chan];
                            cmin[wt] = chan;
                        }
                    }
                }
                //print out results
                if (wt == 1){
                    sprintf(psb,"GTVALID measure results, time in nsec: \n");
                    print_send(psb,view_fdset);
                    for (chan=0;chan<32;chan++){
                        sprintf(psb,"Crate, slot, chan, GTDELAY0/1: %d %d %d %f %f\n",
                                cn, slot, chan, gt_start[0][chan],gt_start[1][chan]);
                        print_send(psb,view_fdset);
                    }
                    sprintf(psb,"TAC0 max-chan, max-gtvalid: %d %f\n",cmax[0],gmax[0]);
                    sprintf(psb+strlen(psb),"TAC1 max-chan, max-gtvalid: %d %f\n",cmax[1],gmax[1]);
                    sprintf(psb+strlen(psb),"TAC0 min-chan, min-gtvalid: %d %f\n",cmin[0],gmin[0]);
                    sprintf(psb+strlen(psb),"TAC1 min-chan, min-gtvalid: %d %f\n",cmin[1],gmin[1]);
                    print_send(psb,view_fdset);
                    // if measuring only, write output to standard paw format file
                    if (gt_cutoff == 0){
                        fp = fopen("get_tcmos.dat","a");
                        for (chan=0;chan<32;chan++){
                            //fprintf(fp,"%d %d %d %f %f \n",cn,slot,chan,gt_start[0][chan],gt_start[1][chan]);
                        }
                        fclose(fp);
                    }
                }


                // if gt_cutoff is set, we are going to change the ISETM dacs until all the
                // channels are just below it.
                if (gt_cutoff != 0){
                    sprintf(psb,"Finding ISETM values for crate,slot,TAC: %d %d %d\n",cn,slot,wt);
                    print_send(psb,view_fdset);
                    isetm_new[0] = ISETM;
                    isetm_new[1] = ISETM;
                    done = FALSE;
                    gt_temp = gmax[wt];
                    while (gt_temp > gt_cutoff){
                        isetm_new[wt]++;
                        error = loadsDac(dac_isetm[wt],isetm_new[wt],cn,select_reg);
                        if (error != 0){
                            print_send("Error loading Dac's', stopping...\n",view_fdset);
                            return 1;
                        }

                        // get new measurement of gtvalid for this channel
                        xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,0x1<<cmax[wt],&temp,cn);
                        error = get_gtdelay(cn, wt, &gt_temp,isetm_new[0],isetm_new[1],select_reg);
                        sprintf(psb,"- was %f, %f - \n",gt_temp,gt_cutoff);
                        //print_send(psb,view_fdset);
                    }

                    // check that we still have the max channel
                    for (chan=0;chan<32;chan++){
                        if (chan_mask & (0x1<<chan)){
                            xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,0x1<<chan,&temp,cn);
                            error = get_gtdelay(cn,wt,&gt_temp,isetm_new[0],isetm_new[1],select_reg);
                            gtchan[chan] = gt_temp;
                        }
                    }

                    // find maximum gtvalid time
                    gt_max_sec = 0.0;
                    chan_max_sec = 0;
                    for (chan=0;chan<32;chan++){
                        if (chan_mask & (0x1<<chan)){
                            gt_max_sec = gtchan[chan];
                            chan_max_sec = chan;
                        }
                    }

                    if (chan_max_sec != cmax[wt]){
                        print_send("Warning, second chan_max not same as first.\n",view_fdset);
                        cmax[wt] = chan_max_sec;
                        gmax[wt] = gt_max_sec;
                        gt_temp = gmax[wt];
                        while (gt_temp > gt_cutoff){
                            isetm_new[wt]++;
                            error = loadsDac(dac_isetm[wt],isetm_new[wt],cn,select_reg);
                            xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,0x1<<cmax[wt],&temp,cn);
                            error = get_gtdelay(cn,wt,&gt_temp,isetm_new[0],isetm_new[1],
                                    select_reg);
                            sprintf(psb,"- was %f, %f - \n",gt_temp,gt_cutoff);
                            print_send(psb,view_fdset);
                        }
                    }

                    // ok now we think that max channel is at gtcutoff
                    // now we change twiddle bits to get other channels close too
                    if (do_twiddle){
                        for (i=0;i<32;i++)
                            gtflag[wt][i]=0;
                        gtflag[wt][chan_max_sec] = 1; // we dont need to change max chan
                        done = FALSE;
                        while (!done){
                            for (chan=0;chan<32;chan++){
                                if (chan_mask & (0x1<<chan) && gtflag[wt][chan] == 0){
                                    xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,0x1<<chan,&temp,cn);
                                    error = get_gtdelay(cn,wt,&gt_temp,isetm_new[0],isetm_new[1],
                                            select_reg);
                                    gtchan_set[wt][chan] = gt_temp;
                                }
                            }
                            done = TRUE;
                            // successively turn off twiddle bits
                            for (chan=0;chan<32;chan++){
                                if (chan_mask & (0x1<<chan)){
                                    if (gtchan_set[wt][chan] <= gt_cutoff && gtflag[wt][chan] == 0
                                            && tacbits_new[wt][chan] != 0x0){
                                        tacbits_new[wt][chan] -= 0x1; // decrement twiddle by 1
                                        done = FALSE;
                                        sprintf(psb,"channel %d, %f, tacbits at %01x\n",chan,gtchan_set[wt][chan],tacbits_new[wt][chan]);
                                        print_send(psb,view_fdset);
                                    }else if (gtchan_set[wt][chan] > gt_cutoff && gtflag[wt][chan] == 0){
                                        tacbits_new[wt][chan] += 0x1; // go up just one
                                        if (tacbits_new[wt][chan] > 0x7)
                                            tacbits_new[wt][chan] = 0x7; //max
                                        gtflag[wt][chan] = 1; // is as close to gt_cutoff as possible
                                        sprintf(psb,"channel %d ok\n",chan);
                                        print_send(psb,view_fdset);
                                    }
                                }
                            }

                            // now load the tacbits
                            for (k=0;k<32;k++) // build twiddle word
                                tacbits[k] = tacbits_new[1][k]*16 + tacbits_new[0][k];
                            packet.cmdHeader.packet_type = LOADTACBITS_ID;
                            *pl = cn;
                            *(pl+1) = select_reg;
                            for (j=0;j<32;j++)
                                *(uint16_t *)(packet.payload+8+j*2) = tacbits[j];
                            SwapLongBlock(packet.payload,2);
                            SwapShortBlock(packet.payload+8,32);
                            do_xl3_cmd(&packet,cn);
                            print_send("Loaded tacbits\n",view_fdset);
                        } // end while(!done)
                    }

                    //we are done, save setup
                    isetm_save[wt] = isetm_new[wt];
                    for (k=0;k<32;k++)
                        tacbits_save[wt][k] = tacbits_new[wt][k];

                    // remeasure gtvalid
                    for (i=0;i<32;i++)
                        gtchan_set[wt][i] = 9999;

                    for (chan=0;chan<32;chan++){
                        if (chan_mask & (0x1<<chan)){
                            xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,1<<chan,&temp,cn);
                            error = get_gtdelay(cn,wt,&gt_temp,isetm_new[0],isetm_new[1],
                                    select_reg);
                            gtchan_set[wt][chan] = gt_temp;
                        }
                    }

                    //find maximum
                    gt_max_set[wt] = 0.0;
                    chan_max_set[wt] = 0;
                    for (i=0;i<32;i++){
                        if (gtchan_set[wt][i] > gt_max_set[wt]){
                            gt_max_set[wt] = gtchan_set[wt][i];
                            chan_max_set[wt] = i;
                        }
                    }

                    //find minimum
                    gt_min_set[wt] = 9999.0;
                    chan_min_set[wt] = 0;
                    for (i=0;i<32;i++){
                        if (gtchan_set[wt][i] < gt_min_set[wt]){
                            gt_min_set[wt] = gtchan_set[wt][i];
                            chan_min_set[wt] = i;
                        }
                    }

                    if (wt == 1){ // only do this once, after measuring TAC1

                        // print out results
                        if (!do_twiddle){
                           printsend(">>>ISETA0/1 = 0, no TAC twiddle bits set.\n");
                        }
                        sprintf(psb, "GTVALID setup results for Crate/Slot %d %d: \n", cn, j);
                        sprintf(psb+strlen(psb), "VMAX, TACREF, ISETA = %hu %hu %hu\n",VMAX,TACREF,ISETA);
                        sprintf(psb+strlen(psb), "CrCaCh, ISETM0/1, TacBits, GTValid0/1:\n");
                        print_send(psb, view_fdset);
                        for (i=0;i<32;i++){
                            sprintf(psb, "%d %d %d %d %d 0x%hx %f %f",
                                    cn,j,i,isetm_save[0],isetm_save[1],
                                    tacbits_save[1][i]*16 + tacbits_save[0][i],
                                    gtchan_set[0][i],gtchan_set[1][i]);
                            if (isetm_save[0] == ISETM || isetm_save[1] == ISETM)
                                sprintf(psb+strlen(psb), ">>> Warning: isetm not adjusted\n");
                            else
                                sprintf(psb+strlen(psb), "\n");
                            print_send(psb, view_fdset);
                        }
                        sprintf(psb, ">>>Maximum Chan/GTValid TAC0: %d %f \n", chan_max_set[0],gt_max_set[0]);
                        sprintf(psb+strlen(psb), ">>>Minimum Chan/GTValid TAC0: %d %f \n", chan_min_set[0],gt_min_set[0]);
                        sprintf(psb+strlen(psb), ">>>Maximum Chan/GTValid TAC1: %d %f \n", chan_max_set[1],gt_max_set[1]);
                        sprintf(psb+strlen(psb), ">>>Minimum Chan/GTValid TAC1: %d %f \n", chan_min_set[1],gt_min_set[1]);
                        sprintf(psb+strlen(psb), "********************************************\n");
                        print_send(psb, view_fdset);
                    }// end if wt==1

                }// end if gtvalid != 0

            }// end loop over TACS

            xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,0x0,&temp,cn); // reset pedestals
            for (i=0;i<32;i++){
                sprintf(psb, "%d %d %d %d %d 0x%hx %f %f",
                        cn,j,i,isetm_save[0],isetm_save[1],
                        tacbits_save[1][i]*16 + tacbits_save[0][i],
                        gtchan_set[0][i],gtchan_set[1][i]);
                if (isetm_save[0] == ISETM || isetm_save[1] == ISETM)
                    sprintf(psb+strlen(psb), ">>> Warning: isetm not adjusted\n");
                else
                    sprintf(psb+strlen(psb), "\n");
                print_send(psb, view_fdset);
            }
            sprintf(psb, ">>>Maximum Chan/GTValid TAC0: %d %f \n", chan_max_set[0],gt_max_set[0]);
            sprintf(psb+strlen(psb), ">>>Minimum Chan/GTValid TAC0: %d %f \n", chan_min_set[0],gt_min_set[0]);
            sprintf(psb+strlen(psb), ">>>Maximum Chan/GTValid TAC1: %d %f \n", chan_max_set[1],gt_max_set[1]);
            sprintf(psb+strlen(psb), ">>>Minimum Chan/GTValid TAC1: %d %f \n", chan_min_set[1],gt_min_set[1]);
            sprintf(psb+strlen(psb), "********************************************\n");
            print_send(psb, view_fdset);

            //store in DB
            if (update_db){
               printsend("updating the database\n");
                char hextostr[50];
                JsonNode *newdoc = json_mkobject();
                json_append_member(newdoc,"type",json_mkstring("cmos_m_gtvalid"));
                json_append_member(newdoc,"vmax",json_mknumber((double)VMAX));
                json_append_member(newdoc,"TACREF",json_mknumber((double)TACREF));
                JsonNode* isetm_new = json_mkarray();
                JsonNode* iseta_new = json_mkarray();
                json_append_element(isetm_new,json_mknumber((double)isetm_save[0]));
                json_append_element(isetm_new,json_mknumber((double)isetm_save[1]));
                json_append_element(iseta_new,json_mknumber((double)ISETA));
                json_append_element(iseta_new,json_mknumber((double)ISETA));
                json_append_member(newdoc,"isetm",isetm_new);
                json_append_member(newdoc,"iseta",iseta_new);
                JsonNode* tac_shift_new = json_mkarray();
                JsonNode* gtchan0 = json_mkarray();
                JsonNode* gtchan1 = json_mkarray();
                JsonNode* errors = json_mkarray();
                for (i=0;i<32;i++){
                    json_append_element(tac_shift_new,json_mknumber((double)(byte) (tacbits_save[1][i]*16+tacbits_save[0][1])));
                    json_append_element(gtchan0,json_mknumber((double)(byte) (gtchan_set[0][i])));
                    json_append_element(gtchan1,json_mknumber((double)(byte) (gtchan_set[1][i])));
                    json_append_element(errors,json_mknumber((double)0));//FIXME
                }
                json_append_member(newdoc,"tac_shift",tac_shift_new);
                json_append_member(newdoc,"GTValid_tac0",gtchan0);
                json_append_member(newdoc,"GTValid_tac1",gtchan1);
                json_append_member(newdoc,"errors",errors);
                json_append_member(newdoc,"pass",json_mkstring("yes"));//FIXME
                if (final_test)
                    json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[slot]));	
                post_debug_doc(cn,slot,newdoc);
                json_delete(newdoc); // only delete the head
            }

        }// vv
    }// end loop over slots

    deselect_fecs(cn);
    return 0;
}


int get_gtdelay(uint16_t crate_num, int wt, float *get_gtchan, uint16_t isetm0, uint16_t isetm1, uint32_t select_reg)
{
   printsend(".");
    fflush(stdout);

    float upper_limit, lower_limit, current_delay;
    int error, done, i;
    uint32_t temp, num_read;
    XL3_Packet packet;
    FECCommand *command;
    done = 0;
    upper_limit = GTMAX;
    lower_limit = 250;
    // set unmeasured TAC GTVALID to long window time
    if (wt == 0)
        error = loadsDac(131,(u_short)125,crate_num,select_reg);
    else
        error = loadsDac(132,(u_short)125,crate_num,select_reg);

    // find the time that the tac stops firing
    while (done == 0){
        //reset fifo
        xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0 | (crate_num << FEC_CSR_CRATE_OFFSET) | 0x2,&temp,crate_num);
        xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0 | (crate_num << FEC_CSR_CRATE_OFFSET),&temp,crate_num);

        //binary search for GT delay
        current_delay = (upper_limit - lower_limit)/2+lower_limit;
        current_delay = set_gt_delay(current_delay+GTPED_DELAY+TDELAY_EXTRA)-GTPED_DELAY-TDELAY_EXTRA;
        //for (i=0;i<NGTVALID;i++){
        //send_softgt();
        //}
        multi_softgt(NGTVALID);
        //mtc_multi_write(0xC,0x0,NGTVALID);

        //packet.cmdHeader.packet_type = CMOSGTVALID_ID;
        //*(uint32_t *) packet.payload = select_reg;
        //SwapLongBlock(packet.payload,1);
        //do_xl3_cmd(&packet,crate_num);
        //SwapLongBlock(packet.payload,2);
        //error = *(uint32_t *) packet.payload;
        //num_read = *(uint32_t *) (packet.payload+4);
        xl3_rw(FIFO_WRITE_PTR_R + select_reg + READ_REG,0x0,&temp,crate_num);
        //packet.cmdHeader.packet_type = CMOSGTVALID_ID;
        //command = (FECCommand *) packet.payload;
        //command->flags = 0x0;
        //command->cmd_num = 0x0;
        //command->packet_num = 0x0;
        //command->address = FIFO_WRITE_PTR_R + select_reg + READ_REG;
        //command->data = 0x0;
        //SwapLongBlock(&(command->data),1);
        //SwapLongBlock(&(command->address),1);
        //do_xl3_cmd(&packet, crate_num);
        //temp = command->data;
        //SwapLongBlock(&temp,1);
        //*(uint32_t *) 
        num_read = (temp & 0x000FFFFF)/3UL;
        //if (error != 0)
        //    return -1;
        //printsend("delay %f, num %d\n",current_delay,num_read);
        // now check to see if we saw the right number of events
        if (num_read < (NGTVALID)*0.75)
            upper_limit = current_delay;
        else
            lower_limit = current_delay;

        if (upper_limit-lower_limit <= 1)
            done = 1;
    }

    if (upper_limit == GTMAX){
        *get_gtchan = 999;
    }else{
        // ok we know that lower limit is within the window, upper limit is outside
        // lets make sure its the right TAC failing by making the window longer
        // and seeing if the events show back up
        if (wt == 0)
            error = loadsDac(132,(u_short)125,crate_num,select_reg);
        else
            error = loadsDac(131,(u_short)125,crate_num,select_reg);

        //reset fifo
        xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0 | (crate_num << FEC_CSR_CRATE_OFFSET) | 0x2,&temp,crate_num);
        xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0 | (crate_num << FEC_CSR_CRATE_OFFSET),&temp,crate_num);
        current_delay = set_gt_delay(upper_limit+GTPED_DELAY+TDELAY_EXTRA)-GTPED_DELAY-TDELAY_EXTRA;
        multi_softgt(NGTVALID);
        //for (i=0;i<NGTVALID;i++){
        //    send_softgt();
        //}

        //packet.cmdHeader.packet_type = CMOSGTVALID_ID;
        //*(uint32_t *) packet.payload = select_reg;
        //SwapLongBlock(packet.payload,1);
        //do_xl3_cmd(&packet,crate_num);
        //SwapLongBlock(packet.payload,2);
        //error = *(uint32_t *) packet.payload;
        //num_read = *(uint32_t *) (packet.payload+4);
        xl3_rw(FIFO_WRITE_PTR_R + select_reg + READ_REG,0x0,&temp,crate_num);
        num_read = (temp & 0x000FFFFF)/3UL;
        //printsend("check, num %d\n",num_read);
        //if (error != 0)
        //    return -1;
        // now check to see if we saw the right number of events
        if (num_read < (NGTVALID)*0.75){
           printsend("Uh oh, still not all the events\n");
            //return -1;
        }
        *get_gtchan = upper_limit;
    }

    //set TACS back to original time
    error = loadsDac(131,(u_short) isetm1, crate_num, select_reg);
    error = loadsDac(132,(u_short) isetm0, crate_num, select_reg);
    return 0;
}

static int setup_crate(int cn, uint16_t slot_mask)
{
    int i;
    uint32_t select_reg, result,temp;
    print_send("Resetting fifo and pedestals.\n", view_fdset);
    for (i=0;i<16;i++){ 
        if (((0x1<<i)&slot_mask) != 0x0){
            select_reg = FEC_SEL * i;

            // disable pedestals
            xl3_rw(PED_ENABLE_R + select_reg + WRITE_REG,0x0,&result,cn);

            // reset fifo
            xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0xFFFFFFFF,&result,cn);
            xl3_rw(GENERAL_CSR_R + select_reg + READ_REG,0x0,&temp,cn);
            // mask in crate_address and fifo reset
            xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,temp | (cn << FEC_CSR_CRATE_OFFSET) | 0x6,&result,cn);
            xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0 | (cn << FEC_CSR_CRATE_OFFSET),&result,cn);
            xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0x0,&result,cn);
        }
    }

    deselect_fecs(cn);
    return 0;
}


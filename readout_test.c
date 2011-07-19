#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "penn_daq.h"
#include "include/xl_regs.h"
#include "fec_util.h"
#include "mtc_util.h"
#include "net_util.h"

#define PED_WIDTH	    25
#define GT_DELAY	    150
#define GT_FINE_DELAY	    0
#define NUM_CELLS	    16
#define TWOTWENTY	    0x100000
#define SLOT_MASK_DEFAULT   0xFFFF
#define MY_UNPK_QLX(a)   ( (*(a+1) & 0x7FFUL) | ( ~ (*(a+1)) & 0x800UL))

int readout_test(char *buffer)
{
    if (sbc_is_connected == 0){
        printsend("SBC not connected.\n");
        
        return -1;
    }
    count_d =0;
    int i,j,errors=0,count,eprint,ch,cell,crateID,num_events,slot_iter;
    uint32_t *pmt_buffer, *pmt_iter;

    int ped_low = 400;
    int ped_high = 700;
    uint32_t crate_mask = 0x4;
    u_long pattern = 0xFFFFFFFF;
    float frequency = 1000.0;
    uint32_t gtdelay = GT_DELAY;
    uint16_t ped_width = PED_WIDTH;
    time_t now;
    FILE *file = stdout; // default file descriptor

    struct pedestal *ped;

    uint32_t select_reg;
    uint32_t slot_mask[20];
    for (i=0;i<20;i++){
        slot_mask[i] = 0xFFFF;
    }

    char *words,*words2;

    // lets get the parameters from command line
    words = strtok(buffer, " ");
    while (words != NULL){
        if (words[0] == '-'){
            if (words[1] == 'c'){
                words2 = strtok(NULL, " ");
                crate_mask = strtoul(words2,(char**)NULL,16);
            }else if (words[1] == 's'){
                words2 = strtok(NULL, " ");
                for (i=0;i<20;i++){
                    slot_mask[i] = strtoul(words2,(char**)NULL,16);
                }
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
            }else if (words[1] == 'h'){
                printsend("Usage: readout_test -c"
                        " [crate_mask (hex)] -s [slot mask (hex)] -f [frequency] -p [pattern] -t [gtdelay] -w [ped width]\n");
                
                return -1;
            }else if (words[1] == '0'){
                if (words[2] == '0'){
                    words2 = strtok(NULL, " ");
                    slot_mask[0] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '1'){
                    words2 = strtok(NULL, " ");
                    slot_mask[1] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '2'){
                    words2 = strtok(NULL, " ");
                    slot_mask[2] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '3'){
                    words2 = strtok(NULL, " ");
                    slot_mask[3] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '4'){
                    words2 = strtok(NULL, " ");
                    slot_mask[4] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '5'){
                    words2 = strtok(NULL, " ");
                    slot_mask[5] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '6'){
                    words2 = strtok(NULL, " ");
                    slot_mask[6] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '7'){
                    words2 = strtok(NULL, " ");
                    slot_mask[7] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '8'){
                    words2 = strtok(NULL, " ");
                    slot_mask[8] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '9'){
                    words2 = strtok(NULL, " ");
                    slot_mask[9] = strtoul(words2, (char **) NULL, 16);
                }
            }else if (words[1] == '1'){
                if (words[2] == '0'){
                    words2 = strtok(NULL, " ");
                    slot_mask[10] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '1'){
                    words2 = strtok(NULL, " ");
                    slot_mask[11] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '2'){
                    words2 = strtok(NULL, " ");
                    slot_mask[12] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '3'){
                    words2 = strtok(NULL, " ");
                    slot_mask[13] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '4'){
                    words2 = strtok(NULL, " ");
                    slot_mask[14] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '5'){
                    words2 = strtok(NULL, " ");
                    slot_mask[15] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '6'){
                    words2 = strtok(NULL, " ");
                    slot_mask[16] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '7'){
                    words2 = strtok(NULL, " ");
                    slot_mask[17] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '8'){
                    words2 = strtok(NULL, " ");
                    slot_mask[18] = strtoul(words2, (char **) NULL, 16);
                }else if (words[2] == '9'){
                    words2 = strtok(NULL, " ");
                    slot_mask[19] = strtoul(words2, (char **) NULL, 16);
                }
            }

        }
        words = strtok(NULL, " ");
    }

    (void) time(&now);

    if(file != stdout){
        fprintf(file,"Readout Test Setup\n");
        fprintf(file,"-------------------------------------------\n");
        fprintf(file,"Crate Mask:		    0x%05x\n",crate_mask);
        fprintf(file,"Pedestal Mask:	    0x%08lx\n",pattern);
        fprintf(file,"GT delay (ns):	    %3hu\n", gtdelay);
        fprintf(file,"Pedestal Width (ns):    %2d\n",ped_width);
        fprintf(file,"Pulser Frequency (Hz):  %3.0f\n",frequency);
        fprintf(file,"\nRun started at %.24s\n",ctime(&now));
    }
    else{
        printsend("Readout Test Setup\n");
        printsend("-------------------------------------------\n");
        printsend("Crate Mask:		    0x%05x\n",crate_mask);
        printsend("Pedestal Mask:	    0x%08lx\n",pattern);
        printsend("GT delay (ns):	    %3hu\n", gtdelay);
        printsend("Pedestal Width (ns):    %2d\n",ped_width);
        printsend("Pulser Frequency (Hz):  %3.0f\n",frequency);
        printsend("\nRun started at %.24s\n",ctime(&now));
        
    }

    uint32_t result;

    XL3_Packet packet;
    uint32_t *p = (uint32_t *) packet.payload;
    for (i=0;i<19;i++){
        if (((0x1<<i)&crate_mask) && (connected_xl3s[i] != -999)){
           printsend("starting readout on crate %d\n",i);
            packet.cmdHeader.packet_type = CHANGE_MODE_ID;
            *p = 0x1;
            SwapLongBlock(p,1);
            do_xl3_cmd(&packet,i);
            for (slot_iter = 0; slot_iter < 16; slot_iter ++){
                if ((0x1 << slot_iter) & slot_mask[i]){
                    select_reg = FEC_SEL*slot_iter;
                    xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0xFFFFFFFF,&result,i);
                    xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x2,&result,i);
                    xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0,&result,i);
                    xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0x0,&result,i);
                }
            }
            deselect_fecs(i); 

            errors = 0;
            errors += fec_load_crateadd(i, slot_mask[i]);
            errors += set_crate_pedestals(i, slot_mask[i], pattern);

            deselect_fecs(i);
        }
    }



    errors = setup_pedestals(frequency,ped_width,gtdelay,GT_FINE_DELAY);
    if (errors){
        printsend("Error setting up MTC for pedestals. Exiting\n");
        unset_ped_crate_mask(MASKALL);
        unset_gt_crate_mask(MASKALL);
        return -1;
    }

    //enable GT/PED only for selected crate
    unset_ped_crate_mask(MASKALL);
    unset_gt_crate_mask(MASKALL);
    set_ped_crate_mask(crate_mask);
    set_gt_crate_mask(crate_mask);
    set_ped_crate_mask(MSK_TUB);
    set_gt_crate_mask(MSK_TUB);
    //wait for pedestals to arrive
    //wtime = (wtime * 1E6); // set this to usec
    //usleep((u_int) wtime);


    // now we can enable the readout

    for (i=0;i<19;i++){
        if (((0x1<<i)&crate_mask) && (connected_xl3s[i] != -999)){
            packet.cmdHeader.packet_type = CHANGE_MODE_ID;
            *p = 0x2;
            *(p+1) = slot_mask[i];
            SwapLongBlock(p,2);
            do_xl3_cmd(&packet,i);
        }
    }
    return 0;

}

int readout_add_crate(char *buffer)
{
    int crate = 2;
    u_long pattern = 0xFFFFFFFF;
    uint32_t select_reg;
    uint32_t slot_mask = 0x2000;
    uint32_t result;
    int slot_iter;
    char *words,*words2;

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
            }else if (words[1] == 'p'){
                words2 = strtok(NULL, " ");
                pattern = strtoul(words2, (char **) NULL, 16);
            }else if (words[1] == 'h'){
                printsend("Usage: readout_addcrate -c"
                        " [crate num] -s [slot mask (hex)] -p [pattern]\n");
                
                return -1;
            }
        }
        words = strtok(NULL, " ");
    }

    printsend("Readout Test Setup - adding a crate\n");
    printsend("-------------------------------------------\n");
    printsend("Crate:		    %d\n",crate);
    printsend("Slot Mask:		    0x%4hx\n",slot_mask);
    printsend("Pedestal Mask:	    0x%08lx\n",pattern);
    

    XL3_Packet packet;
    uint32_t *p = (uint32_t *) packet.payload;
    packet.cmdHeader.packet_type = CHANGE_MODE_ID;
    *p = 0x1;
    SwapLongBlock(p,1);
    do_xl3_cmd(&packet,crate);
    for (slot_iter = 0; slot_iter < 16; slot_iter ++){
        if ((0x1 << slot_iter) & slot_mask){
            select_reg = FEC_SEL*slot_iter;
            xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0xFFFFFFFF,&result,crate);
            xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x2,&result,crate);
            xl3_rw(GENERAL_CSR_R + select_reg + WRITE_REG,0x0,&result,crate);
            xl3_rw(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0x0,&result,crate);
        }
    }
    deselect_fecs(crate); 

    int errors = 0;
    errors += fec_load_crateadd(crate, slot_mask);
    errors += set_crate_pedestals(crate, slot_mask, pattern);

    deselect_fecs(crate);

    // now we can enable the readout
    packet.cmdHeader.packet_type = CHANGE_MODE_ID;
    *p = 0x2;
    *(p+1) = slot_mask;
    SwapLongBlock(p,2);
    do_xl3_cmd(&packet,crate);
    return 0;
}

int readout_add_mtc(char *buffer)
{
    if (sbc_is_connected == 0){
        printsend("SBC not connected.\n");
        
        return -1;
    }
    count_d =0;
    int i,j,errors=0,count,eprint,ch,cell,crateID,num_events,slot_iter;
    uint32_t *pmt_buffer, *pmt_iter;

    int ped_low = 400;
    int ped_high = 700;
    uint32_t crate_mask = 0x4;
    float frequency = 1000.0, wtime;
    uint32_t gtdelay = GT_DELAY;
    uint16_t ped_width = PED_WIDTH;
    time_t now;
    FILE *file = stdout; // default file descriptor

    struct pedestal *ped;

    uint32_t select_reg;

    char *words,*words2;

    // lets get the parameters from command line
    words = strtok(buffer, " ");
    while (words != NULL){
        if (words[0] == '-'){
            if (words[1] == 'c'){
                words2 = strtok(NULL, " ");
                crate_mask = strtoul(words2,(char**)NULL,16);
            }else if (words[1] == 'f'){
                words2 = strtok(NULL, " ");
                frequency =  atof(words2);
            }else if (words[1] == 't'){
                words2 = strtok(NULL, " ");
                gtdelay = atoi(words2);
            }else if (words[1] == 'w'){
                words2 = strtok(NULL, " ");
                ped_width = atoi(words2);
            }else if (words[1] == 'h'){
                printsend("Usage: readout_add_mtc -c [crate mask (hex)] -f [freq (0 = softgt)]\n"
                        "-t [gtdelay] -w [ped_width]\n");
                
                return -1;
            }
        }
        words = strtok(NULL, " ");
    }


    uint32_t result;

    errors = setup_pedestals(frequency,ped_width,gtdelay,GT_FINE_DELAY);
    if (errors){
        printsend("Error setting up MTC for pedestals. Exiting\n");
        unset_ped_crate_mask(MASKALL);
        unset_gt_crate_mask(MASKALL);
        return -1;
    }

    //enable GT/PED only for selected crate
    unset_ped_crate_mask(MASKALL);
    unset_gt_crate_mask(MASKALL);
    set_ped_crate_mask(crate_mask);
    set_gt_crate_mask(crate_mask);
    set_ped_crate_mask(MSK_TUB);
    set_gt_crate_mask(MSK_TUB);

    return 0;
}
int end_readout(char *buffer)
{
    disable_pulser();

    //disable trigger enables
    unset_ped_crate_mask(MASKALL);
    unset_gt_crate_mask(MASKALL);

    // put back in init mode

    XL3_Packet packet;
    uint32_t *p = (uint32_t *) packet.payload;
    int i;
    for (i=0;i<19;i++){
        if (connected_xl3s[i] != -999){
            packet.cmdHeader.packet_type = CHANGE_MODE_ID;
            *p = 0x1;
            do_xl3_cmd(&packet,i);

            //unset pedestalenable
            set_crate_pedestals(i, 0xFFFF, 0x0);


            deselect_fecs(i);
        }
    }
    return 0;
}

int change_pulser(char *buffer)
{
    if (sbc_is_connected == 0){
        printsend("SBC not connected.\n");
        
        return -1;
    }
    int errors=0;
    float frequency = 1000.0;
    uint32_t gtdelay = GT_DELAY;
    uint16_t ped_width = PED_WIDTH;
    char *words,*words2;

    // lets get the parameters from command line
    words = strtok(buffer, " ");
    while (words != NULL){
        if (words[0] == '-'){
            if (words[1] == 'f'){
                words2 = strtok(NULL, " ");
                frequency =  atof(words2);
            }else if (words[1] == 'h'){
                printsend("usage: change_pulser -f [frequency]\n");
                
                return -1;
            }
        }
        words = strtok(NULL, " ");
    }

    printsend( "Pulser Frequency (Hz):  %3.0f\n",frequency);
    

    errors = setup_pedestals(frequency,ped_width,gtdelay,GT_FINE_DELAY);
    if (errors){
        printsend("Error setting up MTC for pedestals. Exiting\n");
        unset_ped_crate_mask(MASKALL);
        unset_gt_crate_mask(MASKALL);
        return -1;
    }
    return 0;
}

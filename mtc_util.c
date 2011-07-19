#include <string.h>
#include <stdlib.h> 
#include <stdio.h>
#include <stdint.h>

#include "include/xl_regs.h"

#include "penn_daq.h"
#include "fec_util.h"
#include "mtc_util.h"
#include "net_util.h"

static char* getXilinxData(long *howManyBits);

static char xilinxfilename[] = MTC_XILINX_LOCATION;

SBC_Packet aPacket;

int trigger_scan(char *buffer)
{
    int trigger = 13;
    int crate = 2;
    uint16_t slot_mask = 0x4000;
    char *words,*words2;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 't'){
		words2 = strtok(NULL, " ");
		trigger = atoi(words2);
		if (trigger > 13 || trigger < 0){
		    printf("Invalid trigger, resetting to default (13)\n");
		    trigger = 13;
		}
	    }
	    if (words[1] == 'c'){
		words2 = strtok(NULL, " ");
		crate = atoi(words2);
	    }
	    if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		slot_mask = strtoul(words2,(char**) NULL,16);
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: set_gt_mask -t [raw trigs to add (hex)]"
			" -c (clear gt mask first)\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }
    if (sbc_is_connected == 0){
        printsend("SBC not connected.\n");
        return -1;
    }

    printsend("starting a trigger scan\n");
    int nhit;
    uint32_t select_reg,pedestals[16],result,beforegt,aftergt;
    uint32_t gtdelay = 150;
    uint16_t ped_width = 25;
    int slot_num = 14;

    int counts[14];
    int i,j;
    for (i=0;i<14;i++){
        counts[i] = 10; 
    }

    // set up the mtcd to send out softgts
    int errors = setup_pedestals(0,ped_width,gtdelay,0);
    if (errors){
        printsend("Error setting up MTC for pedestals. Exiting\n");
        unset_ped_crate_mask(MASKALL);
        unset_gt_crate_mask(MASKALL);
        return -1;
    }

    //enable GT/PED only for selected crate
    unset_ped_crate_mask(MASKALL);
    unset_gt_crate_mask(MASKALL);
    //set_ped_crate_mask(0x1<<crate_num);
    //set_gt_crate_mask(0x1<<crate_num);
    set_ped_crate_mask(0xFFFFFFFF);
    set_gt_crate_mask(0xFFFFFFFF);
    unset_gt_mask(0xFFFFFFFF);
    set_gt_mask(1);

    float values[32*16][150];
    for (i=0;i<32*16;i++)
	for (j=0;j<150;j++)
	    values[i][j] = -999.;
    for (i=0;i<16;i++){
	pedestals[i] = 0x0;
	if ((0x1<<i) & slot_mask)
	    xl3_rw(PED_ENABLE_R + FEC_SEL*i + WRITE_REG,pedestals[i],&result,crate);
    }

    int ithresh;
    int num_slots = 0;


    // now we turn each channel on one at a time
    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    for (nhit=0;nhit<32;nhit++){
		pedestals[i] |= 0x1<<nhit;
		if (i == 7)
		    pedestals[i] &= 0xFFF7FFFF;
		xl3_rw(PED_ENABLE_R + i*FEC_SEL + WRITE_REG,pedestals[i],&result,crate);

		// loop over thresholds
		for (ithresh=0;ithresh<145;ithresh++){
		    counts[trigger] = 3950+ithresh;
		    load_mtc_dacs_counts(counts);

		    // now get current gt count
		    mtc_reg_read(MTCOcGtReg,&beforegt);


		    // send 20 pulses
		    multi_softgt(500);

		    // now get final gt count
		    mtc_reg_read(MTCOcGtReg,&aftergt);

		    uint32_t diff = aftergt-beforegt;
		    values[32*num_slots+nhit][ithresh] = (float) diff/500.0;
		}
	    }
	    num_slots++;
	}
    }

    unset_gt_mask(MASKALL);

    for (i=0;i<32*num_slots;i++)
        for (j=0;j<145;j++)
            printsend("%d %d %f\n",i,j,values[i][j]);

    return 0;
}

int mtc_xilinxload(void)
{
    char *data;
    long howManybits;
    long bitIter;
    uint32_t word;
    uint32_t dp;
    uint32_t temp;

    printsend("loading xilinx\n");
    data = getXilinxData(&howManybits);
    if ((data == NULL) || (howManybits == 0)){
        printsend("error getting xilinx data\n");
        return -1;
    }

    aPacket.cmdHeader.destination = 0x3;
    aPacket.cmdHeader.cmdID = 0x1;
    aPacket.cmdHeader.numberBytesinPayload = sizeof(SNOMtc_XilinxLoadStruct) + howManybits;
    printsend("numbytes is %d, size is %d\n",(int)aPacket.cmdHeader.numberBytesinPayload,(int)sizeof(SNOMtc_XilinxLoadStruct));
    aPacket.numBytes = aPacket.cmdHeader.numberBytesinPayload+256+16;
    SNOMtc_XilinxLoadStruct *payloadPtr = (SNOMtc_XilinxLoadStruct *)aPacket.payload;
    payloadPtr->baseAddress = 0x7000;
    payloadPtr->addressModifier = 0x29;
    payloadPtr->errorCode = 0;
    payloadPtr->programRegOffset = MTCXilProgReg;
    payloadPtr->fileSize = howManybits;
    char *p = (char *)payloadPtr + sizeof(SNOMtc_XilinxLoadStruct);
    strncpy(p, data, howManybits);
    do_mtc_xilinx_cmd(&aPacket);
    long errorCode = payloadPtr->errorCode;
    if (errorCode){
        printsend( "Error code: %d \n",(int)errorCode);
    }
    printsend("Xilinx loading complete\n");

    free(data);
    data = (char *) NULL;
    return 0;
}

/* return a structure of chars that sets up the data structure with
 * all the data for the xilinx chips.  function to read in
 * bitstream. filename hard-coded for now.
 *
 *  PW 8/21/97 
 */
static char* getXilinxData(long *howManyBits)
{
    char c;
    FILE *fp;
    char *data = NULL;

    if ((fp = fopen(xilinxfilename, "r")) == NULL ) {
        printsend( "getXilinxData:  cannot open file %s\n", xilinxfilename);
        return (char*) NULL;
    }

    if ((data = (char *) malloc(MAX_DATA_SIZE)) == NULL) {
        //perror("GetXilinxData: ");
        printsend("GetXilinxData: malloc error\n");
        return (char*) NULL;
    }

    /* skip header -- delimited by two slashes. 
       if ( (c = getc(fp)) != '/') {
       fprintf(stderr, "Invalid file format Xilinx file.\n");
       return (char*) NULL;
       }
       while (( (c = getc(fp))  != EOF ) && ( c != '/'))
       ;
     */
    /* get real data now. */
    *howManyBits = 0;
    while (( (data[*howManyBits] = getc(fp)) != EOF)
            && ( *howManyBits < MAX_DATA_SIZE)) {
        /* skip newlines, tabs, carriage returns */
        if ((data[*howManyBits] != '\n') &&
                (data[*howManyBits] != '\r') &&
                (data[*howManyBits] != '\t') ) {
            (*howManyBits)++;
        }


    }
    fclose(fp);
    return data;
}

/*
 * unset_gt_mask(raw_trig_types)
 * unset bit(s) in the global trigger mask, according to mnemonics   
 * M. Neubauer 2 Sept 1997 
 */
int unset_gt_mask_cmd(char *buffer){
    uint32_t type = 0xFFFFFFFF;
    char *words,*words2;
    words = strtok(buffer, " ");
    while (words != NULL){
        if (words[0] == '-'){
            if (words[1] == 't'){
                words2 = strtok(NULL, " ");
                type = strtoul(words2,(char **) NULL,16);
            }
            if (words[1] == 'h'){
                printsend("Usage: unset_gt_mask -t [raw trigs to remove (hex)]\n");
                return 0;
            }
        }
        words = strtok(NULL, " ");
    }
    unset_gt_mask(type);
    return 0;
}

int set_gt_mask_cmd(char *buffer){
    uint32_t type = 0x0;
    int clear = 0;
    char *words,*words2;
    words = strtok(buffer, " ");
    while (words != NULL){
        if (words[0] == '-'){
            if (words[1] == 't'){
                words2 = strtok(NULL, " ");
                type = strtoul(words2,(char **) NULL,16);
            }
            if (words[1] == 'c'){
                clear = 1;
            }
            if (words[1] == 'h'){
                printsend("Usage: set_gt_mask -t [raw trigs to add (hex)]"
                        " -c (clear gt mask first)\n");
                return 0;
            }
        }
        words = strtok(NULL, " ");
    }
    if (clear == 1)
        unset_gt_mask(0xFFFFFFFF);
    set_gt_mask(type);
    return 0;
}

int mtc_read(char *buffer){
    uint32_t address = 0x0;
    uint32_t data = 0x0;
    char *words,*words2;
    words = strtok(buffer, " ");
    while (words != NULL){
        if (words[0] == '-'){
            if (words[1] == 'a'){
                words2 = strtok(NULL, " ");
                address = strtoul(words2,(char **) NULL,16);
            }
            if (words[1] == 'h'){
                printsend("Usage: mtc_read -a [address (hex)]\n");
                return 0;
            }
        }
        words = strtok(NULL, " ");
    }
    mtc_reg_read(address, &data);
    printsend("Received %08x\n",data);
    return 0;
}


int mtc_write(char *buffer){
    uint32_t address = 0x0;
    uint32_t data = 0x0;
    char *words,*words2;
    words = strtok(buffer, " ");
    while (words != NULL){
        if (words[0] == '-'){
            if (words[1] == 'd'){
                words2 = strtok(NULL, " ");
                data = strtoul(words2,(char **) NULL,16);
            }
            if (words[1] == 'a'){
                words2 = strtok(NULL, " ");
                address = strtoul(words2,(char **) NULL,16);
            }
            if (words[1] == 'h'){
                printsend("Usage: mtc_write -d [data (hex)] -a [address (hex)]\n");
                return 0;
            }
        }
        words = strtok(NULL, " ");
    }
    mtc_reg_write(address, data);
    printsend("wrote %08x\n",data);
    return 0;
}

void unset_gt_mask(unsigned long raw_trig_types) {
    uint32_t temp;
    mtc_reg_read(MTCMaskReg, &temp);
    mtc_reg_write(MTCMaskReg, temp & ~raw_trig_types);
    printsend("Triggers have been removed from the GT Mask\n");
}

void set_gt_mask(uint32_t raw_trig_types){
    uint32_t temp;
    mtc_reg_read(MTCMaskReg, &temp);
    mtc_reg_write(MTCMaskReg, temp | raw_trig_types);
    //printsend("Triggers have been added to the GT Mask\n");
}

/*
 * Mask out crates for pedestal lines, according to mnemonics.
 *
 *  jrk 1 Sept. 1997 (modified 8 Sept 1997 M. Neubauer)
 */

void unset_ped_crate_mask(unsigned long crates) {
    uint32_t temp;
    mtc_reg_read(MTCPmskReg, &temp);
    mtc_reg_write(MTCPmskReg, temp & ~crates);
    //printsend("Crates have been removed from the Pedestal Crate Mask\n");
}

uint32_t set_ped_crate_mask(uint32_t crates){
    uint32_t old_ped_crate_mask;
    mtc_reg_read(MTCPmskReg, &old_ped_crate_mask);
    mtc_reg_write(MTCPmskReg, old_ped_crate_mask | crates);
    //printsend("Crates have been added to the Pedestal Crate Mask\n");
    return old_ped_crate_mask;
}

/*
 * Mask out crates for global trigger lines, according to mnemonics.
 *
 */

void unset_gt_crate_mask(unsigned long crates) {
    uint32_t temp;
    mtc_reg_read(MTCGmskReg, &temp);
    mtc_reg_write(MTCGmskReg, temp & ~crates);
    //printsend("Crates have been removed from the GT Crate Mask\n");
}

void set_gt_crate_mask(uint32_t crates){
    uint32_t temp;
    mtc_reg_read(MTCGmskReg, &temp);
    mtc_reg_write(MTCGmskReg, temp | crates);
    //printsend("Crates have been added to the GT Crate Mask\n");
}

int set_thresholds(char *buffer){
    mtc_cons thresholds;
    int i;
    for (i=0;i<14;i++){
        thresholds.mtca_dac_values[i] = -4900; 
    }
    char *words,*words2;
    words = strtok(buffer, " ");
    while (words != NULL){
        if (words[0] == '-'){
            if (words[1] == '0'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[0] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == '1'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[1] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == '2'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[2] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == '3'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[3] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == '4'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[4] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == '5'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[5] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == '6'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[6] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == '7'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[7] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == '8'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[8] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == '9'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[9] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == 'a'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[10] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == 'b'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[11] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == 'c'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[12] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == 'd'){
                words2 = strtok(NULL, " ");
                thresholds.mtca_dac_values[13] = (float) strtod(words2,(char**)NULL)*1000;
            }
            if (words[1] == 'h'){
                printsend("Usage: set_thresholds -(0..d) [level (float)]\n");
                return 0;
            }
        }
        words = strtok(NULL, " ");
    }
    load_mtc_dacs(&thresholds);

    return 0;
}


/* load_mtc_dacs()
 * Loads all the mtc/a dac thresholds. Values are taken from a file for now.
 *
 * M. Neubauer 4 Sept 1997
 */

int load_mtc_dacs(mtc_cons *mtc_cons_ptr) {


    unsigned long shift_value;
    unsigned short raw_dacs[14];
    char dac_names[][14]={"N100LO","N100MED","N100HI","NHIT20","NH20LB","ESUMHI",
        "ESUMLO","OWLEHI","OWLELO","OWLN","SPARE1","SPARE2",
        "SPARE3","SPARE4"};
    short rdbuf;
    int i, j, bi, di;
    float mV_dacs;

    printsend("Loading MTC/A threshold DACs...\n");

    /* convert each threshold from mVolts to raw value and load into
       raw_dacs array */

    for (i = 0; i < 14; i++) {
        rdbuf = mtc_cons_ptr->mtca_dac_values[i];
        //raw_dacs[i] = ((2048 * rdbuf)/5000) + 2048;
        raw_dacs[i] = MTCA_DAC_SLOPE * rdbuf + MTCA_DAC_OFFSET;
        mV_dacs = (((float)raw_dacs[i]/2048) * 5000.0) - 5000.0;
        printsend( "\t%s\t threshold set to %6.2f mVolts\n", dac_names[i],
                mV_dacs);
    }

    /* set DACSEL */
    mtc_reg_write(MTCDacCntReg,DACSEL);

    /* shift in raw DAC values */

    for (i = 0; i < 4 ; i++) {
        mtc_reg_write(MTCDacCntReg,DACSEL | DACCLK); /* shift in 0 to first 4 dummy bits */
        mtc_reg_write(MTCDacCntReg,DACSEL);
    }

    shift_value = 0UL;
    for (bi = 11; bi >= 0; bi--) {                     /* shift in 12 bit word for each DAC */
        for (di = 0; di < 14 ; di++){
            if (raw_dacs[di] & (1UL << bi))
                shift_value |= (1UL << di);
            else
                shift_value &= ~(1UL << di);
        }
        mtc_reg_write(MTCDacCntReg,shift_value | DACSEL);
        mtc_reg_write(MTCDacCntReg,shift_value | DACSEL | DACCLK);
        mtc_reg_write(MTCDacCntReg,shift_value | DACSEL);
    }
    /* unset DASEL */
    mtc_reg_write(MTCDacCntReg,0x0);


    printsend("DAC loading complete\n");
    return 0;
}

int load_mtc_dacs_counts(int *counts)
{
    printsend("Loading MTC/A threshold DACs...\n");
    int i,bi,di;
    uint32_t shift_value;
    float mv_dacs;
    char dac_names[][14]={"N100LO","N100MED","N100HI","NHIT20","NH20LB","ESUMHI",
        "ESUMLO","OWLEHI","OWLELO","OWLN","SPARE1","SPARE2",
        "SPARE3","SPARE4"};

    for (i=0;i<14;i++){
        mv_dacs = ((float) counts[i]/2048)*5000.0-5000.0;
        printsend( "\t%s\t threshold set to %6.2f mVolts (%d counts)\n",dac_names[i],mv_dacs,counts[i]);
    }

    /* set DACSEL */
    mtc_reg_write(MTCDacCntReg,DACSEL);

    /* shift in raw DAC values */

    for (i = 0; i < 4 ; i++) {
        mtc_reg_write(MTCDacCntReg,DACSEL | DACCLK); /* shift in 0 to first 4 dummy bits */
        mtc_reg_write(MTCDacCntReg,DACSEL);
    }

    shift_value = 0UL;
    for (bi = 11; bi >= 0; bi--) {                     /* shift in 12 bit word for each DAC */
        for (di = 0; di < 14 ; di++){
            if (counts[di] & (1UL << bi))
                shift_value |= (1UL << di);
            else
                shift_value &= ~(1UL << di);
        }
        mtc_reg_write(MTCDacCntReg,shift_value | DACSEL);
        mtc_reg_write(MTCDacCntReg,shift_value | DACSEL | DACCLK);
        mtc_reg_write(MTCDacCntReg,shift_value | DACSEL);
    }
    /* unset DASEL */
    mtc_reg_write(MTCDacCntReg,0x0);


    printsend("DAC loading complete\n");
    return 0;
}

/*
 * set_lockout_width(width)
 * sets the width of the global trigger lockout time
 * width is in ns
 * M. Neubauer 2 Sept 1997
 */

int set_lockout_width(unsigned short width) {

    unsigned long gtlock_value;

    if ((width < 20) || (width > 5100)) {
        printsend("Lockout width out of range\n");
        return -1;
    }
    gtlock_value = ~(width / 20);
    uint32_t temp;
    mtc_reg_write(MTCGtLockReg,gtlock_value);
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp | LOAD_ENLK); /* write GTLOCK value */
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp & ~LOAD_ENLK); /* toggle load enable */

    //printsend( "Lockout width is set to %u ns\n", width);
    return 0;

}

/* set_gt_counter(count) 
 * load a count into the global trigger counter
 * 
 * M. Neubauer 2 Sept 1997
 */

int set_gt_counter(unsigned long count) {

    unsigned long shift_value;
    short j;
    uint32_t temp;

    for (j = 23; j >= 0; j--){
        shift_value = ((count >> j) & 0x01) == 1 ? SERDAT | SEN : SEN ;
        mtc_reg_write(MTCSerialReg,shift_value);
        mtc_reg_read(MTCSerialReg,&temp);
        mtc_reg_write(MTCSerialReg,temp | SHFTCLKGT); /* clock in SERDAT */
    }
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp | LOAD_ENGT); /* toggle load enable */
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp & ~LOAD_ENGT); /* toggle load enable */

    printsend("The GT counter has been loaded\n");
    return 0;
}



/*  
 * set_prescale(scale)- 
 * Set up prescaler for desired number of NHIT_100_LO triggers per PRESCALE trigger
 * scale is the number of NHIT_100_LO triggers per PRESCALE trigger
 * M. Neubauer 29 Aug 1997
 */

int set_prescale(unsigned short scale) {
    uint32_t temp;
    if (scale < 2) {
        printsend("Prescale value out of range\n");
        return -1;
    }
    mtc_reg_write(MTCScaleReg,~(scale-1));
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp | LOAD_ENPR);
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp & ~LOAD_ENPR); /* toggle load enable */

    printsend( "Prescaler set to %d NHIT_100_LO per PRESCALE\n", scale);
    return 0;
}     

/*  
 * set_pulser_frequency(freq)-
 * Set up the pulser for the desired frequency using the 50 MHz clock or SOFT_GT are source
 * freq is in Hz. Pass zero for SOFT_GT as source of the pulser
 * M. Neubauer 29 Aug 1997
 */
int set_pulser_frequency(float freq) {

    unsigned long pulser_value,
                  shift_value,
                  prog_freq;
    short j;
    uint32_t temp;

    if (freq <= 1.0e-3) {                                /* SOFT_GTs as pulser */
        pulser_value = 0;
        printsend("SOFT_GT is set to source the pulser\n");
    }
    else {
        pulser_value = (unsigned long)((781250 / freq) - 1);   /* 50MHz counter as pulser */
        prog_freq = (unsigned long)(781250/(pulser_value + 1));
        if ((pulser_value < 1) || (pulser_value > 167772216)) {
            printsend( "Pulser frequency out of range\n", prog_freq);
            return -1;
        }
    }

    for (j = 23; j >= 0; j--){
        shift_value = ((pulser_value >> j) & 0x01) == 1 ? SERDAT|SEN : SEN; 
        mtc_reg_write(MTCSerialReg,shift_value);
        mtc_reg_read(MTCSerialReg,&temp);
        mtc_reg_write(MTCSerialReg,temp | SHFTCLKPS); /* clock in SERDAT */
    }
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp | LOAD_ENPS);
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp & ~LOAD_ENPS); /* toggle load enable */

    //printsend( "Pulser frequency is set to %u Hz\n", prog_freq);
    return 0;

}

/*
 * set_pedestal_width(width)
 * set width of PED pulses
 * width is in 5 ns increments from 5 to 1275
 * M. Neubauer 1 Sept 1997
 */

int set_pedestal_width(unsigned short width) {

    uint32_t temp;
    unsigned long pwid_value;
    if ((width < 5) || (width > 1275)) {
        printsend("Pedestal width out of range\n");
        return -1;
    }
    pwid_value = ~(width / 5);

    mtc_reg_write(MTCPedWidthReg,pwid_value);
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg, temp | LOAD_ENPW);
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg, temp & ~LOAD_ENPW);

    //printsend( "Pedestal width is set to %u ns\n", width);
    return 0;

}


/*
 * set_coarse_delay(delay)
 * set coarse 10 ns delay of PED->PULSE_GT
 * delay is in ns
 * M. Neubauer 2 Sept 1997
 */

int set_coarse_delay(unsigned short delay) {

    uint32_t temp;
    unsigned long rtdel_value;

    if ((delay < 10) || (delay > 2550)) {
        printsend("Coarse delay value out of range\n");
        return -1;
    } 
    rtdel_value = ~(delay / 10);

    mtc_reg_write(MTCCoarseDelayReg,rtdel_value);
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg, temp | LOAD_ENPW);
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg, temp & ~LOAD_ENPW);

    //printsend( "Coarse delay is set to %u ns\n", delay);
    return 0;

} 

/*
 * set_fine_delay(delay)
 * set fine ~79 ps delay of PED->PULSE_GT
 * delay is in ns
 * M. Neubauer 2 Sept 1997
 */

float set_fine_delay(float delay) {

    uint32_t temp;
    int result;
    unsigned long addel_value;
    float addel_slope;   /* ADDEL value per ns of delay */
    float fdelay_set;


    ;
    pouch_request *response = pr_init();
    char get_db_address[500];
    sprintf(get_db_address,"http://%s:%s/%s/MTC_doc",DB_ADDRESS,DB_PORT,DB_BASE_NAME);
    pr_set_method(response, GET);
    pr_set_url(response, get_db_address);
    pr_do(response);
    if (response->httpresponse != 200){
        printsend("Unable to connect to database. error code %d\n",(int)response->httpresponse);
        return -1;
    }
    JsonNode *doc = json_decode(response->resp.data);
    addel_slope = (float) json_get_number(json_find_member(json_find_member(doc,"mtcd"),"fine_slope")); 
    addel_value = (unsigned long)(delay / addel_slope);
    //printsend( "%f\t%f\t%hu", delay, addel_slope, addel_value);
    if (addel_value > 255) {
        printsend("Fine delay value out of range\n");
        return -1.0;
    }

    mtc_reg_write(MTCFineDelayReg,addel_value);
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg, temp | LOAD_ENPW);
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg, temp & ~LOAD_ENPW);

    fdelay_set = (float)addel_value*addel_slope;
    //printsend( "Fine delay is set to %f ns\n", fdelay_set);
    json_delete(doc);
    pr_free(response);
    return fdelay_set;

}


/* reset_fifo()-
 * toggles fifo_reset bit to reset the memory controller and clears the BBA register
 *
 * M. Neubauer 2 Sept 1997
 *
 */

void reset_memory() {

    uint32_t temp;
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp | FIFO_RESET);
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp & ~FIFO_RESET);
    mtc_reg_write(MTCBbaReg,0x0);  

    printsend("The FIFO control has been reset and the BBA register has been cleared\n",
            view_fdset);

} 


// sets up the pedestal by setting pulser frequency, the lockout width, and
// the delays.
int setup_pedestals(float pulser_freq, uint32_t pedestal_width, /* in ns */
        uint32_t coarse_delay, uint32_t fine_delay)
{
    const uint16_t SP_LOCKOUT_WIDTH = DEFAULT_LOCKOUT_WIDTH;
    const uint32_t SP_GT_MASK = DEFAULT_GT_MASK;
    const uint32_t SP_PED_CRATE_MASK = DEFAULT_PED_CRATE_MASK;
    const uint32_t SP_GT_CRATE_MASK = DEFAULT_GT_CRATE_MASK;

    int result;
    float fdelay_set;
    result = 0;
    result += set_lockout_width(SP_LOCKOUT_WIDTH);
    if (!result){
        result += set_pulser_frequency(pulser_freq);
    }
    if (!result){
        result += set_pedestal_width(pedestal_width);
    }
    if (!result){
        result += set_coarse_delay(coarse_delay);
    }
    if (!result){
        fdelay_set = set_fine_delay(fine_delay);
    }
    enable_pulser();
    enable_pedestal();
    set_ped_crate_mask(SP_PED_CRATE_MASK);
    set_gt_crate_mask(SP_GT_CRATE_MASK);
    set_gt_mask(SP_GT_MASK);
    if (result != 0){
        printsend("new_daq: setup pedestals failed\n");
        return -1;
    }else{
        //printsend("new_daq: setup_pedestals complete\n");
        return 0;
    }
}

void enable_pulser()
{
    uint32_t temp;
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp | PULSE_EN);
    //printsend("Pulser enabled\n");
}

void disable_pulser()
{
    uint32_t temp;
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp & ~PULSE_EN);
    //printsend("Pulser disabled\n");
}


void enable_pedestal()
{
    uint32_t temp;
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp | PED_EN);
    //printsend("Pedestals enabled\n");
}


void disable_pedestal()
{
    uint32_t temp;
    mtc_reg_read(MTCControlReg,&temp);
    mtc_reg_write(MTCControlReg,temp & ~PED_EN);
    //printsend("Pedestals disabled\n");
}


uint32_t get_mtc_crate_mask(uint32_t crate_number)
{
    if ((crate_number > 25)){
        printsend("Illegal crate number (>25) in get_mtc_crate_mask\n");
        return -1;
    }

    return 0x1 << crate_number;
}

int send_softgt()
{
    // value doesnt matter since write strobe clocks a one-shot
    mtc_reg_write(MTCSoftGTReg,0x0); 
    return 0;
}

int prepare_mtc_pedestals(float pulser_freq, /* in Hz */
        uint16_t pedestal_width, uint16_t coarse_delay, uint16_t fine_delay /* in ns */)
{
    const uint16_t SP_LOCKOUT_WIDTH = DEFAULT_LOCKOUT_WIDTH;
    const uint32_t SP_GT_MASK = DEFAULT_GT_MASK;
    const uint32_t SP_PED_CRATE_MASK = DEFAULT_PED_CRATE_MASK;
    const uint32_t SP_GT_CRATE_MASK = DEFAULT_GT_CRATE_MASK;
    char err_str[100];

    int result;
    float fdelay_set;
    result = 0;

    result += set_lockout_width(SP_LOCKOUT_WIDTH);
    result += set_pulser_frequency(pulser_freq);
    result += set_pedestal_width(pedestal_width);
    result += set_coarse_delay(coarse_delay);
    fdelay_set = set_fine_delay(fine_delay);

    enable_pedestal();
    set_ped_crate_mask(SP_PED_CRATE_MASK);
    set_gt_crate_mask(SP_GT_CRATE_MASK);
    set_gt_mask(SP_GT_MASK);
    if (result != 0){
        sprintf(err_str,"prepare mtc pedestals failed\n");
        printsend(err_str);
        //SNO_printerr(5, MTC_FAC, err_str);
        return -1;
    }else{
        sprintf(err_str,"prepare_mtc_pedestals complete\n");
        printsend(err_str);
        //SNO_printerr(9, MTC_FAC, err_str);
        return 0;
    }
}

float set_gt_delay(float gtdel)
{
    int result;
    float offset_res, fine_delay, total_delay, fdelay_set;
    uint16_t cdticks, coarse_delay;

    offset_res = gtdel - (float)(18.35); //not "delay_offset" from db? //FIXME this is the way it is in old code
    cdticks = (uint16_t)(offset_res/10.0);
    coarse_delay = cdticks * 10;
    fine_delay = offset_res - ((float)cdticks*10.0);
    result = set_coarse_delay(coarse_delay);
    fdelay_set = set_fine_delay(fine_delay);
    total_delay = ((float) coarse_delay + fdelay_set + (float)(18.35));

    //SNOprintsend(9,MTC_FAC,"\tPULSE_GT total delay has been set to %f\n", total_delay);
    return total_delay;
}

int get_gt_count(uint32_t *count)
{
    mtc_reg_read(MTCOcGtReg, count);
    *count &= 0x00FFFFFF;
    return 0;
}

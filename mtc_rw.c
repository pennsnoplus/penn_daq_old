#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> 
#include <signal.h>

#include <string.h>
#include <stdlib.h> 
#include <stdio.h>

#include "penn_daq.h"
#include "net_util.h"
#include "mtc_rw.h"
#include "mtc_util.h"

int kill_SBC_process(){
    char bash_command[500];
    char kill_screen[500];
    char kill_orcareadout[500];
        sprintf(kill_orcareadout,"ssh -t daq@10.0.0.30 \"killall OrcaReadout >> /dev/null;  exit >> /dev/null\" >> /dev/null");
        sprintf(kill_screen,"ssh -t daq@10.0.0.30 \"killall screen >> /dev/null; screen -wipe >> /dev/null; exit >> /dev/null\" >> /dev/null");
    
	system(kill_orcareadout);
	system(kill_screen);
	//system(bash_command);

}

int connect_to_SBC(int portno, struct hostent *server){
    /*
       This function connects to the SBC/MTC, which will not connect automatically.
       Although there is a section of the main listener code where it will try to
       connect to an SBC/MTC if it receives a request, the mac_daq program will never
       receive this request. To connect to the SBC, call "connect_to_SBC" from the
       control terminal. This function ssh's into the SBC, starts the OrcaReadout program,
       and then tries (from the SBC/MTC ssh session) connect to mac_daq. After mac_daq
       receives that request, it accept()s and sets up the connection correctly.
     */

    // establish the socket

    char bash_command[500];
    char kill_screen[500];
    char kill_orcareadout[500];
    sprintf(kill_orcareadout,"ssh -t daq@10.0.0.30 \"killall OrcaReadout >> /dev/null;  exit >> /dev/null\" >> /dev/null");
    sprintf(kill_screen,"ssh -t daq@10.0.0.30 \"killall screen >> /dev/null; screen -wipe >> /dev/null; exit >> /dev/null\" >> /dev/null");
    sprintf(bash_command,"ssh -t daq@10.0.0.30 \"cd ORCA_dev >> /dev/null; screen -dmS orca ./OrcaReadout >> /dev/null; screen -d >> /dev/null; exit >> /dev/null\" >> /dev/null");

    int Nretrys=5;  // try 5 times
    int counter=0;
  while (sbc_is_connected==0 || counter<Nretrys){
      counter++;
    if (counter == 0){  
	sprintf(psb, "\n\t Trying to connect to the SBC\n");
	print_send(psb, view_fdset);
    }
    int ALREADY_CONNECTED=0;
    if(mtc_sock > 0){

	//sprintf(psb, "new_daq: Already connected to SBC/MTC (socket %d)\n", mtc_sock);
	//print_send(psb, view_fdset);
	ALREADY_CONNECTED=1;
	return -1;
    }
    mtc_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (mtc_sock <= 0){

	sprintf(psb, "new_daq: error opening SBC/MTC socket\n");
	print_send(psb, view_fdset);
	print_send("error connecting to SBC\n", view_fdset);
	return -1;
    }
    else if (!ALREADY_CONNECTED) {
	sprintf(psb, "Trying to connect with screen \n");
	print_send(psb, view_fdset);
	usleep(100);
	system(kill_orcareadout);
	usleep(100);
	system(kill_screen);
	usleep(100);
	system(bash_command);
	mtc_sock = socket(AF_INET, SOCK_STREAM, 0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
	    (char *)&serv_addr.sin_addr.s_addr,
	    server->h_length);
    serv_addr.sin_port = htons(portno);
    // make the connection
    if (connect(mtc_sock,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0){
	close(mtc_sock);
	FD_CLR(mtc_sock, &mtc_fdset);
	mtc_sock = 0;
	print_send("kk:error connecting to SBC\n", view_fdset);
	return -1;
    }
    // use FD_set so select() can be used
    FD_SET(mtc_sock, &all_fdset);
    FD_SET(mtc_sock, &mtc_fdset);
    if (mtc_sock > fdmax)     // keep track of the max
	fdmax = mtc_sock;

    // this is the test packet that ./OrcaReadout looks for     
    // the ppc mac swaps Bytes, the linux sbc does not.
    int32_t testWord=0x000DCBA;
    char *send_test= (char *)&testWord;

    int n;
    n = write(mtc_sock,send_test,4);

    print_send("\t Connected to SBC.\n", view_fdset);
    sbc_is_connected = 1;
  }
    return 0;
}


int mtc_reg_write(uint32_t address, uint32_t data){
    gPacket.cmdHeader.destination = 0x1;
    gPacket.cmdHeader.cmdID = 0x3;
    gPacket.cmdHeader.numberBytesinPayload  = 256+28;
    gPacket.numBytes = 256+28+16;
    SBC_VmeWriteBlockStruct *writestruct;
    writestruct = (SBC_VmeWriteBlockStruct *) gPacket.payload;
    writestruct->address = address + MTCRegAddressBase;
    writestruct->addressModifier = MTCRegAddressMod;
    writestruct->addressSpace = MTCRegAddressSpace;
    writestruct->unitSize = 4;
    writestruct->numItems = 1;
    writestruct++;
    uint32_t *data_ptr = (uint32_t *) writestruct;
    *data_ptr = data;
    do_mtc_cmd(&gPacket);
    return 0;
}

int mtc_reg_read(uint32_t address, uint32_t *data){
    uint32_t *result;
    gPacket.cmdHeader.destination = 0x1;
    gPacket.cmdHeader.cmdID = 0x2;
    gPacket.cmdHeader.numberBytesinPayload = 256+27;
    gPacket.numBytes = 256+27+16;
    SBC_VmeReadBlockStruct *readstruct;
    readstruct = (SBC_VmeReadBlockStruct *) gPacket.payload;
    readstruct->address = address + MTCRegAddressBase;
    readstruct->addressModifier = MTCRegAddressMod;
    readstruct->addressSpace = MTCRegAddressSpace;
    readstruct->unitSize = 4;
    readstruct->numItems = 1;
    do_mtc_cmd(&gPacket);
    result = (uint32_t *) (readstruct+1);
    *data = *result;
    return 0;
}

int multi_softgt(uint32_t num)
{
    // value doesnt matter since write strobe clocks a one-shot
    //mtc_reg_write(MTCSoftGTReg,0x0); 
    gPacket.cmdHeader.destination = 0x3;
    gPacket.cmdHeader.cmdID = 0x5;
    gPacket.cmdHeader.numberBytesinPayload  = 256+28;
    gPacket.numBytes = 256+28+16;
    SBC_VmeWriteBlockStruct *writestruct;
    writestruct = (SBC_VmeWriteBlockStruct *) gPacket.payload;
    writestruct->address = MTCSoftGTReg + MTCRegAddressBase;
    writestruct->addressModifier = MTCRegAddressMod;
    writestruct->addressSpace = MTCRegAddressSpace;
    writestruct->unitSize = 4;
    writestruct->numItems = num;
    writestruct++;
    uint32_t *data_ptr = (uint32_t *) writestruct;
    *data_ptr = 0x0;
    do_mtc_cmd(&gPacket);
    
    return 0;
}


int mtc_multi_write(uint32_t address, uint32_t data, int num_writes){
    gPacket.cmdHeader.destination = 0x1;
    gPacket.cmdHeader.cmdID = 0x3;
    gPacket.cmdHeader.numberBytesinPayload  = 256+28;
    gPacket.numBytes = 256+28+16;
    SBC_VmeWriteBlockStruct *writestruct;
    writestruct = (SBC_VmeWriteBlockStruct *) gPacket.payload;
    writestruct->address = address + MTCRegAddressBase;
    writestruct->addressModifier = MTCRegAddressMod;
    writestruct->addressSpace = 0xFF;
    writestruct->unitSize = 4;
    writestruct->numItems = num_writes;
    writestruct++;
    uint32_t *data_ptr = (uint32_t *) writestruct;
    int i;
    for (i=0;i<num_writes;i++){
    *(data_ptr+i) = data;
    }
    do_mtc_cmd(&gPacket);
    return 0;
}

int do_mtc_cmd(SBC_Packet *aPacket){
    int32_t numBytesToSend = aPacket->numBytes;
    char* p = (char*)aPacket;
    if(mtc_sock <= 0){
	print_send("not connected to the MTC/SBC\n", view_fdset);
	return -1;
    }
    int n;
    n = write(mtc_sock,p,numBytesToSend);
    if (n < 0) {
	print_send("ERROR writing to socket\n", view_fdset);
	return -1;
    }

    funcreadable_fdset = all_fdset;
    // remove all non-MTC/SBC fd's
    int x;
    for(x = 0; x<=fdmax; x++){
	if(FD_ISSET(x, &funcreadable_fdset)){
	    if(!FD_ISSET(x, &mtc_fdset)){
		FD_CLR(x, &funcreadable_fdset);
	    }
	}
    }
    int data; // flag for select() 
    set_delay_values(SECONDS, USECONDS);
    data=select(fdmax+1, &funcreadable_fdset, NULL, NULL, &delay_value);
    if(data==-1){
	print_send("error in select()", view_fdset);
	return -2;
    }
    else if(data==0){
	print_send("new_daq: timeout in select()\n", view_fdset);

	sprintf(psb, "new_daq: unable to receive response from MTC (socket %d)\n", mtc_sock);
	print_send(psb, view_fdset);
	return -3;
    }
    else{
	n = recv(mtc_sock,p,1500, 0 ); // 1500 should be?
	if (n < 0){
	    print_send("receive error in do_mtc_cmd\n", view_fdset);
	    return -1;
	}
	*aPacket = *(SBC_Packet*)p;
	return 0;
    }
} 

int get_caen_data(char *buffer)
{

    if (sbc_is_connected == 0){
	sprintf(psb,"SBC not connected.\n");
	print_send(psb, view_fdset);
	return -1;
    }
  
   printf("starting to go\n");

   SBC_crate_config configStruct; // for both MTC and CAEN
   int index=0;
   uint32_t dataId=0xBEEF0000;

   configStruct.total_cards=0x1;
   configStruct.card_info[index].hw_type_id           = 7;                 //should be unique 
   configStruct.card_info[index].hw_mask[0]           =  dataId;  //better be unique
   configStruct.card_info[index].slot                         = 0; //[self slot];
   configStruct.card_info[index].add_mod                      = 0x29; //[self addressModifier];
   configStruct.card_info[index].base_add                     = 0x00007000; //[self baseAddress];
   configStruct.card_info[index].deviceSpecificData[0] = 44; //reg[kMtcBbaReg].addressOffset;
   configStruct.card_info[index].deviceSpecificData[1] = 40; //reg[kMtcBwrAddOutReg].addressOffset;
   configStruct.card_info[index].deviceSpecificData[2] = 0x03800000; //[self memBaseAddress];
   configStruct.card_info[index].deviceSpecificData[3] = 0x09; //[self memAddressModifier];       
   configStruct.card_info[index].num_Trigger_Indexes = 0; //no children
   configStruct.card_info[index].next_Card_Index = index + 1;

   index=1; // CAEN time

   configStruct.total_cards++;
   configStruct.card_info[index].hw_type_id       = 6;//kCaen1720; //should be unique
   configStruct.card_info[index].hw_mask[0]       = dataId; //better be unique
   configStruct.card_info[index].slot                     = 0; //[self slot];
   configStruct.card_info[index].crate            = 0; //[self crateNumber];
   configStruct.card_info[index].add_mod          = 0x09; //[self addressModifier];
   configStruct.card_info[index].base_add         = 0x43210000; //[self baseAddress];
   configStruct.card_info[index].deviceSpecificData[0]    = 0x812C; //reg[kEventStored].addressOffset; //Status buffer
   configStruct.card_info[index].deviceSpecificData[1]        = 0x814C; //reg[kEventSize].addressOffset; // "next event size" address
   configStruct.card_info[index].deviceSpecificData[2]        = 0x0000; //reg[kOutputBuffer].addressOffset; // fifo Address
   configStruct.card_info[index].deviceSpecificData[3]        = 0x0C; // fifo Address Modifier (A32 MBLT supervisory)
   configStruct.card_info[index].deviceSpecificData[4]        = 0xFFC; // fifo Size
   configStruct.card_info[index].deviceSpecificData[5]        = 0; //location; location =  (([self crateNumber]&0x01e)<<21) | (([self slot]& 0x0000001f)<<16);
   configStruct.card_info[index].deviceSpecificData[6]        = 0xEF00; //reg[kVMEControl].addressOffset; // VME Control address
   configStruct.card_info[index].deviceSpecificData[7]        = 0xEF1C; //reg[kBLTEventNum].addressOffset; // Num of BLT events address

   int isFixedSize=1; // ????

   unsigned sizeOfEvent = 1000; //FIXME  number of uint32_t for DMA transfer
   configStruct.card_info[index].deviceSpecificData[8]    = sizeOfEvent;  
   configStruct.card_info[index].num_Trigger_Indexes      = 0;
   configStruct.card_info[index].next_Card_Index  = index+1;      

   SBC_Packet aPacket;

   aPacket.cmdHeader.destination = 0x1; //kSBC_Process; //FIXME
   aPacket.cmdHeader.cmdID   =0x4;  // kSBC_LoadConfig; //FIXME
   aPacket.cmdHeader.numberBytesinPayload  = sizeof(SBC_crate_config); //FIXME
   memcpy(aPacket.payload,&configStruct,sizeof(SBC_crate_config));

   printf("doing a mtc_cmd\n");
   printf("the payload is %s\n",aPacket.payload);
   do_mtc_cmd(&aPacket);
   printf("did it work?\n");

   return 0;

} 

int do_mtc_xilinx_cmd(SBC_Packet *aPacket){
    int32_t numBytesToSend = aPacket->numBytes;
    char* p = (char*)aPacket;
    SBC_Packet bPacket;
    char* q = (char*)&bPacket;
    if(mtc_sock <= 0){
	print_send("not connected to the MTC/SBC\n", view_fdset);
	return -1;
    }
    int n;
    n = write(mtc_sock,p,numBytesToSend);
    if (n < 0) {
	print_send("ERROR writing to socket\n", view_fdset);
	return -1;
    }

    funcreadable_fdset = all_fdset;
    // remove all non-MTC/SBC fd's
    int x;
    for(x = 0; x<=fdmax; x++){
	if(FD_ISSET(x, &funcreadable_fdset)){
	    if(!FD_ISSET(x, &mtc_fdset)){
		FD_CLR(x, &funcreadable_fdset);
	    }
	}
    }
    int data; // flag for select() 
    set_delay_values(SECONDS, USECONDS);
    data=select(fdmax+1, &funcreadable_fdset, NULL, NULL, &delay_value);
    if(data==-1){
	print_send("error in select()", view_fdset);
	return -2;
    }
    else if(data==0){
	print_send("new_daq: timeout in select()\n", view_fdset);

	sprintf(psb, "new_daq: unable to receive response from MTC (socket %d)\n", mtc_sock);
	print_send(psb, view_fdset);
	return -3;
    }
    else{
	n = recv(mtc_sock,p,numBytesToSend+10, 0 ); // 1500 should be?
	numBytesToSend-=n;
	while(numBytesToSend>0){
	    n = recv(mtc_sock,q,numBytesToSend+10, 0 ); // 1500 should be?
	    numBytesToSend-=n;
	}
	if (n < 0){
	    print_send("receive error in do_mtc_cmd\n", view_fdset);
	    return -1;
	}
	*aPacket = *(SBC_Packet*)p;
	return 0;
    }
} 

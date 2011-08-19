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


/** sbc_control allows remote control of the OrcaReadout process on the SBC and
  * manages penn_daq's connection to it. The function has three actions:
  *     connect (-c): starts OrcaReadout and connects to it
  *     reconnect (-r): closes any existing OrcaReadout connection then does a
  *                     connect
  *     kill (-k): kills the remote OrcaReadout process
 */
int sbc_control(int portno, struct hostent *server, char *buffer){
    int sbc_action = -1;
    char identity_file[100] = "";
    char base_cmd[100];
    sprintf(base_cmd, "ssh %s@%s", SBC_USER, SBC_SERVER);

    // parse command-line arguments
    char* words;
    char* words2;
    words = strtok(buffer, " ");
    while (words != NULL) {
        if (words[0] == '-') {
            if (words[1] == 'i') {
                words2 = strtok(NULL, " ");
                sprintf(identity_file, "%s", words2);
            }
            if (words[1] == 'c')
                sbc_action = 0;
            if (words[1] == 'r')
                sbc_action = 1;
            if (words[1] == 'k')
                sbc_action = 2;
            if (words[1] == 'h') {
                printsend("Usage: sbc_control "
                          "[-c (connect)|-k (kill)|-r (reconnect)] "
                          "[-i identity_file]\n");
                return 0;
            }
        }
        words = strtok(NULL, " ");
    }

    // no action argument -- fail
    if (sbc_action < 0) {
        printsend("sbc_control: Must specify -c (connect), -k (kill), "
                  "or -r (reconnect)\n");
        return 0;
    }

    // append ssh identity file if necessary
    if (strcmp(identity_file,"") != 0)
        sprintf(base_cmd, "%s -i %s", base_cmd, identity_file);

    // close socket and stop remote OrcaReadout service for kill or reconnect
    if (sbc_action == 1 || sbc_action == 2) {
        if(mtc_sock > 0) {
            close(mtc_sock);
            FD_CLR(mtc_sock, &mtc_fdset);
            mtc_sock = 0;
        }
        char kill_cmd[500];
        sprintf(kill_cmd, "%s service orcareadout stop", base_cmd);
        printsend("sbc_control: Stopping remote OrcaReadout process\n");
        system(kill_cmd);
        if (sbc_action == 2)
            return 0;
    }

    // start OrcaReadout and try to connect to it
    printsend("sbc_control: Connecting to the SBC\n");

    char start_cmd[500];
    sprintf(start_cmd, "%s service orcareadout start", base_cmd);
    printsend("sbc_control: Starting remote OrcaReadout process\n");
    system(start_cmd);

    if (mtc_sock > 0) {
        printsend("sbc_control: Already connected to SBC (socket %d)\n", mtc_sock);
        return -1;
    }

    mtc_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (mtc_sock <= 0) {
        printsend("sbc_control: Error opening SBC socket\n");
        return -1;
    }

    bzero((char*) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char*) server->h_addr, 
          (char*) &serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(portno);

    // make the connection
    if (connect(mtc_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr))<0) {
        close(mtc_sock);
        FD_CLR(mtc_sock, &mtc_fdset);
        mtc_sock = 0;
        printsend("sbc_control: Error connecting to SBC\n");
        return -1;
    }

    // use FD_set so select() can be used
    FD_SET(mtc_sock, &all_fdset);
    FD_SET(mtc_sock, &mtc_fdset);
    if (mtc_sock > fdmax) // keep track of the max?
        fdmax = mtc_sock;

    // test packet for OrcaReadout to infer endianness
    int32_t testWord = 0x000DCBA;
    char *send_test = (char*) &testWord;
    int n = write(mtc_sock, send_test, 4);

    printsend("sbc_connect: Connected to SBC\n");
    sbc_is_connected = 1;

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
        printsend("not connected to the MTC/SBC\n");
        return -1;
    }
    int n;
    n = write(mtc_sock,p,numBytesToSend);
    if (n < 0) {
        printsend("ERROR writing to socket\n");
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
        printsend("error in select()");
        return -2;
    }
    else if(data==0){
        printsend("new_daq: timeout in select()\n");

        printsend( "new_daq: unable to receive response from MTC (socket %d)\n", mtc_sock);
        return -3;
    }
    else{
        n = recv(mtc_sock,p,1500, 0 ); // 1500 should be?
        if (n < 0){
            printsend("receive error in do_mtc_cmd\n");
            return -1;
        }
        *aPacket = *(SBC_Packet*)p;
        return 0;
    }
} 

int get_caen_data(char *buffer)
{

    if (sbc_is_connected == 0){
        printsend("SBC not connected.\n");
        return -1;
    }

   printsend("starting to go\n");

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

   printsend("doing a mtc_cmd\n");
   printsend("the payload is %s\n",aPacket.payload);
    do_mtc_cmd(&aPacket);
   printsend("did it work?\n");

    return 0;

} 

int do_mtc_xilinx_cmd(SBC_Packet *aPacket){
    int32_t numBytesToSend = aPacket->numBytes;
    char* p = (char*)aPacket;
    SBC_Packet bPacket;
    char* q = (char*)&bPacket;
    if(mtc_sock <= 0){
        printsend("not connected to the MTC/SBC\n");
        return -1;
    }
    int n;
    n = write(mtc_sock,p,numBytesToSend);
    if (n < 0) {
        printsend("ERROR writing to socket\n");
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
        printsend("error in select()");
        return -2;
    }
    else if(data==0){
        printsend("new_daq: timeout in select()\n");

        printsend( "new_daq: unable to receive response from MTC (socket %d)\n", mtc_sock);
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
            printsend("receive error in do_mtc_cmd\n");
            return -1;
        }
        *aPacket = *(SBC_Packet*)p;
        return 0;
    }
} 

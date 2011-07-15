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


/* int connect_to_SBC (3.J) */
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

    /*
       note- 
       This still isn't actually working. We're not really sure why. It probably
       has to do with the version of linux running on the sbc not allowing
       screen connections over ssh or something. Also, not stored ssh keys.
       - (Peter Downs, 8/10/10)

    // run startorca.sh, a script which will start the OrcaReadout program on the SBC
    char bash_command[100];
    sprintf(bash_command, "./startorca.sh daq 10.0.0.20 %s", SBC_PORT);
    system(bash_command);


    sprintf(psb, "new_daq: running ./startorca.sh to try to start the OrcaReadout program\n");
    print_send(psb, view_fdset);
     */
    // establish the socket

    char bash_command[1000];
    sprintf(bash_command,"ssh -t daq@10.0.0.30 \"killall OrcaReadout; killall screen; screen -wipe; cd ORCA_dev; screen -dmS orca ./OrcaReadout; screen -d; exit\"");

    sprintf(psb, "\n\t Trying to connect to the SBC\n");
    sprintf(psb + strlen(psb), "\t Is OrcaReadOut running?\n");
    print_send(psb, view_fdset);
    if(mtc_sock > 0){

        sprintf(psb, "new_daq: Already connected to SBC/MTC (socket %d)\n", mtc_sock);
        print_send(psb, view_fdset);
        return -1;
    }
    mtc_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (mtc_sock <= 0){

        sprintf(psb, "new_daq: error opening SBC/MTC socket\n");
        print_send(psb, view_fdset);
        print_send("error connecting to SBC\n", view_fdset);
        return -1;
    }
    else {
       printsend("trying to connect with screen!\n");
        system(bash_command);
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
        print_send("was the SBC connected? Error!\n", view_fdset);
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

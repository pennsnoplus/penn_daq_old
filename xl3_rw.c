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
#include "xl3_rw.h"
#include "cald_test.h"



int receive_cald(int xl3_num, uint16_t *point_buf, uint16_t *adc_buf)
{
    funcreadable_fdset = all_fdset;
    // remove all non-xl3 fd's so that they aren't read from
    int x,n,i;
    for(x = 0; x<=fdmax; x++)
        if(FD_ISSET(x, &funcreadable_fdset))
            if(!FD_ISSET(x, &xl3_fdset))
                FD_CLR(x, &funcreadable_fdset);


    int data; // flag for select()
    XL3_Packet bPacket, *aPacket;
    cald_response_t *response;
    aPacket = &bPacket;
    int point_count = 0;
    int current_slot = 0;
    int current_point = 0;
    char* p = (char*)aPacket;
    while(1){ // we loop, printing out messages, until we get a non-message packet. Hopefully we dont error or time out before then
        memset(aPacket, '\0', MAX_PACKET_SIZE);
        set_delay_values(5, 1000);
        data=select(fdmax+1, &funcreadable_fdset, NULL, NULL, &delay_value);
        if (data == -1){
            print_send("wait_while_messages: Error in select()\n", view_fdset);
            return -1;
        }else if (data == 0){
            print_send("wait_while_messages: timed out\n", view_fdset);
            return -2;
        }else if (FD_ISSET(connected_xl3s[xl3_num], &funcreadable_fdset)){
            n = recv(connected_xl3s[xl3_num],p,MAX_PACKET_SIZE, 0);
            if(n <= 0){
                sprintf(psb, "wait_while_messages: unable to receive response from xl3 #%d (socket %d)\n",
                        xl3_num, connected_xl3s[xl3_num]);
                print_send(psb, view_fdset);
                return -3;
            }
            // We've successfully gotten a packet from XL3, what is it?
            *aPacket = *(XL3_Packet*)p;
            SwapShortBlock(&(aPacket->cmdHeader.packet_num),1);
            if (aPacket->cmdHeader.packet_type == MESSAGE_ID){
                print_send(aPacket->payload, view_fdset); // we loop around again
            }else if(aPacket->cmdHeader.packet_type == CALD_RESPONSE_ID){
                // we now parse the packet
                response = (cald_response_t *) aPacket->payload;
                SwapShortBlock(response,501);
                if (response->slot != current_slot){
                    current_slot = response->slot;
                    current_point = 0;
                }
                for (i=0;i<100;i++){
                    if (response->point[i] != 0){
                        point_buf[current_slot*2000+current_point] = response->point[i];
                        adc_buf[current_slot*8000+current_point*4] = response->adc0[i];
                        adc_buf[current_slot*8000+current_point*4+1] = response->adc1[i];
                        adc_buf[current_slot*8000+current_point*4+2] = response->adc2[i];
                        adc_buf[current_slot*8000+current_point*4+3] = response->adc3[i];
                        //printf("slot %d: point %d, %4u %4u %4u %4u\n",current_slot,response->point[i],response->adc0[i],response->adc1[i],response->adc2[i],response->adc3[i]);
                        current_point++;
                        point_count++;
                    }
                }
            }else if(aPacket->cmdHeader.packet_type == CALD_TEST_ID){
                // we must be finished
                return point_count;
            }else{
                int r;
                printf("wait_while_messages: unknown packet:\n");
                for (r=0;r<5;r++)
                    printf("wait_while_messages: %08x ",*(uint32_t *) (aPacket->payload+4*r));
                printf("\n");
            }
        }else{	// if the data coming in was not from an xl3
            int z;
            for(z = 0; z <= fdmax; z ++){	// loop over all the file_descriptors
                if(FD_ISSET(z, &funcreadable_fdset)){	// if the fd is readable, take the data
                    n = recv(z, p, 2444, 0);
                    if(n >= 0){
                        sprintf(psb, "received data from socket %d\n", z);
                        print_send(psb, view_fdset);
                    }
                    if(FD_ISSET(z, &cont_fdset)){	// if it's a control socket, send back 'busy'
                        n = write(z, "new_daq: busy, did not process command\n", 39);
                        sprintf(psb, "sent %d bytes back to socket %d\n", n, z);
                        print_send(psb, view_fdset);
                    }
                }
            }
            return -4;
        }
    }
    return -5;
}

/*

   int sleep_with_messages(int xl3_num,char *history)
   {
//xl3_num--;
funcreadable_fdset = all_fdset;
// remove all non-xl3 fd's so that they aren't read from
int x,n;
for(x = 0; x<=fdmax; x++)
if(FD_ISSET(x, &funcreadable_fdset))
if(!FD_ISSET(x, &xl3_fdset))
FD_CLR(x, &funcreadable_fdset);


int data; // flag for select()
XL3_Packet bPacket, *aPacket;
aPacket = &bPacket;
int message_count = 0;
char* p = (char*)aPacket;
while(1){ // we loop, printing out messages, until we error or time out. will hopefully time out after all messages are done 
memset(aPacket, '\0', MAX_PACKET_SIZE);
set_delay_values(0, 1000);
data=select(fdmax+1, &funcreadable_fdset, NULL, NULL, &delay_value);
if (data == -1){
print_send("new_daq: error in select()\n", view_fdset);
return 1;
}else if (data == 0){
return message_count;
}else if (FD_ISSET(connected_xl3s[xl3_num], &funcreadable_fdset)){
n = recv(connected_xl3s[xl3_num],p,MAX_PACKET_SIZE, 0);
if(n <= 0){
sprintf(psb, "new_daq: sleep_with_messages, unable to receive response from xl3 #%d (socket %d)\n",
xl3_num, connected_xl3s[xl3_num]);
print_send(psb, view_fdset);
return 1;
}
// We've successfully gotten a packet from XL3, what is it?
 *aPacket = *(XL3_Packet*)p;
 SwapShortBlock(&(aPacket->cmdHeader.packet_num),1);
 if (aPacket->cmdHeader.packet_type == MESSAGE_ID){
 if (strlen(history) < 45000){
 sprintf(history+strlen(history),"%s",aPacket->payload);
 }
 print_send(aPacket->payload, view_fdset); // we loop around again
 message_count++;
 }else{
 int r;
 for (r=0;r<5;r++)
 printf("sleep_with_messages: %08x ",*(uint32_t *) (aPacket->payload+4*r));
 printf("\n");
 }
 }else{	// if the data coming in was not from an xl3
 int z;
 for(z = 0; z <= fdmax; z ++){	// loop over all the file_descriptors
 if(FD_ISSET(z, &funcreadable_fdset)){	// if the fd is readable, take the data
 n = recv(z, p, 2444, 0);
 if(n >= 0){
 sprintf(psb, "received data from socket %d\n", z);
 print_send(psb, view_fdset);
 }
 if(FD_ISSET(z, &cont_fdset)){	// if it's a control socket, send back 'busy'
 n = write(z, "new_daq: busy, did not process command\n", 39);
 sprintf(psb, "sent %d bytes back to socket %d\n", n, z);
 print_send(psb, view_fdset);
 }
 }
 }
 return 1;
 }
 }
 return 1;
}
*/

int receive_data(int num_cmds, int packet_num, int xl3_num, uint32_t *buffer)
{
    //xl3_num -= 1;	// compensate for the fact that arrays start at 0, not 1
    if (connected_xl3s[xl3_num] == -999){
        print_send("Invalid crate number: socket value is NULL\n", view_fdset);
        return -1;
    }    
    XL3_Packet bPacket, *aPacket;
    aPacket = &bPacket;
    char* p = (char*)aPacket;
    int i;
    int current_num = 0;
    // First we check if there's any of our acks in the buffer
    if (multifc_buffer_full != 0){
        for (i=0;i<multifc_buffer.howmany;i++){
            if (multifc_buffer.cmd[i].packet_num == packet_num){
                if (multifc_buffer.cmd[i].cmd_num == current_num){
                    if (multifc_buffer.cmd[i].flags == 0){
                        *(buffer + current_num) = multifc_buffer.cmd[i].data;
                    }else{
                        print_send("There was a bus error in results\n",view_fdset);
                    }
                    current_num++;
                }else{
                    print_send("Results out of order?\n",view_fdset);
                    return -1;
                }
                if (i == (multifc_buffer.howmany-1)){
                    multifc_buffer_full = 0;
                    multifc_buffer.howmany = 0;
                }
            }
        }
    }
    MultiFC commands;
    funcreadable_fdset = all_fdset;
    // remove all non-xl3 fd's so that they aren't read from
    int x;
    for(x = 0; x<=fdmax; x++)
        if(FD_ISSET(x, &funcreadable_fdset))
            if(!FD_ISSET(x, &xl3_fdset))
                FD_CLR(x, &funcreadable_fdset);


    int data; // flag for select()
    int n;
    while(1){ // we loop until we get the right response or error out
        memset(aPacket, '\0', MAX_PACKET_SIZE);
        set_delay_values(SECONDS, USECONDS);
        data=select(fdmax+1, &funcreadable_fdset, NULL, NULL, &delay_value);
        if (data == -1){
            print_send("new_daq: error in select()\n", view_fdset);
            return -2;
        }else if (data == 0){
            print_send("new_daq: timeout in select()\n", view_fdset);
            return -3;
        }else if (FD_ISSET(connected_xl3s[xl3_num], &funcreadable_fdset)){
            n = recv(connected_xl3s[xl3_num],p,MAX_PACKET_SIZE, 0);
            if(n <= 0){
                sprintf(psb, "new_daq: receive_data: unable to receive response from xl3 #%d (socket %d)\n",
                        xl3_num, connected_xl3s[xl3_num]);
                print_send(psb, view_fdset);
                return -1;
            }
            // We've successfully gotten a packet from XL3, what is it?
            *aPacket = *(XL3_Packet*)p;
            SwapShortBlock(&(aPacket->cmdHeader.packet_num),1);
            if (aPacket->cmdHeader.packet_type == MESSAGE_ID)
                printf("%s",aPacket->payload);
            //print_send(aPacket->payload, view_fdset); // we loop around again
            else if (aPacket->cmdHeader.packet_type == CMD_ACK_ID){
                // read in cmds and add to buffer, then loop again unless read in num_cmds cmds
                commands = *(MultiFC *) aPacket->payload;
                SwapLongBlock(&(commands.howmany),1);
                for (i=0;i<MAX_ACKS_SIZE;i++){
                    SwapLongBlock(&(commands.cmd[i].cmd_num),1);
                    SwapShortBlock(&(commands.cmd[i].packet_num),1);
                    SwapLongBlock(&(commands.cmd[i].data),1);
                }
                for (i=0;i<commands.howmany;i++){
                    if (commands.cmd[i].packet_num == packet_num){
                        if (commands.cmd[i].cmd_num == current_num){
                            if (commands.cmd[i].flags == 0){
                                //printf("read in %08x\n",commands.cmd[i].data);
                                *(buffer + current_num) = commands.cmd[i].data;
                            }else{
                                sprintf(psb,"Bus error in receive data, %02x, command # %d\n",commands.cmd[i].flags,i);
                                print_send(psb,view_fdset);
                            }
                            current_num++;
                        }else{
                            print_send("Results out of order?\n",view_fdset);
                            return -1;
                        }
                    }else{
                        // we have results from a different packet, so buffer it and let the next call to this function get it
                        if (multifc_buffer_full == 0){
                            multifc_buffer_full = 1;
                            multifc_buffer.cmd[multifc_buffer.howmany] = commands.cmd[i];
                            multifc_buffer.howmany++;
                        }else{
                            print_send("Result buffer already full, packets mixed up?\n",view_fdset);
                            return -1;
                        }
                    }
                }
                if (current_num == num_cmds){
                    return 0; // we got all our results, otherwise go around again
                }
            }else{
                //printf("packet type was %02x, number %02x\n",aPacket->cmdHeader.packet_type,aPacket->cmdHeader.packet_num);
                //printf("%d bytes: .%s.\n",n,aPacket->payload);
                int r;
                for (r=0;r<5;r++)
                    printf("receive_data: %08x ",*(uint32_t *) (aPacket->payload+4*r));
                printf("\n");
            }
        }else{	// if the data coming in was not from an xl3
            int z;
            for(z = 0; z <= fdmax; z ++){	// loop over all the file_descriptors
                if(FD_ISSET(z, &funcreadable_fdset)){	// if the fd is readable, take the data
                    n = recv(z, p, 2444, 0);
                    if(n >= 0){
                        sprintf(psb, "received data from socket %d\n", z);
                        print_send(psb, view_fdset);
                    }
                    if(FD_ISSET(z, &cont_fdset)){	// if it's a control socket, send back 'busy'
                        n = write(z, "new_daq: busy, did not process command\n", 39);
                        sprintf(psb, "sent %d bytes back to socket %d\n", n, z);
                        print_send(psb, view_fdset);
                    }
                }
            }
            return -1;
        }
    }
    return -1;
}


int do_xl3_cmd(XL3_Packet *aPacket, int xl3_num){
    int result;
    uint8_t this_packet_type = aPacket->cmdHeader.packet_type;

    //xl3_num -= 1;	// compensate for the fact that arrays start at 0, not 1
    if (connected_xl3s[xl3_num] == -999){
        print_send("Invalid crate number: socket value is NULL\n", view_fdset);
        return -1;
    }    
    //int32_t numBytesToSend = aPacket->numBytes;
    int32_t numBytesToSend = 1024;
    int n;
    aPacket->cmdHeader.packet_num = command_number;
    char* p = (char*)aPacket;
    SwapShortBlock(&(aPacket->cmdHeader.packet_num),1);
    n = write(connected_xl3s[xl3_num], p, numBytesToSend);
    if (n < 0) {
        print_send("ERROR writing to socket\n", view_fdset);
        return -1;
    }else{
        if (this_packet_type == CALD_TEST_ID)
            cald_test_file = fopen("cald_test.log", "w");
        command_number++;
        funcreadable_fdset = all_fdset;
        // remove all non-xl3 fd's so that they aren't read from
        int x;
        for(x = 0; x<=fdmax; x++)
            if(FD_ISSET(x, &funcreadable_fdset))
                if(!FD_ISSET(x, &xl3_fdset))
                    FD_CLR(x, &funcreadable_fdset);


        int data; // flag for select()
        while(1){ // we loop until we get the right response or error out
            memset(aPacket, '\0', MAX_PACKET_SIZE);
            if (this_packet_type == ZERO_DISCRIMINATOR_ID)
                set_delay_values(60,0);
            else
                set_delay_values(SECONDS, USECONDS);
            data=select(fdmax+1, &funcreadable_fdset, NULL, NULL, &delay_value);
            if (data == -1){
                print_send("new_daq: error in select()\n", view_fdset);
                return -2;
            }else if (data == 0){
                print_send("new_daq: timeout in select()\n", view_fdset);
                return -3;
            }else if (FD_ISSET(connected_xl3s[xl3_num], &funcreadable_fdset)){
                n = recv(connected_xl3s[xl3_num],p,MAX_PACKET_SIZE, 0);
                if(n <= 0){
                    sprintf(psb, "new_daq: do_xl3_cmd: packet_type %02x, unable to receive response from xl3 #%d (socket %d)\n",
                            this_packet_type,xl3_num, connected_xl3s[xl3_num]);
                    print_send(psb, view_fdset);
                    return -1;
                }
                // We've successfully gotten a packet from XL3, what is it?
                *aPacket = *(XL3_Packet*)p;
                SwapShortBlock(&(aPacket->cmdHeader.packet_num),1);
                if (aPacket->cmdHeader.packet_type == MESSAGE_ID){
                    if (this_packet_type == CALD_TEST_ID){
                        fprintf(cald_test_file, "%s", aPacket->payload);
                    }
                    printf("%s",aPacket->payload);

                    //print_send(aPacket->payload, view_fdset); // we loop around again
                }else if (aPacket->cmdHeader.packet_type == this_packet_type){
                    if (this_packet_type == CALD_TEST_ID)
                        fclose(cald_test_file);
                    if (aPacket->cmdHeader.packet_num == ((command_number-1)%65536))
                        return 0;
                    else{
                        sprintf(psb, "wrong command number?? Expected %d, got %d.\n",command_number-1,aPacket->cmdHeader.packet_num);
                        print_send(psb, view_fdset);
                        return -4;
                    }
                }else if (aPacket->cmdHeader.packet_type == MEGA_BUNDLE_ID){
                    store_mega_bundle(aPacket->cmdHeader.num_bundles);
                }else{
                    //printf("packet type was %02x, number %02x\n",aPacket->cmdHeader.packet_type,aPacket->cmdHeader.packet_num);
                    //printf("%d bytes: .%s.\n",n,aPacket->payload);
                    int r;
                    for (r=0;r<5;r++)
                        printf("do_xl3_cmd: %08x ",*(uint32_t *) (aPacket->payload+4*r));
                    printf("\n");
                }
            }else{	// if the data coming in was not from an xl3
                int z;
                for(z = 0; z <= fdmax; z ++){	// loop over all the file_descriptors
                    if(FD_ISSET(z, &funcreadable_fdset)){	// if the fd is readable, take the data
                        n = recv(z, p, 2444, 0);
                        if(n >= 0){
                            sprintf(psb, "received data from socket %d\n", z);
                            print_send(psb, view_fdset);
                        }
                        if(FD_ISSET(z, &cont_fdset)){	// if it's a control socket, send back 'busy'
                            n = write(z, "new_daq: busy, did not process command\n", 39);
                            sprintf(psb, "sent %d bytes back to socket %d\n", n, z);
                            print_send(psb, view_fdset);
                        }
                    }
                }
                return -1;
            }
        }
        return -1;
    }
}


int do_xl3_cmd_no_response(XL3_Packet *aPacket, int xl3_num){
    int result;
    //xl3_num -= 1;	// compensate for the fact that arrays start at 0, not 1
    if (connected_xl3s[xl3_num] == -999){
        print_send("Invalid crate number: socket value is NULL\n", view_fdset);
        return -1;
    }    
    //int32_t numBytesToSend = aPacket->numBytes;
    int32_t numBytesToSend = 1024;
    int n;
    aPacket->cmdHeader.packet_num = command_number;
    char* p = (char*)aPacket;
    SwapShortBlock(&(aPacket->cmdHeader.packet_num),1);
    n = write(connected_xl3s[xl3_num], p, numBytesToSend);
    if (n < 0) {
        print_send("ERROR writing to socket\n", view_fdset);
        return -1;
    }
    return 0;
}	

int xl3_rw(uint32_t address, uint32_t data, uint32_t *result, int crate_num){
    XL3_Packet tempPacket;
    tempPacket.cmdHeader.packet_type = SINGLE_CMD_ID;
    FECCommand *command;
    command = (FECCommand *) tempPacket.payload;
    command->flags = 0x0;
    command->cmd_num = 0x0;
    command->packet_num = 0x0;
    command->address = address;
    command->data = data;
    //    SwapLongBlock(&(command->packet_num),1);
    //   SwapShortBlock(&(command->cmd_num),1);
    SwapLongBlock(&(command->data),1);
    SwapLongBlock(&(command->address),1);
    do_xl3_cmd(&tempPacket, crate_num);
    *result = command->data;
    SwapLongBlock(result,1);
    return command->flags;
}


int read_from_tut(char* result){
    funcreadable_fdset = all_fdset;
    // remove all non-controller fd's so that they aren't read from
    int x;
    for(x = 0; x<=fdmax; x++)
        if(FD_ISSET(x, &funcreadable_fdset))
            if(!FD_ISSET(x, &cont_fdset))
                FD_CLR(x, &funcreadable_fdset);


    int data; // flag for select()
    int n;
    char p[MAX_PACKET_SIZE];
    memset(p, '\0', MAX_PACKET_SIZE);
    while(1){ // we loop until we get the right response or error out
        data=select(fdmax+1, &funcreadable_fdset, NULL, NULL, NULL);
        if (data == -1){
            print_send("new_daq: error in select()\n", view_fdset);
            return -2;
        }else if (data == 0){
            print_send("new_daq: timeout in select()\n", view_fdset);
            //return -3;
        }else{ 
            int z;
            for(z = 0; z <= fdmax; z ++){	// loop over all the file_descriptors
                if(FD_ISSET(z, &funcreadable_fdset)){	// if the fd is readable, take the data
                    if(FD_ISSET(z, &cont_fdset)){	// if it's a control socket
                        n = recv(z, p, 2444, 0);
                        if(n > 0){
                            sprintf(result,"%s",p);
                            //sprintf(psb, "controller says \"%s\"\n", p);
                            //print_send(psb, view_fdset);
                            if(FD_ISSET(z, &writeable_fdset)){
                                write(z, COMACK, strlen(COMACK));
                                return 0;
                            }else{
                                print_send("could not send response - check connection\n",
                                        view_fdset);
                                return 0;
                            }
                        }else{
                            if (n == 0){
                                close_con(z, "CONTROLLER");
                                return -1;
                            }
                            // if there's an error in the connection, throw an error and close the connection
                            else if ( n < 0){
                                print_send("receive error: receiving controller data\n", view_fdset);
                                close_con(z, "CONTROLLER");
                                return -1;
                            }

                        }
                    }
                }
            }//end for all file_descriptors
        }
    }//end loop
    return 0;
}




#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "penn_daq.h"
#include "fec_util.h"
//#include "pouch.h"
//#include "json.h"
#include "net_util.h"
#include "cald_test.h"


int cald_test(char *buffer)
{
    int errors;
    uint32_t upper, lower, num_points, samples;
    uint32_t crate_number, slot_mask;
    int i;

    //defaults
    lower = 3000; // caldac start
    upper = 3550; // caldac end
    num_points = 550;
    samples = 1; // number of samples per point
    int total_points;
    int num_slots = 0;

    crate_number = 2;
    slot_mask = 0x2000;

    int update_db = 0;
    int final_test = 0;
    char ft_ids[16][50];

    char *words,*words2;

    // lets get the parameters from command line
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c'){
		words2 = strtok(NULL, " ");
		crate_number = atoi(words2);
	    }else if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		slot_mask = strtoul(words2,(char**)NULL,16);

	    }else if (words[1] == 'u'){
		words2 = strtok(NULL, " ");
		upper = strtoul(words2,(char**)NULL,16);
	    }else if (words[1] == 'l'){
		words2 = strtok(NULL, " ");
		lower = strtoul(words2,(char**)NULL,16);
	    }else if (words[1] == 'n'){
		words2 = strtok(NULL, " ");
		num_points = atoi(words2);
	    }else if (words[1] == 'S'){
		words2 = strtok(NULL, " ");
		samples = strtoul(words2,(char**)NULL,16);
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
		sprintf(psb,"Usage: cald_test -c [crate num] -s [slot mask (hex)]"
			" -u [upper limit] -l [lower limit] -n [num points] -S [samples] -d (update database)\n");
		print_send(psb,view_fdset);
		return -1;
	    }
	}
	words = strtok(NULL, " ");
    }	

    if (num_points*samples > 2000){
	printf("not enough space! ask Richie to increase the malloc size!\n");
	return -1;
    }

    for (i=0;i<16;i++){
	if ((0x1<<i) & slot_mask){
	    num_slots++;
	}
    }

    XL3_Packet packet;
    packet.cmdHeader.packet_type = CALD_TEST_ID;
    uint32_t *p = (uint32_t *) packet.payload;
    *p = slot_mask;
    *(p+1) = num_points;
    *(p+2) = samples;
    *(p+3) = upper;
    *(p+4) = lower;
    SwapLongBlock(p,5);
    do_xl3_cmd_no_response(&packet,crate_number); 
    print_send("Starting cald test!\n", view_fdset);
    uint16_t *point_buf;
    uint16_t *adc_buf;
    point_buf = (uint16_t *) malloc(16*2000*sizeof(uint16_t));
    adc_buf = (uint16_t *) malloc(16*2000*4*sizeof(uint16_t));

    total_points = receive_cald(crate_number,point_buf,adc_buf);
    printf("total points received was %d\n",total_points);

    if (update_db){
	;
	printf("updating database\n");
	for (i=0;i<16;i++){
	    if ((0x1<<i) & slot_mask){
		JsonNode *newdoc = json_mkobject();
		json_append_member(newdoc,"type",json_mkstring("cald_test"));
		JsonNode *points = json_mkarray();
		JsonNode *adc0 = json_mkarray();
		JsonNode *adc1 = json_mkarray();
		JsonNode *adc2 = json_mkarray();
		JsonNode *adc3 = json_mkarray();
		int iter = 0;
		while(iter<=2000){
		    if (iter != 0 && point_buf[i*2000+iter] == 0)
			break;
		    printf("Slot %d - %u : %4u %4u %4u %4u\n",i,point_buf[i*2000+iter],adc_buf[i*8000+iter*4],adc_buf[i*8000+iter*4+1],adc_buf[i*8000+iter*4+2],adc_buf[i*8000+iter*4+3]);
		    json_append_element(points,json_mknumber((double)point_buf[i*2000+iter]));
		    json_append_element(adc0,json_mknumber((double)adc_buf[i*8000+iter*4]));
		    json_append_element(adc1,json_mknumber((double)adc_buf[i*8000+iter*4+1]));
		    json_append_element(adc2,json_mknumber((double)adc_buf[i*8000+iter*4+2]));
		    json_append_element(adc3,json_mknumber((double)adc_buf[i*8000+iter*4+3]));
		    iter++;
		}
		json_append_member(newdoc,"dac_value",points);
		json_append_member(newdoc,"adc_0",adc0);
		json_append_member(newdoc,"adc_1",adc1);
		json_append_member(newdoc,"adc_2",adc2);
		json_append_member(newdoc,"adc_3",adc3);
		json_append_member(newdoc,"pass",json_mkstring("yes"));
		if (final_test)
		    json_append_member(newdoc,"final_test_id",json_mkstring(ft_ids[i]));	
		post_debug_doc(crate_number,i,newdoc);
	    }
	}
    }
    free(point_buf);
    free(adc_buf);

    print_send("cald_test complete\n", view_fdset);
    print_send("*************************************\n", view_fdset);
    return 0;
}

int cald_pushed_from_xl3(int xl3_num)
{
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
    uint16_t currentpoint,slot,adc[4];
    char* p = (char*)aPacket;
    while(1){ // we loop until we get a packet telling us we are done, or until we error out 
	memset(aPacket, '\0', MAX_PACKET_SIZE);
	set_delay_values(0, 1000);
	data=select(fdmax+1, &funcreadable_fdset, NULL, NULL, &delay_value);
	if (data == -1){
	    print_send("new_daq: error in select()\n", view_fdset);
	    return 1;
	}else if (FD_ISSET(connected_xl3s[xl3_num], &funcreadable_fdset)){
	    n = recv(connected_xl3s[xl3_num],p,MAX_PACKET_SIZE, 0);
	    if(n <= 0){
		sprintf(psb, "new_daq: cald_pushed_from_xl3, unable to receive response from xl3 #%d (socket %d)\n",
			xl3_num, connected_xl3s[xl3_num]);
		print_send(psb, view_fdset);
		return 1;
	    }
	    // We've successfully gotten a packet from XL3, what is it?
	    *aPacket = *(XL3_Packet*)p;
	    SwapShortBlock(&(aPacket->cmdHeader.packet_num),1);
	    if (aPacket->cmdHeader.packet_type == MESSAGE_ID){
		print_send(aPacket->payload, view_fdset); // we loop around again
		message_count++;
	    }else if(aPacket->cmdHeader.packet_type == CALD_TEST_ID){
		SwapShortBlock(aPacket->payload,7);
		if (*(uint16_t *) aPacket->payload == 0xFFFF){
		    return 0;
		}else{
		    currentpoint = *(uint16_t *) (aPacket->payload+2);
		    slot = *(uint16_t *) (aPacket->payload+4);
		    adc[0] = *(uint16_t *) (aPacket->payload+6);
		    adc[1] = *(uint16_t *) (aPacket->payload+8);
		    adc[2] = *(uint16_t *) (aPacket->payload+10);
		    adc[3] = *(uint16_t *) (aPacket->payload+12);
		}
	    }else{
		int r;
		for (r=0;r<5;r++)
		    printf("%08x ",*(uint32_t *) (aPacket->payload+4*r));
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


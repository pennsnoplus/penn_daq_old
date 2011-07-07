#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "include/Record_Info.h"
#include "include/xl_regs.h"

#include "penn_daq.h"
#include "xl3_util.h"
#include "net_util.h"
#include "db.h"

int set_location(char *buffer)
{
    char *words,*words2;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'p'){
		printf("location set to penn test stand\n");
		current_location = 2;
	    }
	    if (words[1] == 'u'){
		printf("location set to underground\n");
		current_location = 1;
	    }
	    if (words[1] == 'a'){
		printf("location set to above ground test stand\n");
		current_location = 0;
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: set_location"
			"-a (above ground) -u (under ground) -p (penn)\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }
    return 0;

}

int spec_cmd(char *buffer)
{
    char *words,*words2;
    uint32_t address = 0x02000007;
    uint32_t data = 0x00000000;
    uint32_t result = 0x0;
    int errors;
    int crate_num = 2;

    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate_num = atoi(strtok(NULL, " "));
	    if (words[1] == 'a'){
		words2 = strtok(NULL, " ");
		address = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'd'){
		words2 = strtok(NULL, " ");
		data = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: spec_cmd -c"
			" [crate_num] -a [address] -d [data]\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }

    errors = xl3_rw(address, data, &result, crate_num);
    if (errors == 0){
	sprintf(psb, "result was %08x\n",result);
	print_send(psb, view_fdset);
    }
    else 
	print_send("there was a bus error!\n", view_fdset);
    return 0;
}

int add_cmd(char *buffer)
{
    char *words,*words2;
    uint32_t address = 0x02000007;
    uint32_t data = 0x00000000;
    uint32_t result = 0x0;
    int errors;
    int crate_num = 2;

    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate_num = atoi(strtok(NULL, " "));
	    if (words[1] == 'a'){
		words2 = strtok(NULL, " ");
		address = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'd'){
		words2 = strtok(NULL, " ");
		data = strtoul(words2,(char**)NULL,16);
	    }
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: add_cmd -c"
			" [crate_num] -a [address] -d [data]\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }
    XL3_Packet packet;
    MultiFC *commands = (MultiFC *) packet.payload;
    commands->howmany = 1;
    commands->cmd[0].address = address;
    commands->cmd[0].data = data;
    SwapLongBlock(&(commands->cmd[0].address),1);
    SwapLongBlock(&(commands->cmd[0].data),1);
    SwapLongBlock(&(commands->howmany),1);

    packet.cmdHeader.packet_type = FEC_CMD_ID;
    do_xl3_cmd(&packet,crate_num);
    printf("going into receive data\n");
    receive_data(1,command_number-1,crate_num,&result);
    printf("result was %08x\n",result);

    return 0;
}

int sm_reset(char *buffer)
{
    char *words,*words2;
    int crate_num = 2;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c'){
		words2 = strtok(NULL, " ");
		crate_num = atoi(words2);
	    }
	    if (words[1] == 'h'){
		sprintf(psb, "Usage: sm_reset -c [crate num]\n");
		print_send(psb,view_fdset);
		return -1;
	    }
	}
	words = strtok(NULL, " ");
    }

    XL3_Packet packet;
    uint32_t *p = (uint32_t *) packet.payload;
    packet.cmdHeader.packet_type = STATE_MACHINE_RESET_ID;
    do_xl3_cmd(&packet,crate_num);

    return 0;
}

int debugging_mode(char *buffer, uint32_t onoff)
{
    char *words,*words2;
    uint32_t crate_num=2;
    XL3_Packet debug_packet;
    debug_packet.cmdHeader.packet_type = DEBUGGING_MODE_ID;
    uint32_t *payload_ptr = (uint32_t *) debug_packet.payload;
    *payload_ptr = onoff;
    SwapLongBlock(payload_ptr,1);
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate_num = atoi(strtok(NULL," "));
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: debugging_on/off -c [crate_num]\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }
    do_xl3_cmd(&debug_packet,crate_num);
    if (onoff == 1)
	print_send("Debugging turned on\n",view_fdset);
    else
	print_send("Debugging turned off\n",view_fdset);
    return 0;
}


void send_pong(int xl3_num)
{
    XL3_Packet aPacket;
    aPacket.cmdHeader.packet_type = PONG_ID;
    do_xl3_cmd_no_response(&aPacket,xl3_num);
}



int deselect_fecs(int crate_num){
    XL3_Packet tempPacket;
    tempPacket.cmdHeader.packet_type = DESELECT_FECS_ID;
    do_xl3_cmd(&tempPacket, crate_num);
    return 0;
}

int change_mode(char *buffer)
{
    XL3_Packet mode_packet;
    uint32_t crate_num=2;
    char *words,*words2;
    mode_packet.cmdHeader.packet_type = CHANGE_MODE_ID;
    uint32_t *payload_ptr = (uint32_t *) mode_packet.payload;
    int norm_init = 1;
    *payload_ptr = 0x1;
    *(payload_ptr+1) = 0x0;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c')
		crate_num = atoi(strtok(NULL," "));
	    if (words[1] == 'n')
		norm_init = 2;
	    if (words[1] == 's')
		*(payload_ptr+1) = strtoul(strtok(NULL, " "),(char**)NULL,16);
	    if (words[1] == 'h'){
		sprintf(psb,"Usage: change_mode -c"
			" [crate_num] -n [normal mode] -i [init mode] -s [data avail mask]\n");
		print_send(psb, view_fdset);
		return 0;
	    }
	}
	words = strtok(NULL, " ");
    }
    *payload_ptr = norm_init;
    SwapLongBlock(payload_ptr,2);
    do_xl3_cmd(&mode_packet,crate_num); 
    if (norm_init == 1)
	print_send("Mode changed to init mode\n",view_fdset);
    else
	print_send("Mode changed to normal mode\n",view_fdset);
    return 0;
}

int hv_readback(char *buffer)
{
    char *words,*words2;
    int crate_num = 2;
    uint32_t supply_select = 0;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c'){
		words2 = strtok(NULL, " ");
		crate_num = atoi(words2);
	    }
	    if (words[1] == 'a'){
		supply_select += 1;
	    }
	    if (words[1] == 'b'){
		supply_select += 2;
	    }
	    if (words[1] == 'h'){
		sprintf(psb, "Usage: read_local_voltage -c [crate num] -a (supply a) -b (supply b)\n");
		print_send(psb,view_fdset);
		return -1;
	    }
	}
	words = strtok(NULL, " ");
    }

    XL3_Packet packet;
    float voltage_a,voltage_b,current_a,current_b;
    voltage_a = 0.;
    voltage_b = 0.;
    current_a = 0.;
    current_b = 0.;
    float *p = (float *) packet.payload;
    int i;
    int nt = 1;
    for (i=0;i<nt;i++){
	packet.cmdHeader.packet_type = HV_READBACK_ID;
	do_xl3_cmd(&packet,crate_num);
	SwapLongBlock(p,4);
	voltage_a += *p;
	voltage_b += *(p+1);
	current_a += *(p+2);
	current_b += *(p+3);
    }
    voltage_a /= (float) nt;
    voltage_b /= (float) nt;
    current_a /= (float) nt;
    current_b /= (float) nt;
    if (supply_select != 2){
	printf("Supply A - Voltage: %6.3f volts, Current: %6.4f ma\n",voltage_a*300.0,current_a*10.0);
    }
    if (supply_select > 1){
	printf("Supply B - Voltage: %6.3f volts, Current: %6.4f ma\n",voltage_a*300.0,current_a*10.0);
    }
    return 0;
}


int read_local_voltage(char *buffer)
{
    char *words,*words2;
    int crate_num = 2;
    uint32_t v_select = 0;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c'){
		words2 = strtok(NULL, " ");
		crate_num = atoi(words2);
	    }
	    if (words[1] == 's'){
		words2 = strtok(NULL, " ");
		v_select = atoi(words2);
	    }
	    if (words[1] == 'h'){
		sprintf(psb, "Usage: read_local_voltage -c [crate num] -s [voltage number]\n");
		print_send(psb,view_fdset);
		return -1;
	    }
	}
	words = strtok(NULL, " ");
    }


    XL3_Packet packet;
    uint32_t *p = (uint32_t *) packet.payload;
    packet.cmdHeader.packet_type = READ_LOCAL_VOLTAGE_ID;
    *p = v_select;
    SwapLongBlock(p,1);
    do_xl3_cmd(&packet,crate_num);
    return 0;
}

int hv_ramp_map(char *buffer)
{
    char *words,*words2;
    int crate_num = 2;
    words = strtok(buffer, " ");
    while (words != NULL){
	if (words[0] == '-'){
	    if (words[1] == 'c'){
		words2 = strtok(NULL, " ");
		crate_num = atoi(words2);
	    }
	    if (words[1] == 'h'){
		sprintf(psb, "Usage: read_local_voltage -c [crate num] -s [voltage number]\n");
		print_send(psb,view_fdset);
		return -1;
	    }
	}
	words = strtok(NULL, " ");
    }

    float voltage[2817];
    float current[2817];
    XL3_Packet packet;
    float *p = (float *) packet.payload;
    uint32_t current_voltage = 0;
    uint32_t result;
    xl3_rw(0x02000007, 0xFFFFFFFF, &result, crate_num);
    xl3_rw(0x02000009, 0x0, &result, crate_num);
    xl3_rw(0x02000008, 0x1, &result, crate_num);
    int i,j;
    for (i=0;i<2817;i+=256){
	if (0x1*i < 0xb01){
	    xl3_rw(0x02000009,0x1*i,&result,crate_num);
	}else{
	    printf("too high!\n");
	}
	for (j=0;j<20;j++)
	    usleep(5000);
	packet.cmdHeader.packet_type = HV_READBACK_ID;
	do_xl3_cmd(&packet,crate_num);
	SwapLongBlock(p,4);
	voltage[i]= *p;
	current[i] = *(p+2);
    }
    for (i=0;i<2817;i+=256){
	printf("%d %6.3f %6.3f\n",i,voltage[i]*300.,current[i]*10.);
    }
    xl3_rw(0x02000009,0x800,&result,crate_num);
    xl3_rw(0x02000009,0x500,&result,crate_num);
    xl3_rw(0x02000009,0x0,&result,crate_num);
    xl3_rw(0x02000008,0x0,&result,crate_num);
    return 0; 
}

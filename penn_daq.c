#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>

#include "include/xl_regs.h"
#include "include/Record_Info.h"

//#include "pouch.h"
//#include "json.h"

#include "penn_daq.h"
#include "net_util.h"
#include "xl3_rw.h"
#include "mtc_rw.h"
#include "xl3_util.h"
#include "fec_util.h"
#include "mtc_util.h"
#include "crate_cbal.h"

#include <getopt.h>

int main(int argc, char *argv[]){

    ;
    // set up a signal handler to handle C-c
    (void) signal(SIGINT, sigint_func);

    // ########## CHECK COMMAND LINE ARGS FOR -q OR -h #############
    current_location = 0;
    write_log = 0;
    int c;
    while (1){
        static struct option long_options[] = 
        {
            /* These options set a flag. */
            /* These options don't set a flag. */
            {"log", no_argument, 0, 'l'},
            {"help", no_argument, 0, 'h'},
            {"penn", no_argument, 0, 'p'},
            {"aboveground", no_argument, 0, 'a'},
            {"underground", no_argument, 0, 'u'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;
        c = getopt_long(argc, argv, "lhpau", long_options, &option_index);
        /* Detect the end of options. */
        if (c == -1)
            break;
        switch (c){
            case 0:
                break;
            case 'l':
                printsend("Starting to log!\n");
                start_logging();
                break;
            case 'h':
                printsend("usage: %s [-l/--log] [-p/--penn|-a/--aboveground|-u/--underground]", argv[0]);
                printsend("            or\n");
                printsend("       %s [-h/--help]\n", argv[0]);
                printsend("For more help, read the README\n");
                exit(0);
                break;
            case 'p':
                current_location = 2;
                break;
            case 'a':
                current_location = 0;
                break;
            case 'u':
                current_location = 1;
                break;
            case '?':
                /* getopt_long already printed an error message. */
                break;
            default:
                abort();
        }
    }
    /* print any remaining command line arguments (not options). */
    //if (optind < argc){
    ///printsend("Hostname: %s\n", argv[optind]);
    //}
    //else{
    ///printsend("usage: %s [-l/--log] [-p/--penn|-a/--aboveground|-u/--underground]", argv[0]);
    ///printsend("            or\n");
    ///printsend("       %s [-h/--help]\n", argv[0]);
    ///printsend("For more help, read the README\n");
    //exit(0);
    //}

    printsend("current location is %d\n",current_location);

    // ############ INITIALIZE VARIABLES #############

    // counter integers
    int a, t, x;
    // integer to hold return from select() call
    int select_return;
    // integer to hold number of bytes received
    int numbytes;
    // the packet for input and output
    char c_packet[MAX_PACKET_SIZE];
    // clear it so nothing gunky is inside
    memset(c_packet, '\0', MAX_PACKET_SIZE);
    // newly accept()ed socket descriptor
    int new_fd;
    // file for saving input/output/packets/stuff
    //FILE *datafile;
    // set up the file for reading/writing
    //datafile = fopen("datafile.txt", "a+");
    //fprintf(datafile, "%s", "opened!\n");
    //fclose(datafile);

    //initialize a bunch of variables
    db_debug = 1;
    fdmax = 0;
    mtc_sock = 0;
    rec_bytes=0;
    rec_fake_bytes=0;
    multifc_buffer_full = 0;
    command_number = 0;
    sbc_is_connected = 0;
    current_hv_level = 0x0;
    for (a=0;a<19;a++){
        for(t=0;t<16;t++){
            crate_config[a][t].mb_id = 0x0000;
            crate_config[a][t].dc_id[0] = 0x0000;
            crate_config[a][t].dc_id[1] = 0x0000;
            crate_config[a][t].dc_id[2] = 0x0000;
            crate_config[a][t].dc_id[3] = 0x0000;
        }
    }

    for (a=0;a<MAX_XL3_CON;a++){
        connected_xl3s[a] = -999;
    }

    // set the universal timeout value
    set_delay_values(SECONDS, USECONDS);
    printsend( "delay_value set to %d.%d seconds\n", (int)delay_value.tv_sec,
            ((int)delay_value.tv_usec)/100000);
    


    // make sure the database is up and running
    pouch_request *pr = pr_init();
    pr = db_get(pr, DB_SERVER, DB_BASE_NAME);
    pr_do(pr);
    if(pr->httpresponse != 200){
        printsend("Unable to connect to database. error code %d\n",(int)pr->httpresponse);
        printsend("CURL error code: %d\n", pr->curlcode);
        //exit(0);
    }
    else{
        printsend("Connected to database: http response code %d\n",(int)pr->httpresponse);
    }
    pr_free(pr);




    // ########### SETUP ALL THE ETHERNET STUFF ##################
    setup_listeners();

    printsend("new_daq: setup complete\n");
    // show the user what the ports are linked to
    printsend("\nNAME\t\tPORT#\n");
    printsend("XL3s\t\t%d-%d\n", XL3_PORT, XL3_PORT+MAX_XL3_CON-1);
    printsend("SBC/MTC\t\t%d\n", SBC_PORT);
    printsend("CONTROLLER\t%d\n", CONT_PORT);
    printsend("VIEWERs\t\t%d\n\n", VIEW_PORT);
	printsend("MONITORs\t\t%d\n\n", MON_PORT);

    printsend("waiting for connections...\n");
    
    // note- it's very unlikely that all of the commands will work properly without
    //		an SBC/MTC connected as a client. This string is just a reminder to the
    //		user that they need to set up the SBC/MTC. - (Peter Downs, 8/9/10)


    /////////////////////////////////////////////////////
    // ########## BEGINNING MAIN WHILE LOOP ########## //
    /////////////////////////////////////////////////////

    while(1){
        readable_fdset = all_fdset;
        writeable_fdset = all_fdset;
        /*
           select() looks at all of the fd's up to /fdmax+1/ and copies all of those that are readable
           to the /readable_fset/, all of those that are writeable to the /writeable_fdset/, excluding
           no fd's (/NULL/) with a timeout of /0/. Because the timeout is 0 and select is called in
           an infinite loop, the program is effectively polling all of the sockets for data as fast
           as possible. In this way, the program responds to data nearly instantaneously, but does
           take up a fair amount of processing power. It is more efficient than fork()ing a new child
           process everytime data is received, though.
         */
        select_return = select(fdmax+1, &readable_fdset, &writeable_fdset, NULL, 0);
        if (select_return == -1){

            printsend( "select error in main loop\n");
            
            sigint_func(SIGINT);
        }
        else if(select_return > 0){		// if there were file descriptors to be read/written to
            /*
################################
## Read Incoming Data (2.3) ##
################################
             */
            for(a = 0; a <= fdmax; a ++){
                // the integer 'a' is the socket being checked
                if (FD_ISSET(a, &readable_fdset)){	// if it's a socket with data to be read
                    // clear the command packet so that nothing weird happens to the received data
                    memset(c_packet, '\0', MAX_PACKET_SIZE);
                    /*
#############################################
## Accept/Reject New Connections (2.3.1) ##
#############################################
                     */
                    if(FD_ISSET(a, &listener_fdset)){	// if it's a listener socket
                        /* XL3 request (2.3.1.1) */
                        if(get_xl3_location(a, xl3_listener_array) >= 0){
                            if (get_xl3_location(a, connected_xl3s) >= 0){
                                /*
                                   note-
                                   if the XL3 is already connected, because XL3's do
                                   not send a close connection signal on exit, if there
                                   is an XL3 connecting on the same port that we already
                                   have one connected assume that the old one quit and close it
                                   - (Peter Downs, 8/9/10)
                                 */
                                close_con(a, "XL3");
                            }
                            // accept the connection and print all of the connected xl3's
                            new_fd = accept_connection(a, get_xl3_location(a, xl3_listener_array)+XL3_PORT);
                            print_connected();
                        }
                        /* Controller request (2.3.1.2) */
                        else if(a == cont_listener){
                            t = num_fds(cont_fdset, fdmax);
                            // if we haven't filled the maximum number of controllers, connect another
                            // otherwise, reject the connection
                            if (t < MAX_CONT_CON+1){	// you have to account for the listener
                                new_fd = accept_connection(cont_listener, CONT_PORT);
                            }
                            else{
                                reject_connection(cont_listener, CONT_PORT, t-1, MAX_CONT_CON, "CONTROLLER");
                            }
                        }
						/* Monitor request */
						else if(a == mon_listener){
							t = num_fds(mon_fdset, fdmax);
							if (t < MAX_MON_CON+1){ // account for the listener
								new_fd = accept_connection(mon_listener, MON_PORT);
							}
							else{
								reject_connection(mon_listener, MON_PORT, t-1, MAX_MON_CON, "MONITOR");
							}
						}
                        /* Viewer request (2.3.1.3) */
                        else if(a == view_listener){	// you have to account for the listener
                            t = num_fds(view_fdset, fdmax);
                            // if we haven't filled the maximum number of viewers, connect another
                            // otherwise, reject the connection
                            if (t < MAX_VIEW_CON+1){
                                new_fd = accept_connection(view_listener, VIEW_PORT);
                            }
                            else{
                                reject_connection(view_listener, VIEW_PORT, t-1, MAX_VIEW_CON, "VIEWER");
                            }
                        }
                        /* SBC/MTC request (2.3.1.4) */
                        else if(a == sbc_listener){
                            /*
                               note-
                               It would be incredibly strange for there to be a connection on the SBC/MTC
                               port. The SBC/MTC is connected to manually, with this program (mac_daq) requesting
                               the connection, not vice versa. Still, in case things change, this code is being
                               left in here. - (Peter Downs, 8/9/10);
                             */
                            t = num_fds(mtc_fdset, fdmax);
                            // if we haven't filled the maximum number of SBC/MTC's, connect another
                            // otherwise, reject the connection
                            if (t < MAX_SBC_CON+1){
                                new_fd = accept_connection(sbc_listener, SBC_PORT);
                            }
                            else{
                                reject_connection(sbc_listener, SBC_PORT, t-1, MAX_CONT_CON, "SBC/MTC");
                            }
                        }
                    } // End Accept/Reject New Connection
                    /*
################################
## Process New Data (2.3.2) ##
################################
                     */
                    /* XL3 data (2.3.2.1) */
                    else if(FD_ISSET(a, &xl3_fdset)){
                        numbytes = recv(a, c_packet, MAX_PACKET_SIZE, 0);
                        // if the XL3 closes the connection, which will never happen but has
                        // to be handled just in case, close the connection from this side
                        if (numbytes == 0){
                            close_con(a, "XL3");
                        }
                        // if there is an error with receiving the data, close the connection
                        else if (numbytes < 0){

                            printsend( "new_daq: error receiving data from XL3 #%d\n",
                                    get_xl3_location(a, connected_xl3s));
                            
                            close_con(a, "XL3");
                        }
                        else{
                            XL3_Packet *tmp_xl3_pkt = (XL3_Packet *)c_packet;
                            SwapShortBlock(&(tmp_xl3_pkt->cmdHeader.packet_num),1);
                            proc_xl3_rslt(tmp_xl3_pkt, get_xl3_location(a, connected_xl3s),
                                    numbytes);
                        }
                        memset(c_packet, '\0', MAX_PACKET_SIZE);
                    } // End XL3 data
					else if(FD_ISSET(a, &mon_fdset)){
						numbytes = recv(a, c_packet, MAX_PACKET_SIZE, 0);
						if(numbytes == 0){
							close_con(a, "MONITOR");
						}
						else if(numbytes < 0){
							printsend("receive error: receiving monitor data\n");
							close_con(a, "CONTROLLER");
						}
						else{
							printsend("MONITOR (socket %d): %s\n", a, c_packet);
							memset(c_packet, '\0', MAX_PACKET_SIZE);
						}
					}
                    /* Controller data (2.3.2.2) */
                    else if(FD_ISSET(a, &cont_fdset)){
                        numbytes = recv(a, c_packet, MAX_PACKET_SIZE, 0);
                        // if the controller closes the connection, close the connection on our side
                        if (numbytes == 0){
                            close_con(a, "CONTROLLER");
                        }
                        // if there's an error in the connection, throw an error and close the connection
                        else if ( numbytes < 0){
                            printsend("receive error: receiving controller data\n");
                            close_con(a, "CONTROLLER");
                        }
                        else{

                            printsend( "CONTROLLER (socket %d): %s\n", a, c_packet);
                            
                            if(process_command(c_packet) == -1){
                                sigint_func(SIGINT);
                            }

                            if(FD_ISSET(a, &writeable_fdset)){
                                write(a, COMACK, strlen(COMACK));
                            }

                            else{
                                printsend("could not send response - check connection\n",
                                        view_fdset);
                            }

                            memset(c_packet, '\0', MAX_PACKET_SIZE);
                        }

                    } // End Controller data
                    /* SBC/MTC data (2.3.2.3) */
                    else if(FD_ISSET(a, &mtc_fdset)){
                        numbytes = recv(a, c_packet, MAX_PACKET_SIZE, 0);
                        // if the SBC/MTC closes the connection, close the connection on our side
                        if (numbytes == 0){
                            close_con(a, "SBC/MTC");
                            sbc_is_connected = 0;
                        }
                        // if there's an error in the connection, throw an error and close the connection
                        else if( numbytes < 0){
                            printsend("new_daq: error receiving SBC data\n");
                            close_con(a, "SBC/MTC");
                            sbc_is_connected = 0;
                        }
                        else{

                            printsend( "SBC/MTC (socket %d): %s\n", a, c_packet);
                            
                        }
                    } // End SBC/MTC data
                    /* Viewer data 21.3.2.4) */
                    // this should only be zero, telling us that the client has disconnected
                    else{
                        numbytes = recv(a, c_packet, MAX_PACKET_SIZE, 0);
                        // if the viewer closed the connection, close it on our side
                        if (numbytes == 0){
                            close_con(a, "VIEWER");
                        }
                        // if there was an error receiving data (which should never happen because
                        // the viewer should never be sending data other than telling us that it quit)
                        // show that there was an error and close the connection
                        else if(numbytes < 0){
                            printsend("error receiving viewer data\n");
                            close_con(a, "VIEWER");
                        }
                    } // End viewer data
                } // End process new data
            } // End Read incoming data
        } // End Select()
    } // End MAIN LOOP
    sigint_func(SIGINT);
} // End of main()

///////////////////////////////////////////////////
// ############ END MAIN LOOP ################## //
///////////////////////////////////////////////////





// ###### PROCESS PACKETS RECIEVED FROM XL3 TERMINAL ############
// most of these are acknowledges and should be handled within their appropriate functions
// if these messages show up, it is likely due to a timeout or a bug

/* void proc_xl3_rslt (3.O) */
void proc_xl3_rslt(XL3_Packet *packet, int crate_number, int numbytes){
    switch (packet->cmdHeader.packet_type){
        // here crate number goes from 0 to 18
        // acknowledge that we received different commands
        case CHANGE_MODE_ID:
            printsend( "XL3 (crate %d, port %d): CHANGE_MODE_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case XL3_TEST_CMD_ID:
            printsend( "XL3 (crate %d, port %d): XL3_TEST_CMD_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case SINGLE_CMD_ID:
            printsend( "XL3 (crate %d, port %d): SINGLE_CMD_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case DAQ_QUIT_ID:
            printsend( "XL3 (crate %d, port %d): DAQ_QUIT_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case FEC_CMD_ID:
            printsend( "XL3 (crate %d, port %d): FEC_CMD_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case FEC_TEST_ID:
            printsend( "XL3 (crate %d, port %d): FEC_TEST_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case MEM_TEST_ID:
            printsend( "XL3 (crate %d, port %d): MEM_TEST_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case CRATE_INIT_ID:
            printsend( "XL3 (crate %d, port %d): CRATE_INIT_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case VMON_START_ID:
            printsend( "XL3 (crate %d, port %d): VMON_START_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case BOARD_ID_READ_ID:
            printsend( "XL3 (crate %d, port %d): BOARD_ID_READ_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case ZERO_DISCRIMINATOR_ID:
            printsend( "XL3 (crate %d, port %d): ZERO_DISCRIMINATOR_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case FEC_LOAD_CRATE_ADD_ID:
            printsend( "XL3 (crate %d, port %d): FEC_LOAD_CRATE_ADD_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case SET_CRATE_PEDESTALS_ID:
            printsend( "XL3 (crate %d, port %d): SET_CRATE_PEDESTALS_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case DESELECT_FECS_ID:
            printsend( "XL3 (crate %d, port %d): DESELECT_FECS_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case BUILD_CRATE_CONFIG_ID:
            printsend( "XL3 (crate %d, port %d): BUILD_CRATE_CONFIG_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case LOADSDAC_ID:
            printsend( "XL3 (crate %d, port %d): LOADSDAC_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case CALD_TEST_ID:
            printsend( "XL3 (crate %d, port %d): CALD_TEST_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case STATE_MACHINE_RESET_ID:
            printsend( "XL3 (crate %d, port %d): STATE_MACHINE_RESET_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;
        case MULTI_CMD_ID:
            printsend( "XL3 (crate %d, port %d): MULTI_CMD_ID\n",
                    crate_number, crate_number+XL3_PORT);
            
            break;




            // THESE BELOW ARE THE ONLY ONES YOU SHOULD SEE AND
            // DO SOMETHING ABOUT
        case MESSAGE_ID:
            printsend( "XL3 (crate %d, port %d): MESSAGE: %s\n",
                    crate_number, crate_number+XL3_PORT,packet->payload);
            
            break;
        case MEGA_BUNDLE_ID:
            //store_mega_bundle(packet->cmdHeader.num_bundles);
			process_mega_bundle(packet);

            break;
        case CMD_ACK_ID:
            // something with CMD_ACK_ID //FIXME
            break;
        case STATUS_ID:
            // something with STATUS_ID //FIXME
            break;
        case PING_ID:
            send_pong(crate_number);
            break;
        default:
            printsend("STRANGE PACKET:\n");
            printsend( "XL3 (crate %d, port %d): OTHER: %d (%08x)\n",
                    crate_number, crate_number+XL3_PORT,
                    (int)packet->cmdHeader.packet_type,*(uint32_t *) &(packet->cmdHeader));
            break;
    }
}



// ########## PROCESS PACKETS FROM CONTROLLER TERMINAL ###########

int process_command(char *buffer){ //DATABASE
    //_!_begin_commands_!_
    if (strncmp(buffer, "exit", 4) == 0){ // quit
        printsend("new_daq: exiting\n");
        return -1;
    }
    else if (strncmp(buffer, "print_connected", 10) == 0)
        print_connected();
    else if (strncmp(buffer, "stop_logging", 12) == 0)
        stop_logging();
    else if (strncmp(buffer, "start_logging", 13) == 0)
        start_logging();
    else if (strncmp(buffer, "debugging_on",12)==0)
        debugging_mode(buffer,0x1);
    else if (strncmp(buffer, "debugging_off",13)==0)
        debugging_mode(buffer,0x0);
    else if (strncmp(buffer, "set_location",12) == 0)
        set_location(buffer);
    else if (strncmp(buffer, "final_test",10) == 0)
        final_test(buffer);
    else if (strncmp(buffer, "change_mode",11)==0)
        change_mode(buffer);
    else if (strncmp(buffer, "readout_test",12) == 0)
        readout_test(buffer);
    else if (strncmp(buffer, "readout_add_crate",17) == 0)
        readout_add_crate(buffer);
    else if (strncmp(buffer, "end_readout",11) == 0)
        end_readout(buffer);
    else if (strncmp(buffer, "readout_add_mtc",15) == 0)
        readout_add_mtc(buffer);
    else if (strncmp(buffer, "stop_pulser",11) == 0)
        stop_pulser(buffer);
    else if (strncmp(buffer, "change_pulser",13) == 0)
        change_pulser(buffer);
    else if (strncmp(buffer, "mtc_init", 8) == 0)
        mtc_init(buffer);
    else if (strncmp(buffer, "get_caen_data", 13) == 0)
        get_caen_data(buffer);
    else if (strncmp(buffer, "ped_run",7) == 0)
        ped_run(buffer);
    else if (strncmp(buffer, "crate_init",10) == 0)
        crate_init(buffer);
    else if (strncmp(buffer, "fec_test",8) == 0)
        fec_test(buffer);
    else if (strncmp(buffer, "get_ttot",8) == 0)
        get_ttot(buffer);
    else if (strncmp(buffer, "set_ttot",8) == 0)
        set_ttot(buffer);
    else if (strncmp(buffer, "load_relays",8) == 0)
        load_relays(buffer);
    else if (strncmp(buffer, "mem_test",8) == 0)
        mem_test(buffer);
    else if (strncmp(buffer, "vmon",4) == 0)
        vmon(buffer);
    else if (strncmp(buffer, "board_id",8) == 0)
        board_id(buffer);
    else if (strncmp(buffer, "crate_cbal",10) == 0)
        crate_cbal(buffer);
    else if (strncmp(buffer, "spec_cmd",8) == 0)
        spec_cmd(buffer);
    else if (strncmp(buffer, "mtc_read",8) == 0)
        mtc_read(buffer);
    else if (strncmp(buffer, "mtc_write",9) == 0)
        mtc_write(buffer);
    else if (strncmp(buffer, "add_cmd",7) == 0)
        add_cmd(buffer);
    else if (strncmp(buffer, "zdisc",5) == 0)
        zdisc(buffer);
    else if (strncmp(buffer, "cgt_test_1",10) == 0)
        cgt_test_1(buffer);
    else if (strncmp(buffer, "fifo_test",9) == 0)
        fifo_test(buffer);
    else if (strncmp(buffer, "cmos_m_gtvalid",14) == 0)
        cmos_m_gtvalid(buffer);
    else if (strncmp(buffer, "send_softgt",11) == 0)
        send_softgt();
    else if (strncmp(buffer, "multi_softgt",11) == 0)
        multi_softgt(1000);
    else if (strncmp(buffer, "read_bundle",11) == 0)
        read_bundle(buffer);
    else if (strncmp(buffer, "cald_test",9) == 0)
        cald_test(buffer);
    else if (strncmp(buffer, "change_delay",12) == 0)
        changedelay(buffer);
    else if (strncmp(buffer, "sm_reset",8) == 0)
        sm_reset(buffer);
    else if (strncmp(buffer, "read_local_voltage",18) == 0)
        read_local_voltage(buffer);
    else if (strncmp(buffer, "ramp_voltage",12) == 0)
        ramp_voltage(buffer);
    else if (strncmp(buffer, "hv_readback",11) == 0)
        hv_readback(buffer);
    else if (strncmp(buffer, "hv_ramp_map",11) == 0)
        hv_ramp_map(buffer);
    else if (strncmp(buffer, "mb_stability_test",17) == 0)
        mb_stability_test(buffer);
    else if (strncmp(buffer, "chinj_scan", 10) == 0)
        chinj_scan(buffer);
    else if (strncmp(buffer, "set_gt_mask",11) == 0)
        set_gt_mask_cmd(buffer);
    else if (strncmp(buffer, "unset_gt_mask",11) == 0)
        unset_gt_mask_cmd(buffer);
    else if (strncmp(buffer, "setup_pedestals",15) == 0)
        setup_pedestals_cmd(buffer);
    else if (strncmp(buffer, "set_thresholds",14) == 0)
        set_thresholds(buffer);
    else if (strncmp(buffer, "trigger_scan",12) == 0)
        trigger_scan(buffer);
    else if (strncmp(buffer, "start_pulser",12) == 0)
        start_pulser(buffer);
    else if (strncmp(buffer, "enable_pedestal",15) == 0)
        enable_pedestal(buffer);
    else if (strncmp(buffer, "disable_pedestal",16) == 0)
        disable_pedestal(buffer);
    else if (strncmp(buffer, "unset_gt_crate_mask",19) == 0)
        unset_gt_crate_mask_cmd(buffer);
    else if (strncmp(buffer, "set_gt_crate_mask",17) == 0)
        set_gt_crate_mask_cmd(buffer);
    else if (strncmp(buffer, "unset_ped_crate_mask",20) == 0)
        unset_ped_crate_mask_cmd(buffer);
    else if (strncmp(buffer, "set_ped_crate_mask",18) == 0)
        set_ped_crate_mask_cmd(buffer);
    else if (strncmp(buffer, "sbc_control", 11) == 0){
        sbc_control(SBC_PORT, server, buffer);
    }
    else if (strncmp(buffer, "clear_screen", 12) == 0){
        system("clear");
        printsend("new_daq: cleared screen\n");
    }
    //_!_end_commands_!_
    else
        printsend("not a valid command\n");

    return 0;
}

int store_mega_bundle(int nbundles){ // a mega_bundle
    int k;
    int Nprint=100; // print out after Nprint recieved mega bundles
    struct timeval tv;
    struct timeval tv2;
    uint32_t *bundle;
    gettimeofday(&tv,0);
    if(count_d==0){
        start_time = tv.tv_sec*1000000+tv.tv_usec;
        rec_bytes=0;
        rec_fake_bytes=0;
    }
    gettimeofday(&tv2,0);
    new_time = (tv2.tv_sec*1000000+tv2.tv_usec);
    delta_t =new_time -start_time;
    count_d++;
    rec_bytes+=nbundles*12;
    rec_fake_bytes+=(120-nbundles)*12;
    if(count_d%Nprint==0) {
        dt= new_time-old_time;
        old_time=new_time;
        printsend( "recv %i \t %i \t %i \t %i \t ave %8.2f Mb/s \t d/dt %8.2f Mb/s (%.1f %% fake)\n",
                count_d,nbundles,(int)rec_bytes,(int)delta_t,(float)(rec_bytes*8/(float)delta_t),
                (float)(nbundles*12*8*Nprint/(float)dt),rec_fake_bytes/(float)(Nprint*120*12)*100.0);
        
        rec_fake_bytes=0;
    }
	return 0;
}
int process_mega_bundle(XL3_Packet *packet){
	XL3_CommandHeader cmhdr = (XL3_CommandHeader)(packet->cmdHeader);
	PMTBundle *bndl_array = (PMTBundle *)(packet->payload);
	//char *bdata = (char *)packet;
	//int z;
	//for(z = 0; z < sizeof(XL3_Packet); z++){
		//printf("%d : %08x\n", z, (int)(*(bdata+z)));
	//}
	//printf("Num bundles = %d\n", cmhdr.num_bundles);
	//printf("Packet type = %08x", cmhdr.packet_type);
	//printf("Packet num = %08x", cmhdr.packet_num);
	//printf("First PMT Bundle\n");
	//printf("word1= %08x\nword2= %08x\nword3= %08x\n", bndl_array[0].word1, bndl_array[0].word2, bndl_array[0].word3);
	//uint32_t _qlx, _qhs, _qhl;
	//_qlx = (uint32_t) UNPK_QLX((uint32_t *)&bndl_array[0]);
	//_qhs = (uint32_t) UNPK_QHS((uint32_t *)&bndl_array[0]);
	//_qhl = (uint32_t) UNPK_QHL((uint32_t *)&bndl_array[0]);
	//printf("qhl= %d\nqhs= %d\nqlx= %d\n", _qhl, _qhs, _qlx);
	//printf("---------------\n");
	
	if(mon_sock){
		//printsend("process_mega_bundle: sending mega_bundle to monitor\n");
		fd_set outset;
		FD_ZERO(&outset);
		outset = mon_fdset;
		int select_return, x;
		select_return = select(fdmax+1, NULL, &outset, NULL, 0);
		if(select_return > 0){
			//printsend("sizeof(packet->payload)= %d\n", (int)sizeof(packet->payload));
			//write(mon_sock, packet->payload, sizeof(packet->payload));
			write(mon_sock, packet, sizeof(XL3_Packet)); 
		}
	}
	else{
		//printsend("process_mega_bundle: error: no monitor connection\n");
	}
	return 0;
}



///////////////////////////////////////////////
// ######### VARIOUS UTILITIES ############# //
///////////////////////////////////////////////

/* int get_xl3_location (3.G) */
int get_xl3_location(int value, int array[]){
    /*
       Ok, so this function just returns the index
       of /value/ within /array/. But if value is an
       xl3 socket file descriptor and array is
       connected_xl3s[], it returns the crate number.
     ***THIS IS IMPORTANT TO KNOW AND REMEMBER***
     That is how you'll see it used most often within
     the body of the program. But it will work with any
     integer value and array of integers. You'll also see
     it used when assigning the crate number of an XL3-
     by finding the location of the socket a request came
     in on in xl3_listener_array[], you can find the
     crate number
     */
    int z;
    for(z = 0; z <= MAX_XL3_CON; z++){
        if (array[z] == value){
            return z;
        }
    }
    return -1;
}

/* void set_delay_values (3.I) */
void set_delay_values(int seconds, int useconds){
    /*
       This sets the global tv structure delay_value
       for use during select() to time out after
       seconds and useconds amount of time. Because every
       call of select using delay_value resets it to zero,
       I made a function which resets it. You'll notice that
       after any select() call which has a timeout value of
       &delay_value, this function is called to reset the
       delay_value structure.
     */
    delay_value.tv_sec = seconds;
    delay_value.tv_usec = useconds;
}

/* void sigint_func (3.L) */
void sigint_func(int sig){
    /*
       A simple signal handler- whenever a SIGINT is thrown,
       mac_daq catches it and runs this function instead of just
       exiting immediately. This function makes sure that all
       open connections/files are closed correctly. It also moves
       any logs in the /daq directory to /daq/logs.
     */
    printsend("\nnew_daq: beginning shutdown\n");
    printsend("new_daq: closing connections\n");
    int u;
    for(u = 0; u <= fdmax; u++){
        if(FD_ISSET(u, &all_fdset)){
            close(u);
        }
    }
    stop_logging();
    if(write_log){
        printsend("new_daq: closing log\n");
        if(ps_log_file){
            stop_logging();
        }
        else{
            printsend("No log file to close\n");
        }
    }
    exit(0);
}

/* void start_logging (3.M) */
void start_logging(){
    /*
       Opens up a time stamped log file which printsend()
       tries to write to every time it is called.
       Format is:
       log_name = "Year_Month_Day_Hour_Minute_Second_Milliseconds.log"

       All log files are created in /daq. As the program quits, they are moved
       to /daq/LOGS.
     */
    if(!write_log){
        write_log = 1;
        char log_name[256] = {'\0'};	// random size, it's a pretty nice number though.
        time_t curtime = time(NULL);
        struct timeval moretime;
        gettimeofday(&moretime,0);
        struct tm *loctime = localtime(&curtime);

        strftime(log_name, 256, "%Y_%m_%d_%H_%M_%S_", loctime);
        sprintf(log_name+strlen(log_name), "%d.log", (int)moretime.tv_usec);
        ps_log_file = fopen(log_name, "a+");
        printsend( "Enabled logging\n");
        printsend( "Opened log file: %s\n", log_name);
        
    }
    else{
        printsend("Logging already enabled\n");
    }
}

/* void stop_logging (3.N) */
void stop_logging(){
    /*
       This function does two things:
       1. Tells the program to stop logging future output
       2. Closes all existing log files and moves them to
       /daq/LOGS
     */
    if(write_log){
        write_log = 0;
        printsend("Disabled logging\n");
        if(ps_log_file){
            printsend("Closed log file\n");
            fclose(ps_log_file);
            system("mv *.log ./logs");
        }
        else{
            printsend("\tNo log file to close\n");
        }
    }
    else{
        printsend("Logging is already disabled\n");
    }
}

void SwapLongBlock(void* p, int32_t n){
#ifdef NeedToSwap
    int32_t* lp = (int32_t*)p;
    int32_t i;
    for(i=0;i<n;i++){
        int32_t x = *lp;
        *lp = (((x) & 0x000000FF) << 24) |
            (((x) & 0x0000FF00) << 8) |
            (((x) & 0x00FF0000) >> 8) |
            (((x) & 0xFF000000) >> 24);
        lp++;
    }
#endif
}
void SwapShortBlock(void* p, int32_t n){
#ifdef NeedToSwap
    int16_t* sp = (int16_t*)p;
    int32_t i;
    for(i=0;i<n;i++){
        int16_t x = *sp;
        *sp = ((x & 0x00FF) << 8) |
            ((x & 0xFF00) >> 8) ;
        sp++;
    }
#endif
}

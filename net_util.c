#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> 
#include <signal.h>

#include <string.h>
#include <stdlib.h> 
#include <stdio.h>
#include <stdint.h>

#include "penn_daq.h"
#include "net_util.h"

void setup_listeners(){

	// set the server for connect_to_SBC()
	server = gethostbyname((char*)SBC_SERVER);

	// ##  Clear fd_set's (1.2)  ##
	FD_ZERO(&xl3_fdset);
	FD_ZERO(&mtc_fdset);
	FD_ZERO(&cont_fdset);
	FD_ZERO(&view_fdset);
	FD_ZERO(&readable_fdset);
	FD_ZERO(&writeable_fdset);
	FD_ZERO(&all_fdset);
	FD_ZERO(&funcreadable_fdset);
	FD_ZERO(&listener_fdset);

	// ##  Bind Non-XL3 Listeners (1.3)  ##
	sbc_listener  = bind_listener("0.0.0.0", SBC_PORT);
	cont_listener = bind_listener("0.0.0.0", CONT_PORT);
	view_listener = bind_listener("0.0.0.0", VIEW_PORT);

	//##  Add Non-XL3 Listeners (1.4)  ##

	// add sbc listener
	if (listen(sbc_listener, MAX_PENDING_CONS) == -1){
		print_send("listen error: adding SBC/MTC client listener\n", view_fdset);
		sigint_func(SIGINT);
	}
	else{
		FD_SET(sbc_listener, &all_fdset);
		FD_SET(sbc_listener, &listener_fdset);
		FD_SET(sbc_listener, &mtc_fdset);
		if (fdmax < sbc_listener){
			fdmax = sbc_listener;
		}
	}
	// add controller client listener
	if (listen(cont_listener, MAX_PENDING_CONS) == -1){
		print_send("listen error: adding controller client listener\n", view_fdset);
		sigint_func(SIGINT);
	}
	else{
		FD_SET(cont_listener, &all_fdset);
		FD_SET(cont_listener, &listener_fdset);
		FD_SET(cont_listener, &cont_fdset);
		if (fdmax < cont_listener){
			fdmax = cont_listener;
		}
	}
	// add view client listener
	if (listen(view_listener, MAX_PENDING_CONS) == -1){
		print_send("listen error: adding view client listener\n", view_fdset);
		sigint_func(SIGINT);
	}
	else{
		FD_SET(view_listener, &all_fdset);
		FD_SET(view_listener, &listener_fdset);
		FD_SET(view_listener, &view_fdset);
		if (fdmax < view_listener){
			fdmax = view_listener;
		}
	}
	/*
####################################
##  Bind/Add XL3 Listeners (1.5)  ##
####################################

XL3 listeners are bind()ed (...bound) and added in exactly
the same way as the other listeners, but because there are MAX_XL3_CON
different listeners it's easier to do it with a for loop. Each listener socket
is added to the xl3_fdset, the listener_fdset, and the all_fdset, but in addition
they are added to the xl3_listener_array. Their location/address in the array
(let's call it q for convenience) is their crate number (counting from 0 to 18). This is used later
in the program to find the crate number of a specific xl3- all that needs to be
done is to find the listener socket's address in this array.
	 */
	int q;
	for(q = 0; q< MAX_XL3_CON; q++){
		xl3_listener_array[q] = bind_listener("0.0.0.0", XL3_PORT+q); //FIXME
		// add xl3 listener
		if (listen(xl3_listener_array[q], MAX_PENDING_CONS) == -1){
			print_send("listen error: adding XL3 listener\n", view_fdset);
			sigint_func(SIGINT);
		}
		else{
			FD_SET(xl3_listener_array[q], &all_fdset);
			FD_SET(xl3_listener_array[q], &listener_fdset);
			FD_SET(xl3_listener_array[q], &xl3_fdset);
			if (fdmax < xl3_listener_array[q]){
				fdmax = xl3_listener_array[q];
			}
		}
	}

}

/*  void *get_in_addr (3.A) */
void *get_in_addr(struct sockaddr *sa){
	/*
	   Get the address of the connection
	   associated with sockaddr
	   - beej.us

	   note-
	   this function was stolen directly
	   from beej.us's excellent socket
	   tutorial.
	   - (Peter Downs, 8/9/10)
	 */
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* int bind_listener (3.B) */
int bind_listener(char *host, int port){
	/*
	   Given a host and a port, return a file descriptor
	   (basically an integer) / socket to listen on that
	   port for new requests (such as clients trying to
	   connect).
	 */
	int rv, listener;
	int yes = 1;
	char str_port[10];
	sprintf(str_port,"%d",port);
	struct addrinfo hints, *ai, *p;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((rv = getaddrinfo(host, str_port, &hints, &ai)) != 0) {

		sprintf(psb, "new_daq: %s\n", gai_strerror(rv));
		print_send(psb, view_fdset);
		return -1;
		sigint_func(SIGINT);
	}
	for(p = ai; p != NULL; p = p->ai_next) {
		listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) { 
			continue;
		}
		// lose the pesky "address already in use" error message
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

		if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
			close(listener);
			continue;
		}
		break;
	}
	if (p == NULL) {
		// If p == NULL, that means that listener
		// did not get bound to the port

		sprintf(psb, "new_daq: failed to bind listener\n");
		print_send(psb, view_fdset);
		return -1;
	}
	else{
		return listener;
	}
	freeaddrinfo(ai); // all done with this
}

/* void reject_connection (3.C) */
void reject_connection(int socket, int listener_port, int connections, int max_con, char name[]){
	/*
	   This function rejects a connection to a specific port. It tells
	   the client trying to connect that there are too many connections
	   to the port already, prints the number of connections to the screen,
	   and prints that it rejected a connection. It is not that fancy,
	   and doesn't do anything too special, but makes the rejection look
	   nice and, because there are a couple of different places where
	   a connection needs to be rejected in the code, saves some space
	   The way this is actually accomplished is by accepting the connection,
	   writing the message to it, and then immediately closing it (in effect, rejecting it).
	 */
	// handle new connections
	struct sockaddr_storage remoteaddr; // client address
	socklen_t addrlen;
	int newfd; // the new file descriptor
	int numbytes; // number of bytes sent
	char remoteIP[INET6_ADDRSTRLEN]; // character array to hold the remote IP address
	addrlen = sizeof remoteaddr;
	newfd = accept(socket, (struct sockaddr *)&remoteaddr, &addrlen);

	if (newfd == -1) {
		print_send("reject_connection: accept error\n", view_fdset);
	} 
	else {
		numbytes = send(newfd, "new_daq: too many connections to this port\n", 44, 0);

		sprintf(psb, "%s (%s) tried to connect on socket %d\n", name, inet_ntop(remoteaddr.ss_family,
					get_in_addr((struct sockaddr*)&remoteaddr), remoteIP, INET6_ADDRSTRLEN), newfd);
		print_send(psb, view_fdset);

		sprintf(psb, "\tthere are already %d of %d %ss connected\n\trejected %s connection on socket %d\n",
				connections, max_con, name, name, newfd);
		print_send(psb, view_fdset);
		close(newfd);
	}
}

/* int num_fds (3.F) */
int num_fds(fd_set set, int fdmax){
	/*
	   For a given fd_set, return how many
	   file descriptors (in this case sockets)
	   are associated with it.
	 */
	int i;
	int count = 0;
	for(i = 0; i <= fdmax; i++){
		if (FD_ISSET(i, &set)){
			// everytime i is a member of the
			// fd_set, increase the total count
			count++; 
		}
	}
	// return the total count of file descriptors
	return count;
}

/* int accept_connection (3.D) */
int accept_connection(int socket, int listener_port){
	// note: listener port goes from 1-19
	/*
	   This function, given a socket and a listener port,
	   accept()s the new socket and adds it to the appropriate
	   socket file descriptor sets (fd_sets). It keeps track of the
	   highest file descriptor (fdmax) and changes it appropriately, too.
	   Returns a file descriptor (an integer) to the new socket.
	 */
	// handle new connections
	struct sockaddr_storage remoteaddr; // client address
	socklen_t addrlen;
	int newfd; // the new file descriptor
	char remoteIP[INET6_ADDRSTRLEN]; // character array to hold the remote IP address
	addrlen = sizeof remoteaddr;
	newfd = accept(socket, (struct sockaddr *)&remoteaddr, &addrlen);

	if (newfd == -1) {
		print_send("accept error in accept_connection\n", view_fdset);
		return FAIL;
	} 
	else {
		if(newfd > 0){
			// set the new highest file descriptor
			if (newfd > fdmax){
				fdmax = newfd;
			}
			FD_SET(newfd, &all_fdset);
			if(get_xl3_location(socket, xl3_listener_array) >= 0){
				int p = listener_port - XL3_PORT;
				if(connected_xl3s[p] == -999){

					sprintf(psb, "new_daq: connection: XL3 (port %d, socket %d, from %s)\n", listener_port,
							newfd, inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr),
								remoteIP, INET6_ADDRSTRLEN), newfd);
					print_send(psb, view_fdset);
					connected_xl3s[p] = newfd;	// listener_port = XL3_PORT + listener number 
					FD_SET(newfd, &xl3_fdset);
				}
				else{
					close_con(connected_xl3s[p], "XL3");

					FD_SET(newfd, &xl3_fdset);
					connected_xl3s[p] = newfd;

					sprintf(psb,"new_daq: resumed connection: XL3 (port %d, socket %d, from %s)\n", listener_port,
							newfd, inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr),
								remoteIP, INET6_ADDRSTRLEN), newfd);
					print_send(psb, view_fdset);
				}
			}
			else if(listener_port == SBC_PORT){
				/*
				   note-
				   Ok, so i'm pretty sure this part is completely unecessary because the sbc won't
				   try to connect to the daq- through the control client, you need to call
				   "connect_to_SBC". So yeah, it's here. but will probably never be called. And it
				   should definitely be removed at some point.
				   - (Peter Downs, 8/6/10)
				 */

				sprintf(psb, "new_daq: connection request: SBC/MTC (%s) on socket %d\n",
						inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr),
							remoteIP, INET6_ADDRSTRLEN), newfd);
				print_send(psb, view_fdset);
				// this is the test packet that ./OrcaReadout looks for     
				// the ppc mac swaps Bytes, the linux sbc does not.
				int32_t testWord=0x000DCBA;
				char *send_test= (char *)&testWord;

				int n;
				if(FD_ISSET(socket, &writeable_fdset)){
					n = write(socket,send_test,4);

				}
				else{
					print_send("could not send test packet to SBC\n new_daq: SBC/MTC connection denied\n",
							view_fdset);
					//reject_connection(listener_port, "SBC/MTC");
					return FAIL;
				}

				sprintf(psb, "new_daq: SBC/MTC connected (port %d, socket %d)\n", SBC_PORT, newfd);
				print_send(psb, view_fdset);
				FD_SET(newfd, &mtc_fdset);
				mtc_sock = newfd;
			}
			else if(listener_port == CONT_PORT){
				FD_SET(newfd, &cont_fdset);

				sprintf(psb, "new_daq: connection:  CONTROLLER (%s) on socket %d\n", 
						inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr),
							remoteIP, INET6_ADDRSTRLEN), newfd);
				print_send(psb, view_fdset);
			}
			else{


				sprintf(psb, "new_daq: connection: VIEWER (%s) on socket %d\n", 
						inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr),
							remoteIP, INET6_ADDRSTRLEN), newfd);
				print_send(psb, view_fdset);
				FD_SET(newfd, &view_fdset);
			}
			return newfd;
		}
		else{
			print_send("new_daq: failed to accept connection\n", view_fdset);
			return FAIL;
		}
	}
}

/* void close_con (3.E) */
void close_con(int con_fd, char name[]){
	/*
	   This is a nice, pretty function which closes
	   a socket connection and removes it from the
	   fd_sets it had belonged to. It does some specific stuff
	   based on whether the socket was an XL3 or SBC/MTC,
	   but really it's just to save space in the code.
	 */

	sprintf(psb, "new_daq: closed %s connection (", name);
	close(con_fd);
	FD_CLR(con_fd, &all_fdset);
	if(FD_ISSET(con_fd, &xl3_fdset)){
		FD_CLR(con_fd, &xl3_fdset);
		sprintf(psb + strlen(psb), "crate #%d, port %d, ",
				get_xl3_location(con_fd, connected_xl3s),
				XL3_PORT+get_xl3_location(con_fd, connected_xl3s));

		connected_xl3s[get_xl3_location(con_fd, connected_xl3s)] = -999;
	}
	else if(FD_ISSET(con_fd, &mtc_fdset)){
		sprintf(psb + strlen(psb), "port %d, ", SBC_PORT);
		mtc_sock = 0;
		FD_CLR(con_fd, &mtc_fdset);
	}
	else if(FD_ISSET(con_fd, &cont_fdset)){
		sprintf(psb + strlen(psb), "port %d, ", CONT_PORT);
		FD_CLR(con_fd, &cont_fdset);
	}
	else{
		sprintf(psb + strlen(psb), "port %d, ", VIEW_PORT);
		FD_CLR(con_fd, &view_fdset);
	}
	sprintf(psb + strlen(psb), "socket %d)\n", con_fd);
	print_send(psb, view_fdset);
}


/* void print_connected (3.H) */
void print_connected(void){
	/*
	   Print out information on every connected
	   client, be it an XL3, SBC/MTC, Controller,
	   or Viewer.
	 */
	int z,i;
	int y = 0;
	print_send("CONNECTED CLIENTS:\n", view_fdset);
	for(z = 0; z <= fdmax; z++){
		if(!FD_ISSET(z, &listener_fdset)){	
			for (i=0;i<MAX_XL3_CON;i++){ 
				if(connected_xl3s[i] == z){
					y++;
					sprintf(psb, "\tXL3 ");
					sprintf(psb + strlen(psb), "(crate #%d, ", i);
					sprintf(psb + strlen(psb), "port %d, ", XL3_PORT+i);
					sprintf(psb + strlen(psb), "socket %d)\n", connected_xl3s[i]);
					print_send(psb, view_fdset);	
				}
			}
			if(FD_ISSET(z, &cont_fdset)){
				y++;
				sprintf(psb, "\tController (port %d, socket %d)\n",
						CONT_PORT, z);
				print_send(psb, view_fdset);
			}
			if(FD_ISSET(z, &mtc_fdset)){
				y++;
				sprintf(psb, "\tSBC/MTC (port %d, socket %d)\n",
						SBC_PORT, z);
				print_send(psb, view_fdset);
			}
			if(FD_ISSET(z, &view_fdset)){
				y++;
				sprintf(psb, "\tViewer (port %d, socket %d)\n",
						VIEW_PORT, z);
				print_send(psb, view_fdset);
			}
		}
	}
	if(y == 0){
		print_send("\tno connected boards\n", view_fdset);
	}
}

/* int print_send (3.K) */
int print_send(char *input, fd_set suggested){
	/*
	   This is a printf() replacement function that:
	   1. prints the given string(/input/)
	   2. attempts to send the given string(/input/) to all of
	   the file descriptors in fd_set /suggested/.

	   print_send() accesses the global "fdmax", defined in mac_daq.c, for its select() function.
	   print_send() also accessed the global "write_log", defined in mac_daq.c, to determine
	   whether or not to write all output to a log file, too.

	   By sprintf()ing any message into a buffer and feeding that buffer, along with
	   a fd_set to look in for writeable sockets, this function can be used to send to
	   the connected viewers any message that is also printed to the screen. 

	   If one wanted to also send all of these messages to the controller, or, say, 
	   another type of client, all that needs to be done is when accepting them
	   (in accept_connection(), most likely) add:
	   FD_SET(socket, &view_fdset);
	   where "socket" is the socket of the new connection. 
	 */

	int printed_bytes, select_return, x;
	int sent_bytes = 0;

	// create an fd_set for use with the select() function
	fd_set outset;
	// clear the fd_set so nothing funky happens
	FD_ZERO(&outset);

	// print input to the screen
	printed_bytes = printf("%s", input);

	// make sure that there is a writeable file descriptor
	// in the suggested fd_set other than the view listener
	// and then write() /input/ to them
	int i;
	int count = 0;
	for(i = 0; i <= fdmax; i++){
		if (FD_ISSET(i, &suggested)){
			// everytime i is a member of the
			// fd_set, increase the total count
			count++; 
		}
	}
	if(count > 1){	// there will always be one: the view listener
		outset = suggested;
		select_return = select(fdmax+1, NULL, &outset, NULL, 0);
		// if there were writeable file descriptors
		if(select_return > 0){
			for(x = 0; x <= fdmax; x++){
				if(FD_ISSET(x, &outset)){
					// send /input/ to each writeable file descriptor
					sent_bytes += write(x, input, printed_bytes);
				}
			}
		}
	}

	// if not silenced, write to the log file ps_log_file
	if(write_log && ps_log_file){
		fprintf(ps_log_file, "%s", input);
	} 

	// if the psb has something in it, clear it
	if(strlen(psb) > 0){
		memset(psb, '\0', sizeof(psb));
	}

	// return the total number of sent bytes
	return sent_bytes;
}


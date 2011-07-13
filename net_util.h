#include <sys/socket.h>

#include "penn_daq.h"

/*
   __FILE DESCRIPTOR SETS__
   fd_sets are used to keep track of file descriptors (basically integers, but really just
   something that can be written to or read from). In this case, fd_sets are used to
   categorize open sockets so that data can be handled differently depending on what type
   it is. Every time data is received, mac_daq checks which fd_set's the socket the data was
   sent on belongs to; based off of this, the data is handled in different ways.
 */
fd_set xl3_fdset;			// fd_set for the xl3 boards
fd_set mtc_fdset;			// fd_set for the SBC/MTC board/server
fd_set cont_fdset;			// fd_set for the control client

// view_fdset is the fd_set that print_send() looks to for possible sockets to write to.
// Adding other types of clients to view_fdset lets print_send() also try to write all
// output to them, too.
// for example, in accept_connection() where it accepts new controller clients:
//		FD_SET(socket, &view_fdset);


// fd_sets for the main select() function
fd_set readable_fdset;		// stores readable file descriptors
fd_set funcreadable_fdset;	// stores readable file descriptors for functions that send data
fd_set writeable_fdset;		// stores writeable file descriptors
fd_set all_fdset;			// stores all of the file descriptors
fd_set listener_fdset;		// stores all of the listener file descriptors
fd_set view_fdset;			// fd_set for the view client

/* variables for print_send()
 */
int fdmax;			// highest file descriptor number - this is used in the select() call, as well as
// in the print_send() function.

// listeners
int sbc_listener, cont_listener, view_listener;
// sockets for the MTC/SBC, controller (only one of each)
int mtc_sock;
int control_sock;

// array of sockets for accepting the xl3s on MAX_XL3_CON different ports
int xl3_listener_array[MAX_XL3_CON];

// connected_xl3s[crate_number] = socket associated with each crate number
// xl3 number goes from 0 to 18
int connected_xl3s[MAX_XL3_CON];
struct hostent *server;



void *get_in_addr(struct sockaddr *sa);
int bind_listener(char *host, int port);
void reject_connection(int socket, int listener_port, int connections, int max_con, char name[]);
int num_fds(fd_set set, int fdmax);
void setup_listeners();
void close_con(int con_fd, char name[]);
void print_connected(void);
int accept_connection(int socket, int listener_port);
int print_send(char *input, fd_set suggested);

####################
##  Introduction  ##
####################
penn_daq.c: a DAQ program for the SNO+ experiment.
Based off of code by Paul Keener written for the original SNO experiment, 
heavily modifed by Peter Downs (August 2010)

Note-   penn_daq.c is a stream socket server. If this means nothing to you, I highly recommend
	reading:
		http://beej.us/guide/bgnet/
	Seriously, go and read it. Quite an excellent guide. The majority of penn_daq is based off
	of his examples.

###################
##  Description  ##
###################
penn_daq.c is a server which accepts stream connections from 4 different types of clients:
	1. Controller client - this client sends commands which are then parsed and executed
	2. Single Board Computer / Master Trigger Controller (SBC/MTC) - a board which can be written
	   to and have commands done to it, it also can be polled for data
	3. XL3 board - an update of the XL2 board used in SNO, the XL3 takes data from something like
	   16 different PMT data collection boards and sends it to penn_daq. 
	4. View client - not yet implemented, but a client from which no data is received. The idea is that
	   any time there is a print statement in penn_daq, or data is displayed on screen, that text should
	   also be sent to the viewer.
The main purpose of penn_daq is to send commands to the XL3 boards and the SBC/MTC and receive data back.
penn_daq uses the select() function to deal with connections closing when not expected to. Instead of just
constantly blocking on a recv() function, polling select() allows the program to handle data as it comes in
in nearly real time. Now, as opposed to the original, when a client disconnects nothing needs to be reset- 
penn_daq just alerts the user to what disconnected and keeps chugging along. This is most definitely a good thing.

#################
##  TODO List  ##
#################
nothing to do!

################
##  Overview  ##
################
(0.1)		INCLUDES
(0.2)		DEFINITIONS
(0.3)		GLOBALS
(1.0)		SETUP
(1.1)			Initialize Variables
(1.2)			Clear fd_set's			
(1.3)			Bind Non-XL3 Listeners
(1.4)			Add Non-XL3 Listeners
(1.5)			Bind/Add XL3 Listeners
(2.0) 		MAIN LOOP
(2.1)			Copy fd_set's for select()
(2.2)		 	Select()
(2.3)			Read Incoming Data
(2.3.1)				Accept/Reject New Connections
(2.3.1.1)					XL3 request
(2.3.1.2)					Controller request
(2.3.1.3)					Viewer request
(2.3.1.4)					SBC/MTC request
(2.3.2)				Process New Data
(2.3.2.1)					XL3 data
(2.3.2.2)					Controller data
(2.3.2.3)					SBC/MTC data
(2.3.2.4)					Viewer data
(3.0)		NEW FUNCTIONS
(3.A)			void *get_in_addr
(3.B)			int bind_listener
(3.C)			void reject_connection
(3.D)			int accept_connection
(3.E)			void close_con
(3.F)			int numb_fds
(3.G)			int get_xl3_location
(3.H)			void print_connected_xl3s
(3.I)			void set_delay_values
(3.J)			int connect_to_SBC
(3.K)			int print_send
(3.L)			void sigint_func
(3.M)			void start_logging
(3.N)			void stop_logging
(4.0)		LEGACY FUNCTIONS

For descriptions of each section, please refer to the source code; A simple C-f
(or /, or C-s) for the number in parentheses of the section you want should point
you in the right direction. For new, non-legacy functions, see below.

################################
##  New Function Information  ##
################################
All of this information (including examples) can be found
in the source code, but here it is in one place, nicely formatted.


(3.A): void *get_in_addr(struct sockaddr *sa)
	This function gets the address of the connection associated with sockaddr.
	Stolen directly from beej.us (see note in Introduction).

	Example:
	printf("new_daq: connection: XL3 (port %d, socket %d, from %s)\n", listener_port,
	       newfd, inet_ntop(remoteaddr.ss_family,
	       get_in_addr((struct sockaddr*)&remoteaddr), remoteIP, INET6_ADDRSTRLEN), newfd);

(3.B): int bind_listener(char *host, char *port)
	Given a host and a port, return a file descriptor (basically an integer)
	/ socket to listen on that port for new requests (such as clients trying
	to connect).

	Example:
	sbc_listener = bind_listener(argv[1], SBC_PORT);

(3.C): void reject_connection(int socket, int listener_port, int connections, int max_con, char name[])
	This function rejects a connection to a specific port (listener_port). It tells the client trying
	to connect that there are too many connections to the port already, prints the number of
	connections to the screen, and prints that it rejected a connection. It is not that fancy, and
	doesn't do anything too special, but makes the rejection look nice and, because there are a
	couple of different places where a connection needs to be rejected in the code, saves some space.
	The way this is actually accomplished is by accepting the connection, writing the message to it,
	and then immediately closing it (in effect, rejecting it).

	Example:
	reject_connection(cont_listener, atoi(CONT_PORT), t-1, MAX_CONT_CON, "CONTROLLER");

(3.D): int accept_connection(int socket, int listener_port)
	This function, given a socket and a listener port, accept()s the new socket and adds it to
	the appropriate socket file descriptor sets (fd_sets). It keps track of the highest file
	descriptor (fdmax) and changes it appropriately, too. Returns a file descriptor (an integer)
	to the new socket.

	Example:
	new_fd = accept_connection(a, get_xl3_location(a, xl3_listener_array)+atoi(XL3_PORT));

(3.E): void close_con(int con_fd, char name[])
	This is a nice, pretty function which closes a socket connection and removes it from the fd_sets
	it had belonged to. It does some specific stuff based on whether the socket was an XL3 or SBC/MTC,
	but really it's just to save space in the code.

	Example:
	close_con(a, "XL3");

(3.F): int num_fds(fd_set set, int fdmax)
	For a given fd_set, return how many file descriptors (in this case sockets) are associated with it.

	Example:
	t = num_fds(cont_fdset, fdmax);

(3.G): int get_xl3_location(int value, int array[])
	Ok, so this function just returns the index of /value/ within /array/. But if value is an xl3
	socket file descriptor and array is connected_xl3s[], it returns the crate number.
	***THIS IS IMPORTANT TO KNOW AND REMEMBER***
	That is how you'll see it used most often within the body of the program. But it will work with any
	integer value and array of integers. You'll also see it used when assigning the crate number of an XL3-
	by finding the location of the socket a request came in on in xl3_listener_array[], you can find the
	crate number.

	Example:
	if(get_xl3_location(a, xl3_listener_array) >= 0){

(3.H): void print_connected_xl3s(void)
	Print out every connected XL3 board's:
		crate #
		port # (XL3_PORT + crate # - 1)
		socket (not really necessary)
	Can be called with print_xl3s through the command terminal.

	Example:
	else if (strncmp(buffer, "print_xl3s", 10) == 0){
		print_connected_xl3s();
	}

(3.I): void set_delay_values(int seconds, int useconds)
	This sets the global tv structure delay_value for use suring select() to time out after
	seconds and useconds amount of time. Because every call of select using delay_value
	resets it to zero, I made a function which resets it. You'll notice that after any select()
	call with a timeout value of &delay_value this function is called to reset the delay_value
	structure.

	Example:
	data=select(fdmax+1, &readable_fdset, NULL, NULL, &delay_value);
	set_delay_values(SECONDS, USECONDS);

(3.J): int connect_to_SBC(int portno, struct hostent *server)
	This function connects to the SBC/MTC, which will not connect automatically. Although there
	is a section of the main listener code where it will try to connect to an SBC/MTC if it receives
	a request, the penn_daq program will never receive this request. To connect to the SBC call
	"connect_to_SBC" from the control terminal. This function ssh's into the SBC, starts the OrcaReadout
	program, and then tries (from the SBC/MTC ssh session) connect to penn_daq. After penn_daq 
	receives that request, it accept()s and sets up the connection correctly.

	Example:
	else if (strncmp(buffer, "connect_to_SBC", 14) == 0){
		int x;
		x = connect_to_SBC(atoi(SBC_PORT), server);
		if (x != 0){
			fprintf(stderr, "error connecting to SBC\n");
		}
	}
(3.K): int print_send(char *input, fd_set suggested)
	This is a printf() replacement function that:
	1. prints the given string(/input/)
	2. attempts to send the given string(/input/) to all of
	   the file descriptors in fd_set /suggested/.
	 
	print_send() accesses the global "fdmax", defined in penn_daq.c, for its select() function.
	print_send() also accessed the global "write_log", defined in penn_daq.c, to determine
	whether or not to write all output to a log file, too.
	
	By sprintf()ing any message into a buffer and feeding that buffer, along with
	a fd_set to look in for writeable sockets, this function can be used to send to
	the connected viewers any message that is also printed to the screen. 
	
	If one wanted to also send all of these messages to the controller, or, say, 
	another type of client, all that needs to be done is when accepting them
	(in accept_connection(), most likely) add:
	FD_SET(socket, &view_fdset);
	where "socket" is the socket of the new connection.
	
	Example:
	else{
		print_send("not a valid command\n", view_fdset)
	}

(3.L): void sigint_func(int sig)
	A simple signal handler- whenever a SIGINT is thrown,
	penn_daq catches it and runs this function instead of just
	exiting immediately. This function makes sure that all
	open connections/files are closed correctly. It also moves
	any logs in the /daq directory to /daq/logs. In the program,
	sigint_func(SIGINT) has replaced all calls of exit().

	For this function to be called, at the beginning of main()
	we call:
		(void) signal(SIGINT, sigint_func);
	This is what actually tells the program to use this function
	to handle SIGINTs.

	Example:
	else{
		fprintf(stderr, "usage: ./penn_daq hostname [-q or --quiet] \n"); // argv[1] is hostname
		fprintf(stderr, "for more help, use \'-h\' or \'--help\' as the argument or open ./README.TXT\n");
		leave(SIGINT);
	}

(3.M): void start_logging(void)
	Opens up a time stamped log file which print_send()
	tries to write to every time it is called.
	Format is:
		log_name = "Year_Month_Day_(Hour.Minute.Second.Milliseconds).log"
	
	All log files are created in /daq. As the program quits, they are moved
	to /daq/LOGS.

	Example:
	if(write_log && ps_log_file){
		fprintf(ps_log_file, "%s", input);
	}

(3.N): void stop_logging(void)
	This function does two things:
	1. Tells the program to stop logging future output
	2. Closes all existing log files and moves them to
	   /daq/LOGS
	
	Example:
	else if (strncmp(buffer, "stop_logging", 12) == 0){
		stop_logging();
	}
############
##  Help  ##
############
If, for some reason, there's a problem that can't be resolved by checking this readme, reading the source code,
or talking to anyone still working at Penn, feel free to contact me (Peter Downs) at peter.l.downs@gmail.com

/*++
/* NAME
/*	multi_server 3
/* SUMMARY
/*	skeleton multi-threaded mail subsystem
/* SYNOPSIS
/*	#include <mail_server.h>
/*
/*	NORETURN multi_server_main(argc, argv, service, key, value, ...)
/*	int	argc;
/*	char	**argv;
/*	void	(*service)(VSTREAM *stream, char *service_name, char **argv);
/*	int	key;
/*
/*	void	multi_server_disconnect(stream, argv)
/*	VSTREAM *stream;
/*	char	**argv;
/* DESCRIPTION
/*	This module implements a skeleton for multi-threaded
/*	mail subsystems: mail subsystem programs that service multiple
/*	clients at the same time. The resulting program expects to be run
/*	from the \fBmaster\fR process.
/*
/*	multi_server_main() is the skeleton entry point. It should be
/*	called from the application main program.  The skeleton does all
/*	the generic command-line processing, initialization of
/*	configurable parameters, and connection management.
/*	The skeleton never returns.
/*
/*	Arguments:
/* .IP "void (*service)(VSTREAM *stream, char *service_name, char **argv)"
/*	A pointer to a function that is called by the skeleton each
/*	time a client sends data to the program's service port. The
/*	function is run after the program has optionally dropped its
/*	privileges. This function should not attempt to preserve state
/*	across calls. The stream initial state is non-blocking mode.
/*	The service name argument corresponds to the service name in the
/*	master.cf file.
/*	The argv argument specifies command-line arguments left over
/*	after options processing.
/* .PP
/*	Optional arguments are specified as a null-terminated (key, value)
/*	list. Keys and expected values are:
/* .IP "MAIL_SERVER_INT_TABLE (CONFIG_INT_TABLE *)"
/*	A table with configurable parameters, to be loaded from the
/*	global Postfix configuration file. Tables are loaded in the
/*	order as specified, and multiple instances of the same type
/*	are allowed.
/* .IP "MAIL_SERVER_STR_TABLE (CONFIG_STR_TABLE *)"
/*	A table with configurable parameters, to be loaded from the
/*	global Postfix configuration file. Tables are loaded in the
/*	order as specified, and multiple instances of the same type
/*	are allowed.
/* .IP "MAIL_SERVER_BOOL_TABLE (CONFIG_BOOL_TABLE *)"
/*	A table with configurable parameters, to be loaded from the
/*	global Postfix configuration file. Tables are loaded in the
/*	order as specified, and multiple instances of the same type
/*	are allowed.
/* .IP "MAIL_SERVER_RAW_TABLE (CONFIG_STR_TABLE *)"
/*	A table with configurable parameters, to be loaded from the
/*	global Postfix configuration file. Tables are loaded in the
/*	order as specified, and multiple instances of the same type
/*	are allowed. Raw parameters are not subjected to $name
/*	evaluation.
/* .IP "MAIL_SERVER_PRE_INIT (void *(char *service_name, char **argv))"
/*	A pointer to a function that is called once
/*	by the skeleton after it has read the global configuration file
/*	and after it has processed command-line arguments, but before
/*	the skeleton has optionally relinquished the process privileges.
/* .sp
/*	Only the last instance of this parameter type is remembered.
/* .IP "MAIL_SERVER_POST_INIT (void *(char *service_name, char **argv))"
/*	A pointer to a function that is called once
/*	by the skeleton after it has optionally relinquished the process
/*	privileges, but before servicing client connection requests.
/* .sp
/*	Only the last instance of this parameter type is remembered.
/* .IP "MAIL_SERVER_LOOP (int *(char *service_name, char **argv))"
/*	A pointer to function that is executed from
/*	within the event loop, whenever an I/O or timer event has happened,
/*	or whenever nothing has happened for a specified amount of time.
/*	The result value of the function specifies how long to wait until
/*	the next event. Specify -1 to wait for "as long as it takes".
/* .sp
/*	Only the last instance of this parameter type is remembered.
/* .IP "MAIL_SERVER_EXIT (void *(char *service_name, char **argv))"
/*	A pointer to function that is executed immediately before normal
/*	process termination.
/* .IP "MAIL_SERVER_PRE_ACCEPT (void *(char *service_name, char **argv))"
/*	Function to be executed prior to accepting a new connection.
/* .sp
/*	Only the last instance of this parameter type is remembered.
/* .PP
/*	multi_server_disconnect() should be called by the application
/*	when a client disconnects.
/*
/*	The var_use_limit variable limits the number of clients that
/*	a server can service before it commits suicide.
/*	This value is taken from the global \fBmain.cf\fR configuration
/*	file. Setting \fBvar_use_limit\fR to zero disables the client limit.
/*
/*	The var_idle_limit variable limits the time that a service
/*	receives no client connection requests before it commits suicide.
/*	This value is taken from the global \fBmain.cf\fR configuration
/*	file. Setting \fBvar_use_limit\fR to zero disables the idle limit.
/* DIAGNOSTICS
/*	Problems and transactions are logged to \fBsyslogd\fR(8).
/* SEE ALSO
/*	master(8), master process
/*	syslogd(8) system logging
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/

/* System library. */

#include <sys_defs.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#ifdef STRCASECMP_IN_STRINGS_H
#include <strings.h>
#endif

/* Utility library. */

#include <msg.h>
#include <msg_syslog.h>
#include <chroot_uid.h>
#include <listen.h>
#include <events.h>
#include <vstring.h>
#include <vstream.h>
#include <msg_vstream.h>
#include <mymalloc.h>
#include <iostuff.h>
#include <stringops.h>
#include <sane_accept.h>
#include <myflock.h>
#include <safe_open.h>
#include <listen.h>
#include <watchdog.h>

/* Global library. */

#include <mail_task.h>
#include <debug_process.h>
#include <mail_params.h>
#include <mail_conf.h>
#include <timed_ipc.h>
#include <resolve_local.h>

/* Process manager. */

#include "master_proto.h"

/* Application-specific */

#include "mail_server.h"

 /*
  * Global state.
  */
static int client_count;
static int use_count;

static void (*multi_server_service) (VSTREAM *, char *, char **);
static char *multi_server_name;
static char **multi_server_argv;
static void (*multi_server_accept) (int, char *);
static void (*multi_server_onexit) (char *, char **);
static void (*multi_server_pre_accept) (char *, char **);
static VSTREAM *multi_server_lock;

/* multi_server_exit - normal termination */

static NORETURN multi_server_exit(void)
{
    if (multi_server_onexit)
	multi_server_onexit(multi_server_name, multi_server_argv);
    exit(0);
}

/* multi_server_abort - terminate after abnormal master exit */

static void multi_server_abort(int unused_event, char *unused_context)
{
    if (msg_verbose)
	msg_info("master disconnect -- exiting");
    multi_server_exit();
}

/* multi_server_timeout - idle time exceeded */

static void multi_server_timeout(int unused_event, char *unused_context)
{
    if (msg_verbose)
	msg_info("idle timeout -- exiting");
    multi_server_exit();
}

/* multi_server_disconnect - terminate client session */

void    multi_server_disconnect(VSTREAM *stream)
{
    if (msg_verbose)
	msg_info("connection closed fd %d", vstream_fileno(stream));
    event_disable_readwrite(vstream_fileno(stream));
    (void) vstream_fclose(stream);
    client_count--;
    use_count++;
}

/* multi_server_execute - in case (char *) != (struct *) */

static void multi_server_execute(int unused_event, char *context)
{
    VSTREAM *stream = (VSTREAM *) context;

    if (multi_server_lock != 0
	&& myflock(vstream_fileno(multi_server_lock), MYFLOCK_NONE) < 0)
	msg_fatal("select unlock: %m");

    /*
     * Do not bother the application when the client disconnected.
     */
    if (peekfd(vstream_fileno(stream)) > 0) {
	multi_server_service(stream, multi_server_name, multi_server_argv);
    } else {
	multi_server_disconnect(stream);
    }
    if (client_count == 0 && var_idle_limit > 0)
	event_request_timer(multi_server_timeout, (char *) 0, var_idle_limit);
}

/* multi_server_wakeup - wake up application */

static void multi_server_wakeup(int fd)
{
    VSTREAM *stream;

    if (msg_verbose)
	msg_info("connection established fd %d", fd);
    non_blocking(fd, BLOCKING);
    close_on_exec(fd, CLOSE_ON_EXEC);
    client_count++;
    stream = vstream_fdopen(fd, O_RDWR);
    timed_ipc_setup(stream);
    event_enable_read(fd, multi_server_execute, (char *) stream);
}

/* multi_server_accept_local - accept client connection request */

static void multi_server_accept_local(int unused_event, char *context)
{
    int     listen_fd = (int) context;
    int     time_left = -1;
    int     fd;

    /*
     * Be prepared for accept() to fail because some other process already
     * got the connection (the number of processes competing for clients is
     * kept small, so this is not a "thundering herd" problem). If the
     * accept() succeeds, be sure to disable non-blocking I/O, in order to
     * minimize confusion.
     */
    if (client_count == 0 && var_idle_limit > 0)
	time_left = event_cancel_timer(multi_server_timeout, (char *) 0);

    if (multi_server_pre_accept)
	multi_server_pre_accept(multi_server_name, multi_server_argv);
    fd = LOCAL_ACCEPT(listen_fd);
    if (multi_server_lock != 0
	&& myflock(vstream_fileno(multi_server_lock), MYFLOCK_NONE) < 0)
	msg_fatal("select unlock: %m");
    if (fd < 0) {
	if (errno != EAGAIN)
	    msg_fatal("accept connection: %m");
	if (time_left >= 0)
	    event_request_timer(multi_server_timeout, (char *) 0, time_left);
	return;
    }
    multi_server_wakeup(fd);
}

/* multi_server_accept_inet - accept client connection request */

static void multi_server_accept_inet(int unused_event, char *context)
{
    int     listen_fd = (int) context;
    int     time_left = -1;
    int     fd;

    /*
     * Be prepared for accept() to fail because some other process already
     * got the connection (the number of processes competing for clients is
     * kept small, so this is not a "thundering herd" problem). If the
     * accept() succeeds, be sure to disable non-blocking I/O, in order to
     * minimize confusion.
     */
    if (client_count == 0 && var_idle_limit > 0)
	time_left = event_cancel_timer(multi_server_timeout, (char *) 0);

    if (multi_server_pre_accept)
	multi_server_pre_accept(multi_server_name, multi_server_argv);
    fd = inet_accept(listen_fd);
    if (multi_server_lock != 0
	&& myflock(vstream_fileno(multi_server_lock), MYFLOCK_NONE) < 0)
	msg_fatal("select unlock: %m");
    if (fd < 0) {
	if (errno != EAGAIN)
	    msg_fatal("accept connection: %m");
	if (time_left >= 0)
	    event_request_timer(multi_server_timeout, (char *) 0, time_left);
	return;
    }
    multi_server_wakeup(fd);
}

/* multi_server_main - the real main program */

NORETURN multi_server_main(int argc, char **argv, MULTI_SERVER_FN service,...)
{
    char   *myname = "multi_server_main";
    VSTREAM *stream = 0;
    char   *root_dir = 0;
    char   *user_name = 0;
    int     debug_me = 0;
    char   *service_name = basename(argv[0]);
    int     delay;
    int     c;
    int     socket_count = 1;
    int     fd;
    va_list ap;
    MAIL_SERVER_INIT_FN pre_init = 0;
    MAIL_SERVER_INIT_FN post_init = 0;
    MAIL_SERVER_LOOP_FN loop = 0;
    int     key;
    char   *transport = 0;
    char   *lock_path;
    VSTRING *why;
    int     alone = 0;
    WATCHDOG *watchdog;

    /*
     * Process environment options as early as we can.
     */
    if (getenv(CONF_ENV_VERB))
	msg_verbose = 1;
    if (getenv(CONF_ENV_DEBUG))
	debug_me = 1;

    /*
     * Don't die when a process goes away unexpectedly.
     */
    signal(SIGPIPE, SIG_IGN);

    /*  
     * Don't die for frivolous reasons.
     */
#ifdef SIGXFSZ
    signal(SIGXFSZ, SIG_IGN);
#endif

    /*
     * May need this every now and then.
     */
    var_procname = mystrdup(basename(argv[0]));
    set_mail_conf_str(VAR_PROCNAME, var_procname);

    /*
     * Initialize logging and exit handler. Do the syslog first, so that its
     * initialization completes before we enter the optional chroot jail.
     */
    msg_syslog_init(mail_task(var_procname), LOG_PID, LOG_FACILITY);
    if (msg_verbose)
	msg_info("daemon started");

    /*
     * Initialize from the configuration file. Allow command-line options to
     * override compiled-in defaults or configured parameter values.
     */
    mail_conf_read();
    va_start(ap, service);
    while ((key = va_arg(ap, int)) != 0) {
	switch (key) {
	case MAIL_SERVER_INT_TABLE:
	    get_mail_conf_int_table(va_arg(ap, CONFIG_INT_TABLE *));
	    break;
	case MAIL_SERVER_STR_TABLE:
	    get_mail_conf_str_table(va_arg(ap, CONFIG_STR_TABLE *));
	    break;
	case MAIL_SERVER_BOOL_TABLE:
	    get_mail_conf_bool_table(va_arg(ap, CONFIG_BOOL_TABLE *));
	    break;
	case MAIL_SERVER_RAW_TABLE:
	    get_mail_conf_raw_table(va_arg(ap, CONFIG_STR_TABLE *));
	    break;
	case MAIL_SERVER_PRE_INIT:
	    pre_init = va_arg(ap, MAIL_SERVER_INIT_FN);
	    break;
	case MAIL_SERVER_POST_INIT:
	    post_init = va_arg(ap, MAIL_SERVER_INIT_FN);
	    break;
	case MAIL_SERVER_LOOP:
	    loop = va_arg(ap, MAIL_SERVER_LOOP_FN);
	    break;
	case MAIL_SERVER_EXIT:
	    multi_server_onexit = va_arg(ap, MAIL_SERVER_EXIT_FN);
	    break;
	case MAIL_SERVER_PRE_ACCEPT:
	    multi_server_pre_accept = va_arg(ap, MAIL_SERVER_ACCEPT_FN);
	    break;
	default:
	    msg_panic("%s: unknown argument type: %d", myname, key);
	}
    }
    va_end(ap);

    /*
     * Pick up policy settings from master process. Shut up error messages to
     * stderr, because no-one is going to see them.
     */
    opterr = 0;
    while ((c = GETOPT(argc, argv, "cDi:lm:n:s:St:uv")) > 0) {
	switch (c) {
	case 'c':
	    root_dir = var_queue_dir;
	    break;
	case 'D':
	    debug_me = 1;
	    break;
	case 'i':
	    if ((var_idle_limit = atoi(optarg)) <= 0)
		msg_fatal("invalid max_idle time: %s", optarg);
	    break;
	case 'l':
	    alone = 1;
	    break;
	case 'm':
	    if ((var_use_limit = atoi(optarg)) <= 0)
		msg_fatal("invalid max_use: %s", optarg);
	    break;
	case 'n':
	    service_name = optarg;
	    break;
	case 's':
	    if ((socket_count = atoi(optarg)) <= 0)
		msg_fatal("invalid socket_count: %s", optarg);
	    break;
	case 'S':
	    stream = VSTREAM_IN;
	    break;
	case 'u':
	    user_name = var_mail_owner;
	    break;
	case 't':
	    transport = optarg;
	    break;
	case 'v':
	    msg_verbose++;
	    break;
	default:
	    msg_fatal("invalid option: %c", c);
	    break;
	}
    }

    /*
     * If not connected to stdin, stdin must not be a terminal.
     */
    if (stream == 0 && isatty(STDIN_FILENO)) {
	msg_vstream_init(var_procname, VSTREAM_ERR);
	msg_fatal("do not run this command by hand");
    }

    /*
     * Can options be required?
     */
    if (stream == 0) {
	if (transport == 0)
	    msg_fatal("no transport type specified");
	if (strcasecmp(transport, MASTER_XPORT_NAME_INET) == 0)
	    multi_server_accept = multi_server_accept_inet;
	else if (strcasecmp(transport, MASTER_XPORT_NAME_UNIX) == 0)
	    multi_server_accept = multi_server_accept_local;
	else
	    msg_fatal("unsupported transport type: %s", transport);
    }

    /*
     * Optionally start the debugger on ourself.
     */
    if (debug_me)
	debug_process();

    /*
     * Traditionally, BSD select() can't handle multiple processes selecting
     * on the same socket, and wakes up every process in select(). See TCP/IP
     * Illustrated volume 2 page 532. We avoid select() collisions with an
     * external lock file.
     */
    if (stream == 0 && !alone) {
	lock_path = concatenate(DEF_PID_DIR, "/", transport,
				".", service_name, (char *) 0);
	why = vstring_alloc(1);
	if ((multi_server_lock = safe_open(lock_path, O_CREAT | O_RDWR, 0600,
					   -1, -1, why)) == 0)
	    msg_fatal("%s", vstring_str(why));
	close_on_exec(vstream_fileno(multi_server_lock), CLOSE_ON_EXEC);
	myfree(lock_path);
	vstring_free(why);
    }

    /*
     * Run pre-jail initialization.
     */
    if (pre_init)
	pre_init(multi_server_name, multi_server_argv);

    /*
     * Optionally, restrict the damage that this process can do.
     */
    if (chdir(var_queue_dir) < 0)
	msg_fatal("chdir(\"%s\"): %m", var_queue_dir);
    resolve_local_init();
    chroot_uid(root_dir, user_name);

    /*
     * Run post-jail initialization.
     */
    if (post_init)
	post_init(multi_server_name, multi_server_argv);

    /*
     * Are we running as a one-shot server with the client connection on
     * standard input? If so, make sure the output is written to stdout so as
     * to satisfy common expectation.
     */
    if (stream != 0) {
	vstream_control(stream,
			VSTREAM_CTL_DOUBLE,
			VSTREAM_CTL_WRITE_FD, STDOUT_FILENO,
			VSTREAM_CTL_END);
	service(stream, service_name, argv + optind);
	vstream_fflush(stream);
	multi_server_exit();
    }

    /*
     * Running as a semi-resident server. Service connection requests.
     * Terminate when we have serviced a sufficient number of clients, when
     * no-one has been talking to us for a configurable amount of time, or
     * when the master process terminated abnormally.
     */
    multi_server_service = service;
    multi_server_name = service_name;
    multi_server_argv = argv + optind;
    if (var_idle_limit > 0)
	event_request_timer(multi_server_timeout, (char *) 0, var_idle_limit);
    for (fd = MASTER_LISTEN_FD; fd < MASTER_LISTEN_FD + socket_count; fd++) {
	event_enable_read(fd, multi_server_accept, (char *) fd);
	close_on_exec(fd, CLOSE_ON_EXEC);
    }
    event_enable_read(MASTER_STATUS_FD, multi_server_abort, (char *) 0);
    close_on_exec(MASTER_STATUS_FD, CLOSE_ON_EXEC);
    watchdog = watchdog_create(var_daemon_timeout, (WATCHDOG_FN) 0, (char *) 0);

    /*
     * The event loop, at last.
     */
    while (var_use_limit == 0 || use_count < var_use_limit || client_count > 0) {
	if (multi_server_lock != 0) {
	    watchdog_stop(watchdog);
	    if (myflock(vstream_fileno(multi_server_lock), MYFLOCK_EXCLUSIVE) < 0)
		msg_fatal("select lock: %m");
	}
	watchdog_start(watchdog);
	delay = loop ? loop(multi_server_name, multi_server_argv) : -1;
	event_loop(delay);
    }
    multi_server_exit();
}

// https://github.com/gdbinit/tcplognke/blob/master/tcplognke.c
// (useful as implementation guide)

// todo: look at saner start/stop logic here:
// https://developer.apple.com/library/content/samplecode/enetlognke/Listings/enetlognke_c.html#//apple_ref/doc/uid/DTS10003579-enetlognke_c-DontLinkElementID_4

#include <mach/mach_types.h>
#include <mach/vm_types.h>
#include <mach/kmod.h>
#include <sys/socket.h>
#include <sys/kpi_socket.h>
#include <sys/kpi_mbuf.h>
#include <sys/kpi_socket.h>
#include <sys/kpi_socketfilter.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/mbuf.h>

#include <sys/proc.h>

#include <netinet/in.h>
#include <kern/task.h>
#include <kern/locks.h>
#include <kern/assert.h>
#include <kern/debug.h>

#include <libkern/OSMalloc.h>
#include <libkern/OSAtomic.h>

#include <sys/kern_control.h>

#include "workwall.h"

#define PNAME_MAX 30

static lck_mtx_t *g_mutex = NULL;
static lck_grp_t *g_mutex_group = NULL;
static OSMallocTag g_osm_tag;

static int g_enabled = 0; // protected by g_mutex
static uint32_t g_ctl_connections = 0;

static boolean_t g_filter_registered_ip4 = FALSE;
static boolean_t g_filter_unregister_started_ip4 = FALSE;

static boolean_t g_filter_registered_ip6 = FALSE;
static boolean_t g_filter_unregister_started_ip6 = FALSE;

static boolean_t g_kern_ctl_registered = FALSE;

static kern_ctl_ref g_ctl_ref;

// TODO: might need to put struct defs above this...
// list of tcp sockets
TAILQ_HEAD(tcp_entry_list, TCPEntry);
static struct tcp_entry_list tcp_entries; // protected by g_mutex

struct TCPInfo {
    uint32_t    state;             // connection state - TLS_CONNECT_OUT or TLS_CONNECT_IN
    uint32_t    is_allowed_in;     // established during connection
    uint32_t    is_allowed_out;    // established during connection
    /*
    union {
        struct sockaddr_in	addr4; // ipv4 local addr
        struct sockaddr_in6	addr6; // ipv6 local addr
    } local;
    union {
        struct sockaddr_in	addr4; // ipv4 remote addr
        struct sockaddr_in6	addr6; // ipv6 remote addr
    } remote;
    */
    pid_t       pid;               // pid that created the socket
    char        pname[PNAME_MAX];  // name of process that create the socket
    pid_t       uid;               // user id that created the socket (unused)
    int         protocol;          // ipv4 or ipv6
};

#define TCPINFO_CONNECT_OUT	0x1
#define TCPINFO_CONNECT_IN	0x2

// todo: cull
// todo: rename props
struct TCPEntry {
    TAILQ_ENTRY(TCPEntry)   link; // link to next entry
    socket_t                so; // pointer to socket
    struct TCPInfo          info;
    uint32_t                magic; // magic value to ensure that system is passing me my buffer
};
typedef struct TCPEntry TCPEntry;

#define kTCPEntryMagic 0xAABBCCDD;

// macros to access TCPInfo fields in the TCPEntry structure
#define te_state            info.state
#define te_is_allowed_in    info.is_allowed_in
#define te_is_allowed_out   info.is_allowed_in
#define te_len              info.len
#define te_pid              info.pid
#define te_pname            info.pname
#define te_uid              info.uid
#define te_protocol         info.protocol

#define te_local4           info.local.addr4
#define te_remote4          info.remote.addr4
#define te_local6           info.local.addr6
#define te_remote6          info.remote.addr6

#pragma mark  Utility functions

static void ww_info(const char *fmt, ...)
{
    va_list listp;
    char log_buffer[92];
    va_start(listp, fmt);
    vsnprintf(log_buffer, sizeof(log_buffer), fmt, listp);
    printf("workwall %s", log_buffer);
    va_end(listp);
}

static void ww_debug(const char *fmt, ...)
{
#if DEBUG
    va_list listp;
    char log_buffer[92];
    va_start(listp, fmt);
    vsnprintf(log_buffer, sizeof(log_buffer), fmt, listp);
    printf("workwall %s", log_buffer);
    va_end(listp);
#endif
}

static struct TCPEntry * TCPEntryFromCookie(void *cookie) {
    struct TCPEntry *result;
    result = (struct TCPEntry *) cookie;
    assert(result != NULL);
    assert(result->magic == kTCPEntryMagic);
    return result;
}

#pragma mark Connection Permissions

// this kind of thing should come over ctl socket from userland,
// and should be built into a radix tree. or something.
static boolean_t is_pname_allowed(char *name) {
    if (strcmp(name, "ntpd") == 0) return true;
    if (strcmp(name, "geth") == 0) return true;
    if (strcmp(name, "VirtualBoxVM") == 0) return true;
    if (strcmp(name, "Spotify") == 0) return true;
    if (strcmp(name, "Things") == 0) return true;
    if (strcmp(name, "ruby") == 0) return true; // for screenshot uploading
    if (strcmp(name, "bztransmit") == 0) return true; // backblaze backups
    return false;
}


// Useful tool for editing this fn http://www.silisoftware.com/tools/ipconverter.php
static boolean_t is_addr_allowed_ip4(struct sockaddr_in* addr) {
    // reverse byte order of usual, but matches above tool
    uint32_t intip = htonl(addr->sin_addr.s_addr);
    uint16_t port = ntohs(addr->sin_port);
    
    if (port == 22) return TRUE; // allow ssh
    
    if (intip == 2130706433) return TRUE; // 127.0.0.1
    
    // local networks
    if(intip >= 167772160 && intip <= 184549375) return TRUE; // 10.0.0.0 to 10.255.255.255
    if(intip >= 2886729728 && intip <= 2887778303) return TRUE; // 172.16.0.0 to 172.31.255.255
    if(intip >= 3232235520 && intip <= 3232301055) return TRUE; // 192.168.0.0 to 192.168.255.255
    
    // dig workflowy.com
    if (intip == 876040851) return TRUE; // 52.55.82.147
    if (intip == 885986970) return TRUE; // 52.207.22.154
    if (intip == 873784021) return TRUE; // 52.20.226.213
    if (intip == 915946831) return TRUE; // 54.152.61.79
    
    // for workflowy: dig fonts.googleapis.com (and dig subsequent CNAME multiple times)
    if (intip == 3627729738) return TRUE; // 216.58.195.74
    if (intip == 2899903850) return TRUE; // 172.217.5.106
    if (intip == 3627729578) return TRUE; // 216.58.194.170
    if (intip == 3627728906) return TRUE; // 216.58.192.10
    
    /*
    unsigned char addstr[256];
    inet_ntop(AF_INET, &addr->sin_addr, (char*)addstr, sizeof(addstr));
    printf("%s:%d\n", addstr, ntohs(addr->sin_port));
    */

    return FALSE;
}

static boolean_t can_connect_in(struct TCPEntry *entry, const struct sockaddr *from) {
    if (is_pname_allowed(entry->te_pname)) {
        ww_debug("allowed in due to pname (%s)\n", entry->te_pname);
        return true;
    }

    if (entry->te_protocol == AF_INET) {
        // ip4
        return is_addr_allowed_ip4((struct sockaddr_in*)from);
    }
    else {
        // ip6
        ww_debug("disallowed in ip6 (%s)\n", entry->te_pname);
    }
    return false;
}

static boolean_t can_connect_out(struct TCPEntry *entry, const struct sockaddr *to) {
    if (is_pname_allowed(entry->te_pname)) {
        ww_debug("allowed out due to pname (%s)\n", entry->te_pname);
        return true;
    }
    
    if (entry->te_protocol == AF_INET) {
        // ip4
        //ww_debug("disallowed out ip4 (%s) to: ", entry->te_pname);
        return is_addr_allowed_ip4((struct sockaddr_in*)to);
    }
    else {
        // ip6
        ww_debug("disallowed out ip6 (%s)\n", entry->te_pname);
    }
    return false;
}


#pragma mark Socket Filter Functions

/*!
 @typedef ww_attach_locked
 
 @discussion attach_locked is called by both sf_attach_funcs to initialize internel
 memory structures - assumption that the fine grain lock associated with the
 tcp_entries queue is held so that the queue entry can be inserted atomically.
 @param so - The socket the filter is being attached to.
 entry - pointer to ths log entry structure to be associated with this
 socket reference for future socket filter calls
 */
static void ww_attach_locked(socket_t so, struct TCPEntry *entry)
{
    bzero(entry, sizeof (*entry));
    entry->so = so;
    entry->magic = kTCPEntryMagic; // set the magic cookie for debugging purposes only to verify that the system
    // only returns memory that I allocated.
    
    // attach time is a good time to identify the calling process ID.
    // could also make the proc_self call to obtain the proc_t value which is useful
    // to get the ucred structure.
    // important note: the pid associated with this socket is the pid of the process which created the
    // socket. The socket may have been passed to another process with a different pid.
    entry->te_pid = proc_selfpid();
    proc_selfname(entry->te_pname, PNAME_MAX);
    //entry->te_uid = kauth_getuid();
    ww_debug("attaching to socket for process: %s\n", entry->te_pname);
    TAILQ_INSERT_TAIL(&tcp_entries, entry, link);
}

/*!
 @typedef sf_attach_func
 
 @discussion sf_attach_func is called to notify the filter it has
 been attached to a new TCP socket. The filter may allocate memory for
 this attachment and use the cookie to track it. We attach to every
 socket, because depending on where it tries to connect, we'll need
 to block it.
 @param cookie Used to allow the socket filter to set the cookie for
 this attachment.
 @param so The socket the filter is being attached to.
 @result If you return a non-zero value, your filter will not be
 attached to this socket.
 */
static errno_t ww_attach_ip4(void **cookie, socket_t so)
{
    struct TCPEntry *entry;
    errno_t	result = 0;
    
    entry = OSMalloc(sizeof (struct TCPEntry), g_osm_tag);
    if (entry == NULL) {
        return ENOBUFS;
    }
    
    /* save the log entry as the cookie associated with this socket ref */
    *(struct TCPEntry**)cookie = entry;
    
    lck_mtx_lock(g_mutex);
    ww_attach_locked(so, entry);
    // set fields after: ww_attach_locked clears the entry structure
    entry->te_protocol = AF_INET; // indicate ipv4
    lck_mtx_unlock(g_mutex);
    
    return result;
}

static errno_t ww_attach_ip6(void **cookie, socket_t so)
{
    struct TCPEntry *entry;
    errno_t	result = 0;
    
    entry = OSMalloc(sizeof (struct TCPEntry), g_osm_tag);
    if (entry == NULL) {
        return ENOBUFS;
    }
    
    /* save the log entry as the cookie associated with this socket ref */
    *(struct TCPEntry**)cookie = entry;
    
    lck_mtx_lock(g_mutex);
    ww_attach_locked(so, entry);
    // set fields after: ww_attach_locked clears the entry structure
    entry->te_protocol = AF_INET6; // indicate ipv6
    lck_mtx_unlock(g_mutex);
    
    return result;
}


/*
 @typedef sf_detach_func
 
 @discussion sf_detach_func is called to notify the filter it has
 been detached from a socket. If the filter allocated any memory
 for this attachment, it should be freed. This function will
 be called when the socket is disposed of.
 @param cookie Cookie value specified when the filter attach was
 called.
 @param so The socket the filter is attached to.
 @result If you return a non-zero value, your filter will not be
 attached to this socket.
 */
static void ww_detach(void *cookie, socket_t so)
{
    struct TCPEntry *entry = TCPEntryFromCookie(cookie);
    if (entry == NULL) {
        return;
    }
    
    lck_mtx_lock(g_mutex);
    TAILQ_REMOVE(&tcp_entries, entry, link);
    lck_mtx_unlock(g_mutex);
}

/*!
 @typedef sf_unregistered_func
 
 @discussion sf_unregistered_func is called to notify the filter it
 has been unregistered. This is the last function the stack will
 call and this function will only be called once all other
 function calls in to your filter have completed. Once this
 function has been called, your kext may safely unload.
 @param handle The socket filter handle used to identify this filter.
 */
static void ww_unregistered_ip4(sflt_handle handle) {
    g_filter_registered_ip4 = FALSE;
    ww_info("unregistered (ip4)\n");
}

static void ww_unregistered_ip6(sflt_handle handle) {
    g_filter_registered_ip6 = FALSE;
    ww_info("unregistered (ip6)\n");
}

/*
 @typedef sf_data_in_func
	
 @discussion sf_data_in_func is called to filter incoming data. If
 your filter intercepts data for later reinjection, it must queue
 all incoming data to preserve the order of the data. Use
 sock_inject_data_in to later reinject this data if you return
 EJUSTRETURN. Warning: This filter is on the data path, do not
 block or spend excessive time.
 @param cookie Cookie value specified when the filter attach was
 called.
 @param so The socket the filter is attached to.
 @param from The addres the data is from, may be NULL if the socket
 is connected.
 @param data The data being received. Control data may appear in the
 mbuf chain, be sure to check the mbuf types to find control
 data.
 @param control Control data being passed separately from the data.
 @param flags Flags to indicate if this is out of band data or a
 record.
 @result Return:
 0 - The caller will continue with normal processing of the data.
 EJUSTRETURN - The caller will stop processing the data, the data will not be freed.
 Anything Else - The caller will free the data and stop processing.
 
 Note: as this is a TCP connection, the "from" parameter will be NULL - for UDP, the
 "from" field will point to a valid sockaddr structure. In this case, you must copy
 the contents of the "from" field to local memory when swallowing the packet so that
 you have a valid sockaddr to pass in the inject call.
 */
static errno_t ww_data_in(void *cookie, socket_t so, const struct sockaddr *from,
                             mbuf_t *data, mbuf_t *control, sflt_data_flag_t flags) {
    struct TCPEntry	*entry = (struct TCPEntry *) cookie;
    
    if (from) { // see note above
        ww_info("ERROR - to field not NULL!");
    }
    
    if (!g_enabled || entry->te_is_allowed_in) {
        return 0; // allow
    }
    else {
        ww_debug("Blocking a data packet for %s", entry->te_pname);
        return 1; //block
    }
}


/*
 @typedef sf_data_out_func

 @discussion sf_data_out_func is called to filter outbound data. If
 your filter intercepts data for later reinjection, it must queue
 all outbound data to preserve the order of the data when
 reinjecting. Use sock_inject_data_out to later reinject this
 data. Warning: This filter is on the data path, do not block or
 spend excessive time.
 @param cookie Cookie value specified when the filter attach was
 called.
 @param so The socket the filter is attached to.
 @param from The address the data is from, may be NULL if the socket
 is connected.
 @param data The data being received. Control data may appear in the
 mbuf chain, be sure to check the mbuf types to find control
 data.
 @param control Control data being passed separately from the data.
 @param flags Flags to indicate if this is out of band data or a
 record.
 @result Return:
 0 - The caller will continue with normal processing of the data.
 EJUSTRETURN - The caller will stop processing the data, the data will not be freed.
 Anything Else - The caller will free the data and stop processing.

 Note: as this is a TCP connection, the "to" parameter will be NULL - for UDP, the
 "to" field will point to a valid sockaddr structure. In this case, you must copy
 the contents of the "to" field to local memory when swallowing the packet so that
 you have a valid sockaddr to pass in the inject call.
 */
static errno_t ww_data_out(void *cookie, socket_t so, const struct sockaddr *to,
                           mbuf_t *data, mbuf_t *control, sflt_data_flag_t flags) {
    struct TCPEntry	*entry = (struct TCPEntry *) cookie;

    if (to) { // see note above
        ww_info("ERROR - to field not NULL!");
    }
    
    if (!g_enabled || entry->te_is_allowed_out) {
        return 0; // allow
    }
    else {
        ww_debug("Blocking a data packet for %s", entry->te_pname);
        return 1; //block
    }
}


/*
 @typedef sf_connect_in_func
 
 @discussion sf_connect_in_func is called to filter data_inbound
 connections. A protocol will call this before accepting an
 incoming connection and placing it on the queue of completed
 connections. Warning: This filter is on the data path, do not
 block or spend excessive time.
 @param cookie Cookie value specified when the filter attach was
 called.
 @param so The socket the filter is attached to.
 @param from The address the incoming connection is from.
 @result Return:
 0 - The caller will continue with normal processing of the connection.
 Anything Else - The caller will rejecting the incoming connection.
 */
static errno_t ww_connect_in(void *cookie, socket_t so, const struct sockaddr *from)
{
    struct TCPEntry *entry = TCPEntryFromCookie(cookie);
    
    assert((from->sa_family == AF_INET) || (from->sa_family == AF_INET6));
    OSBitOrAtomic(can_connect_in(entry, from), (UInt32*)&(entry->te_is_allowed_in));
    //OSBitOrAtomic(TCPINFO_CONNECT_IN, (UInt32*)&(entry->state)); // not used
    
    if(!g_enabled || entry->te_is_allowed_in) {
        return 0; // allow
    }
    else {
        return EPERM; // block
    }
}


/*
 @typedef sf_connect_out_func
 
 @discussion sf_connect_out_func is called to filter outbound
 connections. A protocol will call this before initiating an
 outbound connection. Warning: This filter is on the data path,
 do not block or spend excesive time.
 @param cookie Cookie value specified when the filter attach was
 called.
 @param so The socket the filter is attached to.
 @param to The remote address of the outbound connection.
 @result Return:
 0 - The caller will continue with normal processing of the connection.
 Anything Else - The caller will rejecting the outbound connection.
 */
static errno_t ww_connect_out(void *cookie, socket_t so, const struct sockaddr *to)
{
    struct TCPEntry *entry = TCPEntryFromCookie(cookie);
    
    assert((from->sa_family == AF_INET) || (from->sa_family == AF_INET6));
    OSBitOrAtomic(can_connect_out(entry, to), (UInt32*)&(entry->te_is_allowed_out));
    //OSBitOrAtomic(TCPINFO_CONNECT_IN, (UInt32*)&(entry->state)); // not used
    
    if(!g_enabled || entry->te_is_allowed_out) {
        return 0; // allow
    }
    else {
        return EPERM; // block
    }
}



#pragma mark tcplog Filter Definition

#define WORKWALL_FLT_TCP_HANDLE_IP4 'wwt4' // code should registered with Apple for distributed software
#define WORKWALL_FLT_TCP_HANDLE_IP6 'wwt6'

static struct sflt_filter socket_tcp_filter_ip4 = {
    WORKWALL_FLT_TCP_HANDLE_IP4,
    SFLT_GLOBAL,
    BUNDLE_ID,
    ww_unregistered_ip4,
    ww_attach_ip4,
    ww_detach,
    NULL,
    NULL,
    NULL,
    ww_data_in,
    ww_data_out,
    ww_connect_in,
    ww_connect_out,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static struct sflt_filter socket_tcp_filter_ip6 = {
    WORKWALL_FLT_TCP_HANDLE_IP6,
    SFLT_GLOBAL,
    BUNDLE_ID,
    ww_unregistered_ip6,
    ww_attach_ip6,
    ww_detach,
    NULL,
    NULL,
    NULL,
    ww_data_in,
    ww_data_out,
    ww_connect_in,
    ww_connect_out,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};


#pragma mark Control Functions


static errno_t ctl_connect(kern_ctl_ref ctl_ref, struct sockaddr_ctl *sac, void **unitinfo) {
    ww_info("ctl_connect - unit is %d\n", sac->sc_unit);
    OSIncrementAtomic((SInt32*)&g_ctl_connections);
    return 0;
}


/*
 @typedef ctl_disconnect_func
 @discussion The ctl_disconnect_func is used to receive notification
 that a client has disconnected from the kernel control. This
 usually happens when the socket is closed. If this is the last
 socket attached to your kernel control, you may unregister your
 kernel control from this callback.
 @param ctl_ref The control ref for the kernel control instance the client has
 disconnected from.
 @param unit The unit number of the kernel control instance the client has
 disconnected from.
 @param unitinfo The unitinfo value specified by the connect function
 when the client connected.
 */
static errno_t ctl_disconnect(kern_ctl_ref ctl_ref, u_int32_t unit, void *unitinfo) {
    ww_info("ctl_disconnect\n");
    OSDecrementAtomic((SInt32*)&g_ctl_connections);
    return 0;
}

/*
 @typedef ctl_getopt_func
 @discussion The ctl_getopt_func is used to handle client get socket
 option requests for the SYSPROTO_CONTROL option level. A buffer
 is allocated for storage and passed to your function. The length
 of that buffer is also passed. Upon return, you should set *len
 to length of the buffer used. In some cases, data may be NULL.
 When this happens, *len should be set to the length you would
 have returned had data not been NULL. If the buffer is too small,
 return an error.
 @param ctl_ref The control ref of the kernel control.
 @param unit The unit number of the kernel control instance.
 @param unitinfo The unitinfo value specified by the connect function
 when the client connected.
 @param opt The socket option.
 @param data A buffer to copy the results in to. May be NULL, see
 discussion.
 @param len A pointer to the length of the buffer. This should be set
 to the length of the buffer used before returning.
 */
static int ctl_get(kern_ctl_ref ctl_ref, u_int32_t unit, void *unitinfo, int opt,
                   void *data, size_t *len)
{
    int		error = 0;
    size_t  valsize;
    void    *buf;
    
    ww_info("ctl_get - opt is %d\n", opt);
    
    switch (opt) {
        case WORKWALL_ENABLED:
            valsize = min(sizeof(g_enabled), *len);
            buf = &g_enabled;
            break;

        default:
            error = ENOTSUP;
            break;
    }
    
    if (error == 0) {
        *len = valsize;
        if (data != NULL)
            bcopy(buf, data, valsize);
    }
    
    return error;
}

/*
 @typedef ctl_setopt_func
 @discussion The ctl_setopt_func is used to handle set socket option
 calls for the SYSPROTO_CONTROL option level.
 @param ctl_ref The control ref of the kernel control.
 @param unit The unit number of the kernel control instance.
 @param unitinfo The unitinfo value specified by the connect function
 when the client connected.
 @param opt The socket option.
 @param data A pointer to the socket option data. The data has
 already been copied in to the kernel for you.
 @param len The length of the socket option data.
 */
static int ctl_set(kern_ctl_ref ctl_ref, u_int32_t unit, void *unitinfo, int opt,
                   void *data, size_t len)
{
    int error = 0;
    int intval;
    
    ww_info("ctl_set - opt is %d\n", opt);
    
    switch (opt)
    {
        case WORKWALL_ENABLED:
            if (len < sizeof(int)) {
                error = EINVAL;
                break;
            }
            intval = *(int *)data;
            lck_mtx_lock(g_mutex);
            g_enabled = intval ? true : false;
            lck_mtx_unlock(g_mutex);
            break;
        default:
            error = ENOTSUP;
            break;
    }
    
    return error;
}


#pragma mark System Control Structure Definition

// this is not a const structure since the ctl_id field will be set when the ctl_register call succeeds
static struct kern_ctl_reg g_ctl_reg = {
    BUNDLE_ID,          // use a reverse dns name which includes a name unique to your comany
    0,                  // set to 0 for dynamically assigned control ID - CTL_FLAG_REG_ID_UNIT not set
    0,                  // ctl_unit - ignored when CTL_FLAG_REG_ID_UNIT not set
    0,                  // use CTL_FLAG_PRIVILEGED if want to require connecting user to be privleged
    0,                  // use default send size buffer
    (8 * 1024),         // Override receive buffer size
    ctl_connect,        // Called when a connection request is accepted
    ctl_disconnect,     // called when a connection becomes disconnected
    NULL,               // ctl_send_func - handles data sent from the client to kernel control
    ctl_set,            // called when the user process makes the setsockopt call
    ctl_get             // called when the user process makes the getsockopt call
};


#pragma mark kext entry functions

extern kern_return_t workwall_start (kmod_info_t *ki, void *data) {
    
    int ret;
    
    TAILQ_INIT(&tcp_entries);
    
    g_osm_tag = OSMalloc_Tagalloc(BUNDLE_ID, OSMT_DEFAULT);
    if (!g_osm_tag)
        goto bail;
    
    /* allocate mutex group and a mutex to protect global data. */
    g_mutex_group = lck_grp_alloc_init(BUNDLE_ID, LCK_GRP_ATTR_NULL);
    if (!g_mutex_group)
        goto bail;
    
    g_mutex = lck_mtx_alloc_init(g_mutex_group, LCK_ATTR_NULL);
    if (!g_mutex)
        goto bail;
    
    ret = sflt_register(&socket_tcp_filter_ip4, PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ret == KERN_SUCCESS)
        g_filter_registered_ip4 = TRUE;
    else
        goto bail;
    
    ret = sflt_register(&socket_tcp_filter_ip6, PF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (ret == KERN_SUCCESS)
        g_filter_registered_ip6 = TRUE;
    else
        goto bail;
    
    // register our control structure so that we can be found by a user level process.
    ret = ctl_register(&g_ctl_reg, &g_ctl_ref);
    if (ret == 0) {
        ww_debug("ctl_register id 0x%x, ref 0x%x \n", g_ctl_reg.ctl_id, g_ctl_ref);
        g_kern_ctl_registered = TRUE;
    }
    else {
        ww_info("ctl_register returned error %d\n", ret);
        goto bail;
    }
    
    
    ww_info("started\n");
    
    return KERN_SUCCESS;
bail:
    ww_info("bailing on start; something wrong\n");
    if (g_filter_registered_ip4) {
        sflt_unregister(WORKWALL_FLT_TCP_HANDLE_IP4);
        g_filter_registered_ip4 = FALSE;
    }
    
    if (g_filter_registered_ip6) {
        sflt_unregister(WORKWALL_FLT_TCP_HANDLE_IP6);
        g_filter_registered_ip6 = FALSE;
    }
    
    if (g_kern_ctl_registered == TRUE) {
        ret = ctl_deregister(g_ctl_ref);
        if (ret == 0) {
            g_kern_ctl_registered = FALSE;
        }
    }

    if (g_mutex) {
        lck_mtx_free(g_mutex, g_mutex_group);
        g_mutex = NULL;
    }
    if (g_mutex_group) {
        lck_grp_free(g_mutex_group);
        g_mutex_group = NULL;
    }
    if (g_osm_tag) {
        OSMalloc_Tagfree(g_osm_tag);
        g_osm_tag = NULL;
    }
    
    return KERN_FAILURE;
}

extern kern_return_t workwall_stop (kmod_info_t *ki, void *data)
{
    int ret;
    struct TCPEntry *entry, *next_entry;

    if (g_ctl_connections > 0) {
        ww_info("still connected to a control socket - quit control process\n");
        return EBUSY;
    }
    
    if (g_kern_ctl_registered == TRUE) {
        ret = ctl_deregister(g_ctl_ref);
        if (ret == 0) {
            g_kern_ctl_registered = FALSE;
        }
    }
    
    if (g_filter_registered_ip4 == TRUE && !g_filter_unregister_started_ip4) {
        sflt_unregister(WORKWALL_FLT_TCP_HANDLE_IP4);
        g_filter_unregister_started_ip4 = TRUE;
    }
    
    if (g_filter_registered_ip6 == TRUE && !g_filter_unregister_started_ip6) {
        sflt_unregister(WORKWALL_FLT_TCP_HANDLE_IP6);
        g_filter_unregister_started_ip6 = TRUE;
    }
    
    // if filter still not unregistered defer stop.
    if (g_filter_registered_ip4) {
        ww_info("waiting to stop (ip4)\n");
        return EBUSY;
    }
    
    // if filter still not unregistered defer stop.
    if (g_filter_registered_ip6) {
        ww_info("waiting to stop (ip6)\n");
        return EBUSY;
    }
    
    lck_mtx_lock(g_mutex);
    for (entry = TAILQ_FIRST(&tcp_entries); entry; entry = next_entry) {
        next_entry = TAILQ_NEXT(entry, link);
        TAILQ_REMOVE(&tcp_entries, entry, link);
        OSFree(entry, sizeof(struct TCPEntry), g_osm_tag);
    }
    lck_mtx_unlock(g_mutex);
    
    lck_mtx_free(g_mutex, g_mutex_group);
    lck_grp_free(g_mutex_group);
    g_mutex = NULL;
    g_mutex_group = NULL;
    
    OSMalloc_Tagfree(g_osm_tag);
    g_osm_tag = NULL;
    
    ww_info("stopped\n");
    
    return KERN_SUCCESS;
}

// https://github.com/gdbinit/tcplognke/blob/master/tcplognke.c
// (useful as implementation guide)

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

#include <sys/kern_control.h>

#define BUNDLE_ID "local.roboteddy.workwall"
#define PNAME_MAX 30

static lck_mtx_t *g_mutex = NULL;
static lck_grp_t *g_mutex_group = NULL;
static OSMallocTag g_osm_tag;

static boolean_t g_filter_registered_ip4 = FALSE;
static boolean_t g_filter_unregister_started_ip4 = FALSE;

static boolean_t g_filter_registered_ip6 = FALSE;
static boolean_t g_filter_unregister_started_ip6 = FALSE;

// TODO: might need to put struct defs above this...
// list of tcp sockets
TAILQ_HEAD(tcp_entry_list, TCPEntry);
static struct tcp_entry_list tcp_entries; // protected by g_mutex

struct TCPInfo {
    uint32_t    state;             // connection state - TLS_CONNECT_OUT or TLS_CONNECT_IN
    union {
        struct sockaddr_in	addr4; // ipv4 local addr
        struct sockaddr_in6	addr6; // ipv6 local addr
    } local;
    union {
        struct sockaddr_in	addr4; // ipv4 remote addr
        struct sockaddr_in6	addr6; // ipv6 remote addr
    } remote;
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
#define te_state     info.state
#define te_len       info.len
#define te_pid       info.pid
#define te_pname     info.pname
#define te_uid       info.uid
#define te_protocol  info.protocol

#define te_local4    info.local.addr4
#define te_remote4   info.remote.addr4
#define te_local6    info.local.addr6
#define te_remote6   info.remote.addr6

#pragma mark  Utility functions

static struct TCPEntry * TCPEntryFromCookie(void *cookie) {
    struct TCPEntry *result;
    result = (struct TCPEntry *) cookie;
    assert(result != NULL);
    assert(result->magic == kTCPEntryMagic);
    return result;
}

#pragma mark  Connection Permissions

// this is for ip4...
static void log_ip_and_port_addr(struct sockaddr_in* addr)
{
    unsigned char addstr[256];
    inet_ntop(AF_INET, &addr->sin_addr, (char*)addstr, sizeof(addstr));
    printf("%s:%d\n", addstr, ntohs(addr->sin_port));
}

boolean_t may_connect_in(struct TCPEntry *entry, const struct sockaddr *from) {
    printf("workwall allowing incoming connection\n");
    return true;
}

boolean_t may_connect_out(struct TCPEntry *entry, const struct sockaddr *to) {
    
    
    if (strcmp(entry->te_pname, "Spotify") == 0) {
        return true;
    }
    if (strcmp(entry->te_pname, "Things") == 0) {
        return true;
    }
    if (strcmp(entry->te_pname, "ruby") == 0) { // for screenshot uploading
        return true;
    }
    if (strcmp(entry->te_pname, "bztransmit") == 0) { // backblaze backups
        return true;
    }

    
    
    if (entry->te_protocol == AF_INET) {
        // ip4
        printf("workwall blocking ip4 connection from (%s) to: ", entry->te_pname);
        log_ip_and_port_addr((struct sockaddr_in*)to);
    }
    else {
        printf("workwall blocking ip6 connection to somewhere\n");
        // ip6
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
    printf("workwall attaching to process: %s\n", entry->te_pname);
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
    printf("workwall unregistered ip4\n");
}

static void ww_unregistered_ip6(sflt_handle handle) {
    g_filter_registered_ip6 = FALSE;
    printf("workwall unregistered ip6\n");
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
    in_port_t port;
    
    assert((from->sa_family == AF_INET) || (from->sa_family == AF_INET6));
    //OSBitOrAtomic(TCPINFO_CONNECT_IN, (UInt32*)&(entry->state)); // not used
    
    if(may_connect_in(entry, from)) {
        return 0; // allow
    }
    else {
        return EPERM; // block
    }
}

/*!
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
    in_port_t port;
    
    assert((from->sa_family == AF_INET) || (from->sa_family == AF_INET6));
    //OSBitOrAtomic(TCPINFO_CONNECT_IN, (UInt32*)&(entry->state)); // not used
    
    if(may_connect_out(entry, to)) {
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
    NULL,
    NULL,
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
    NULL,
    NULL,
    ww_connect_in,
    ww_connect_out,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
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
    
    
    printf("workwall started\n");
    
    return KERN_SUCCESS;
bail:
    printf("workwall bailing on start; something wrong\n");
    if (g_filter_registered_ip4) {
        sflt_unregister(WORKWALL_FLT_TCP_HANDLE_IP4);
        g_filter_registered_ip4 = FALSE;
    }
    
    if (g_filter_registered_ip6) {
        sflt_unregister(WORKWALL_FLT_TCP_HANDLE_IP6);
        g_filter_registered_ip6 = FALSE;
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
    struct TCPEntry *entry, *next_entry;
    
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
        printf("workwall waiting to stop (ip4)\n");
        return EBUSY;
    }
    
    // if filter still not unregistered defer stop.
    if (g_filter_registered_ip6) {
        printf("workwall waiting to stop (ip6)\n");
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
    
    printf("workwall stopped\n");
    
    return KERN_SUCCESS;
}

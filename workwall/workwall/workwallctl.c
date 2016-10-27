//
//  workwallct.c
//  workwallctl
//
//  Created by Ted Suzman on 10/26/16.
//  Copyright © 2016 Ted Suzman. All rights reserved.
//

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

#include "workwall.h"


static int g_socket = -1;

/*
	SignalHandler - implemented to handle an interrupt from the command line using Ctrl-C.
 */
static void SignalHandler(int sigraised)
{
#if DEBUG
    printf("\nworkwallctl Interrupted - %d\n", g_socket); // note - printf is unsupported function call from a signal handler
#endif
    if (g_socket > 0);
    {
#if DEBUG
        printf("closing socket %d\n", g_socket);	// note - printf is an unsupported function call from a signal handler
#endif
        close (g_socket);	// per man 2 sigaction, close can be invoked from a signal-catching function.
    }
    
    // exit(0) should not be called from a signal handler.  Use _exit(0) instead
    _exit(0);
}


static void usage(int help, const char *s) {
    printf("usage: %s [-h] [-s] [-E]\n", s);
    if (help == 0)
        return;
    printf("workwallctl is used to control the workwallnke kernel extension\n");
    printf("The command takes one of the following options,\n");
    printf(" %-10s%s\n", "-h", "display this help and exit");
    printf(" %-10s%s\n", "-s", "show status -- enabled or not");
    printf(" %-10s%s\n", "-E n", "enable log on (n > 0) or off (n = 0)");
}

int main(int argc, char * const *argv) {
    struct sockaddr_ctl sc;
    socklen_t size;
    int c;
    
    int should_get_enabled = 1;
    int enabled = -1;
    
    int should_set_enabled = 0;
    int set_enabled = -1;
    
    struct ctl_info ctl_info;
    sig_t oldHandler;
    
    // Set up a signal handler so we can clean up when we're interrupted from the command line
    // Otherwise we stay in our run loop forever.
    oldHandler = signal(SIGINT, SignalHandler);
    if (oldHandler == SIG_ERR)
        printf("Could not establish new signal handler");
    
    c = getopt(argc, argv, "sE:h");
    switch(c) {
        case 's':
            should_get_enabled = 1;
            break;
        case 'E':
            should_set_enabled = 1;
            set_enabled = strtol(optarg, 0, 0);
            break;
        case 'h':
            usage(1, argv[0]);
            exit(0);
        case '?':
        default:
            usage(0, argv[0]);
            exit(-1);
    }
    
    g_socket = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (g_socket < 0) {
        perror("socket SYSPROTO_CONTROL");
        exit(0);
    }
    bzero(&ctl_info, sizeof(struct ctl_info));
    strcpy(ctl_info.ctl_name, BUNDLE_ID);
    if (ioctl(g_socket, CTLIOCGINFO, &ctl_info) == -1) {
        perror("ioctl CTLIOCGINFO");
        exit(0);
    }/* else
        printf("ctl_id: 0x%x for ctl_name: %s\n", ctl_info.ctl_id, ctl_info.ctl_name);*/
    
    bzero(&sc, sizeof(struct sockaddr_ctl));
    sc.sc_len = sizeof(struct sockaddr_ctl);
    sc.sc_family = AF_SYSTEM;
    sc.ss_sysaddr = SYSPROTO_CONTROL;
    sc.sc_id = ctl_info.ctl_id;
    sc.sc_unit = 0;
    
    if (connect(g_socket, (struct sockaddr *)&sc, sizeof(struct sockaddr_ctl))) {
        perror("connect");
        exit(0);
    }
    
    
    if (should_set_enabled) {
        if (setsockopt(g_socket, SYSPROTO_CONTROL, WORKWALL_ENABLED, &set_enabled, sizeof(set_enabled)) == -1) {
            perror("setsockopt WORKWALL_ENABLED");
            exit(0);
        }
    }
    
    if (should_get_enabled) {
        size = sizeof(enabled);
        if (getsockopt(g_socket, SYSPROTO_CONTROL, WORKWALL_ENABLED, &enabled, &size) == -1) {
            perror("getsockopt WORKWALL_ENABLED");
            exit(0);
        }
        printf("enabled: %d\n", enabled);
    }
    
    
    close(g_socket);
    g_socket = -1;
    
    return 0;
}

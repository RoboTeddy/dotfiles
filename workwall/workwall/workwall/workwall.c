//
//  workwall.c
//  workwall
//
//  Created by Ted Suzman on 10/25/16.
//  Copyright © 2016 Ted Suzman. All rights reserved.
//

#include <mach/mach_types.h>
#include <libkern/libkern.h>

kern_return_t workwall_start(kmod_info_t * ki, void *d);
kern_return_t workwall_stop(kmod_info_t *ki, void *d);

kern_return_t workwall_start(kmod_info_t * ki, void *d)
{
    printf("workwall says hello!\n");
    return KERN_SUCCESS;
}

kern_return_t workwall_stop(kmod_info_t *ki, void *d)
{
    printf("workwall waves goodbye :(\n");
    return KERN_SUCCESS;
}

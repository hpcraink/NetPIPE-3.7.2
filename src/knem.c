/*****************************************************************************/
/* "NetPIPE" -- Network Protocol Independent Performance Evaluator.          */
/* Copyright 1997, 1998 Iowa State University Research Foundation, Inc.      */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation.  You should have received a copy of the     */
/* GNU General Public License along with this program; if not, write to the  */
/* Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   */
/*                                                                           */
/*     * knem.c           ---- knem Linux kernel module memory copy          */
/*                             http://runtime.bordeaux.inria.fr/knem/        */
/*****************************************************************************/
#include    "netpipe.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>

typedef struct shared_data_t {
    uint64_t cookies[2];
    double time;
    int repeat;
    uint8_t sync_counter;
} shared_data_t;

static int knem_fd = -1;

static int shared_fd = -1;
static volatile char *shared_map = NULL;
static size_t shared_len = 0;
static volatile shared_data_t *my_shared = NULL;
static int my_cookie_index = 0;
static volatile shared_data_t *peer_shared = NULL;
static int peer_cookie_index = 0;
static uint8_t last_sync_counter_value = 0;

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
    /* Default to software copy mode; may be reset by command line
       args */
    p->prot.flags = 0;
}

void Setup(ArgStruct *p)
{
    pid_t pid;
    struct knem_cmd_info info;
    char *filename = (char*) "NPknem.mmap";
    size_t pagesize = sysconf(_SC_PAGESIZE);
    int need_fork = 0, am_parent = 0, flags = O_RDWR;

    /* If no filename was specified, then we're the parent until we
       fork */
    if (NULL == p->host) {
        am_parent = 1;
        need_fork = 1;
        unlink(filename);
    } 
    /* If the file was specified, see if the file exists.  If it does,
       then we're the parent.  Otherwise, we're the "child". */
    else {
        int ret;
        struct stat sbuf;

        filename = p->host;
        need_fork = 0;

        ret = stat(filename, &sbuf);
        if (ret < 0 && errno == ENOENT) {
            am_parent = 1;
        }
    }

    /* Open a file so that we can mmap it */
    if (am_parent) {
        flags |= O_CREAT;
        printf("NPknem: creating shared file: %s\n", filename);
    } else {
        printf("NPknem: attaching to shared file: %s\n", filename);
    }
    shared_fd = open(filename, flags, 0600);
    if (shared_fd < 0) {
        fprintf(stderr, "Netpipe failed to create a file to mmap.  Aborting in despair.\n");
        exit(1);
    }
    shared_len = pagesize * 2;
    if (0 != ftruncate(shared_fd, shared_len)) {
        fprintf(stderr, "Netpipe failed to set the size of a shared file.  Aborting in despair.\n");
        exit(1);
    }

    /* Now fork the child and reset transmitter / receiver flags as
       appropriate */
    if (need_fork) {
        pid = fork();
        if (0 == pid) {
            am_parent = 0;
        } else {
            am_parent = 1;
        }
    }
    if (am_parent) {
        p->rcv = 0;
        p->tr = 1;
    } else {
        p->rcv = 1;
        p->tr = 0;
    }

    /* Map an anonymous shared memory segment between the two
       processes for passing cookies, etc. */
    shared_map = mmap(NULL, shared_len, (PROT_READ | PROT_WRITE), MAP_SHARED, 
                  shared_fd, 0);
    if (MAP_FAILED == shared_map) {
        if (am_parent || !need_fork) {
            perror("mmap");
            fprintf(stderr, "Netpipe failed to mmap a shared file.  Aborting in despair.\n");
        }
        exit(1);
    }

    /* Once we have both mmaped the file, parent unlinks the file so
       that we don't deal with filesystem synchronization.  Parent's
       data is the first page; child's data is the second.  This
       way we can pin them down to our local memory via "first touch"
       method. */
    if (am_parent) {
        my_shared = (shared_data_t*) shared_map;
        peer_shared = (shared_data_t*) (shared_map + pagesize);
    } else {
        peer_shared = (shared_data_t*) shared_map;
        my_shared = (shared_data_t*) (shared_map + pagesize);
    }
    memset((void*) my_shared, 0, sizeof(*my_shared));
    Sync(p);
    if (am_parent){ 
        unlink(filename);
    }

    /* See if we can find /dev/knem */
    knem_fd = open(KNEM_DEVICE_FILENAME, O_RDWR);
    if (knem_fd < 0) {
        if (am_parent || !need_fork) {
            fprintf(stderr, "Netpipe cannot open %s on this host.  Aborting in despair.\n", KNEM_DEVICE_FILENAME);
        }
        exit(1);
    }

    /* Check that we're compiled against the same ABI version that
       we're running with */
    if (ioctl(knem_fd, KNEM_CMD_GET_INFO, &info) < 0) {
        if (am_parent || !need_fork) {
            fprintf(stderr, "Netpipe failed to get ABI version from %s.  Aborting in despair.\n", KNEM_DEVICE_FILENAME);
        }
        exit(1);
    }

#if KNEM_ABI_VERSION < 0x0000000c
#error KNEM is too old, pleasse upgrade to 0.7 or later
#endif

    if (KNEM_ABI_VERSION != info.abi) {
        if (am_parent || !need_fork) {
            fprintf(stderr, "The knem ABI that Netpipe was compiled against is different than that of %s.  Aborting in despair.\n", KNEM_DEVICE_FILENAME);
        }
        exit(1);
    }

    /* If DMA was asked for, ensure that it's available */
    if (0 != p->prot.flags && 0 == (info.features & KNEM_FEATURE_DMA)) {
        if (am_parent || !need_fork) {
            fprintf(stderr, "DMA copy mode was requested but is not supported in the running kernel.  Aborting in despair.\n");
        }
        exit(1);
    }
}   

void Sync(ArgStruct *p)
{
    uint8_t save;

    ++my_shared->sync_counter;
    do {
        save = peer_shared->sync_counter;
        if (last_sync_counter_value != save) {
            ++last_sync_counter_value;
            break;
        }
    } while (1);
}

void PrepareToReceive(ArgStruct *p)
{
}

void SendData(ArgStruct *p)
{
    struct knem_cmd_param_iovec knem_iov = {
        .base = (uintptr_t) p->s_ptr,
        .len = p->bufflen
    };
    struct knem_cmd_create_region createregioncmd = {
        .iovec_array = (uintptr_t) &knem_iov,
        .iovec_nr = 1,
        .flags = KNEM_FLAG_SINGLEUSE,
        .protection = PROT_READ,
    };

    /* Create the region */
    if (0 != ioctl(knem_fd, KNEM_CMD_CREATE_REGION, &createregioncmd)) {
        fprintf(stderr, "Netpipe failed to KNEM_CMD_CREATE_REGION.  Aborting in despair.\n");
        exit(1);
    }

    /* Send the cookie to the peer */
    peer_shared->cookies[peer_cookie_index] = createregioncmd.cookie;
    peer_cookie_index = (0 == peer_cookie_index) ? 1 : 0;
}

void RecvData(ArgStruct *p)
{
    uint64_t cookie;

    struct knem_cmd_param_iovec knem_iov = {
        .base = (uintptr_t) p->r_ptr,
        .len = p->bufflen
    };
    struct knem_cmd_inline_copy icopycmd = {
        .local_iovec_array = (uintptr_t) &knem_iov,
        .local_iovec_nr = 1,
        .write = 0,
        .async_status_index = 0,
        .flags = p->prot.flags
    };

    /* Wait for the sender to set my cookie */
    while (0 == (cookie = my_shared->cookies[my_cookie_index])) {
        continue;
    }
    icopycmd.remote_cookie = cookie;
    icopycmd.remote_offset = 0;
    my_shared->cookies[my_cookie_index] = 0;
    my_cookie_index = (0 == my_cookie_index) ? 1 : 0;

    /* Initiate the receive (synchronous mode) */
    if (0 != ioctl(knem_fd, KNEM_CMD_INLINE_COPY, &icopycmd)) {
        fprintf(stderr, "Netpipe failed to KNEM_CMD_INLINE_COPY.  Aborting in despair.\n");
        exit(1);
    }

    if (icopycmd.current_status != KNEM_STATUS_SUCCESS) {
        fprintf(stderr, "Netpipe failed to complete KNEM_CMD_INLINE_COPY.  Aborting in despair.\n");
        exit(1);
    }
}

void SendTime(ArgStruct *p, double *t)
{
    /* Only child calls SendTime */
    peer_shared->time = *t;
    Sync(p);
}

void RecvTime(ArgStruct *p, double *t)
{
    /* Only parent calls RecvTime */
    Sync(p);
    *t = my_shared->time;
}

void SendRepeat(ArgStruct *p, int rpt)
{
    /* Only parent calls SendRepeat */
    peer_shared->repeat = rpt;
    Sync(p);
}

void RecvRepeat(ArgStruct *p, int *rpt)
{
    /* Only child calls RecvRepeat */
    Sync(p);
    *rpt = my_shared->repeat;
}

void CleanUp(ArgStruct *p)
{
    if (NULL != shared_map && 0 != shared_len) {
        munmap((void*) shared_map, shared_len);
    }
    if (-1 != shared_fd) {
        close(shared_fd);
    }
    close(knem_fd);
}

void Reset(ArgStruct *p)
{
}

void AfterAlignmentInit(ArgStruct *p)
{
}

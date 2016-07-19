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
/*     * vmsplice.c           ---- vmsplice                                  */
/*****************************************************************************/
#include    "netpipe.h"

#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

static int pipe_to_child[2];
static int pipe_to_parent[2];
static int vmsplice_to_child[2];
static int vmsplice_to_parent[2];
static char *fcontrol_to_parent = NULL, *fcontrol_to_child = NULL;
static char *fdata_to_parent = NULL, *fdata_to_child = NULL;

/* Don't know if this is hard-coded in the kernel or not */
const int MAX_VMSPLICE_SIZE = 65536;


static void despair(const char *msg)
{
    fprintf(stderr, "%s  Aborting in despair.\n", msg);

    CleanUp(NULL);

    exit(1);
}   

static void sig_handler(int sig)
{
    CleanUp(NULL);
}   

static void pipe_send(int *fd, void *buf, size_t len)
{
    int ret;
    char *start = (char*) buf;

    while (1) {
        ret = write(fd[1], start, len);
        if (0 == ret) {
            despair("Netpipe wrote 0 bytes down a pipe.");
        } else if (ret > 0) {
            len -= ret;
            start += ret;
            if (0 == len) {
                return;
            }
        } else {
            despair("Netpipe got an error writing down a pipe.");
        }
    }
}

static void pipe_recv(int *fd, void *buf, size_t len)
{
    int ret;
    char *start = (char*) buf;

    while (1) {
        ret = read(fd[0], start, len);
        if (0 == ret) {
            despair("Netpipe read 0 bytes down a pipe.");
            exit(1);
        } else if (ret > 0) {
            len -= ret;
            start += ret;
            if (0 == len) {
                return;
            }
        } else {
            despair("Netpipe got an error reading from a pipe; that shouldn't happen.");
        }
    }
}

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
    /* Install a signal handler to remove all stale fifos if someone
       hits ctrl-c */
    signal(SIGHUP, sig_handler);
    signal(SIGINT, sig_handler);
}

void Setup(ArgStruct *p)
{
    pid_t pid;
    const char *base = "NPknem.vmsplice";
    const char *child_suffix = "to-child";
    const char *parent_suffix = "to-parent";
    int need_fork = 0, am_parent = 0;

    /* If no filename was specified, then we're the parent until we
       fork */
    if (NULL == p->host) {
        am_parent = 1;
        need_fork = 1;
    } 
    /* If the file was specified, see if all 4 files exist.  If it does,
       then we're the parent.  Otherwise, we're the "child". */
    else {
        int ret;
        struct stat sbuf;

        base = p->host;
        need_fork = 0;

        asprintf(&fcontrol_to_parent, "%s.control-%s", base, parent_suffix);
        asprintf(&fcontrol_to_child, "%s.control-%s", base, child_suffix);
        asprintf(&fdata_to_parent, "%s.data-%s", base, parent_suffix);
        asprintf(&fdata_to_child, "%s.data-%s", base, child_suffix);

        if (0 == stat(fcontrol_to_parent, &sbuf) &&
            0 == stat(fcontrol_to_child, &sbuf) &&
            0 == stat(fdata_to_parent, &sbuf) &&
            0 == stat(fdata_to_child, &sbuf)) {
            am_parent = 0;
        } else {
            unlink(fcontrol_to_parent);
            unlink(fcontrol_to_child);
            unlink(fdata_to_parent);
            unlink(fdata_to_child);
            am_parent = 1;
        }
    }

    /* If we can fork, then it's easy to just create pipes between
       parent and child */
    if (need_fork) {
        if (pipe(pipe_to_child) < 0 ||
            pipe(pipe_to_parent) < 0 ||
            pipe(vmsplice_to_child) < 0 ||
            pipe(vmsplice_to_parent) < 0) {
            despair("Netpipe failed to create a pipe.");
        }

        /* Now fork the child and reset transmitter / receiver flags as
           appropriate */
        if (0 == fork()) {
            /* CHILD */
            p->rcv = 1;
            p->tr = 0;
            
            /* Close the relevant ends of pipes */
            close(pipe_to_child[1]);
            close(pipe_to_parent[0]);
            close(vmsplice_to_child[1]);
            close(vmsplice_to_parent[0]);
        } else {
            /* PARENT */
            p->rcv = 0;
            p->tr = 1;
            
            /* Close the relevant ends of pipes */
            close(pipe_to_child[0]);
            close(pipe_to_parent[1]);
            close(vmsplice_to_child[0]);
            close(vmsplice_to_parent[1]);
        }
    }

    /* Otherwise, we have unrelated processes.  So open up several
       named pipes. */
    else {
        if (am_parent) {
            /* Parent */
            p->rcv = 0;
            p->tr = 1;

            if (mkfifo(fcontrol_to_parent, 0600) < 0 ||
                mkfifo(fcontrol_to_child, 0600) < 0 ||
                mkfifo(fdata_to_parent, 0600) < 0 ||
                mkfifo(fdata_to_child, 0600) < 0) {
                /* This is where people typically hit ctrl-c, so don't
                   print an error in this case */
                if (EINTR == errno) {
                    exit(0);
                }
                despair("Netpipe was unable to create a named pipe.");
            }

            if ((pipe_to_parent[0] = open(fcontrol_to_parent, O_RDONLY)) < 0 ||
                (pipe_to_child[1] = open(fcontrol_to_child, O_WRONLY)) < 0 ||
                (vmsplice_to_parent[0] = open(fdata_to_parent, O_RDONLY)) < 0 ||
                (vmsplice_to_child[1] = open(fdata_to_child, O_WRONLY)) < 0) {
                /* This is where people typically hit ctrl-c, so don't
                   print an error in this case */
                if (ENOENT == errno || EINTR == errno) {
                    exit(0);
                }
                despair("Netpipe was unable to open parent ends of the named pipes.");
            }
        } else {
            /* Child */
            p->rcv = 1;
            p->tr = 0;

            if ((pipe_to_parent[1] = open(fcontrol_to_parent, O_WRONLY)) < 0 ||
                (pipe_to_child[0] = open(fcontrol_to_child, O_RDONLY)) < 0 ||
                (vmsplice_to_parent[1] = open(fdata_to_parent, O_WRONLY)) < 0 ||
                (vmsplice_to_child[0] = open(fdata_to_child, O_RDONLY)) < 0) {
                despair("Netpipe was unable to open child ends of the named pipes.");
            }
        }
    }
    printf("All pipes opened\n");

    return;
}

void Sync(ArgStruct *p)
{
    int bogus;

    /* Parent */
    if (p->tr) {
        pipe_send(pipe_to_child, &bogus, sizeof(bogus));
        pipe_recv(pipe_to_parent, &bogus, sizeof(bogus));
    } 

    /* Child */
    else {
        pipe_recv(pipe_to_child, &bogus, sizeof(bogus));
        pipe_send(pipe_to_parent, &bogus, sizeof(bogus));
    }
}

void PrepareToReceive(ArgStruct *p)
{
}

void SendData(ArgStruct *p)
{
    long ret;
    int fd = p->tr ? vmsplice_to_child[1] : vmsplice_to_parent[1];
    int left = p->bufflen;
    struct iovec v = {
        .iov_base = p->s_ptr,
        .iov_len = p->bufflen
    };

    /* Can only vmsplice 64k at a time -- don't know if this is a
       hard-coded limit or not */
    while (left > 0) {
        v.iov_len = left > MAX_VMSPLICE_SIZE ? MAX_VMSPLICE_SIZE : left;
        if (vmsplice(fd, &v, 1, 0) != v.iov_len) {
            despair("Netpipe failed to vmsplice.");
        }
        left -= v.iov_len;
        v.iov_base += v.iov_len;
    }
}

void RecvData(ArgStruct *p)
{
    int fd = p->tr ? vmsplice_to_parent[0] : vmsplice_to_child[0];
    int len, left = p->bufflen;
    char *dest = p->r_ptr;

    /* Can only vmsplice 64k at a time */
    while (left > 0) {
        len = left > MAX_VMSPLICE_SIZE ? MAX_VMSPLICE_SIZE : left;
        if (read(fd, dest, len) != len) {
            despair("Netpipe failed to read bytes from the vmsplice.");
        }
        left -= len;
        dest += len;
    }
}

void SendTime(ArgStruct *p, double *t)
{
    /* Only child calls SendTime */
    pipe_send(pipe_to_parent, t, sizeof(*t));
}

void RecvTime(ArgStruct *p, double *t)
{
    /* Only parent calls RecvTime */
    pipe_recv(pipe_to_parent, t, sizeof(*t));
}

void SendRepeat(ArgStruct *p, int rpt)
{
    /* Only parent calls SendRepeat */
    pipe_send(pipe_to_child, &rpt, sizeof(rpt));
}

void RecvRepeat(ArgStruct *p, int *rpt)
{
    /* Only child calls RecvRepeat */
    pipe_recv(pipe_to_child, rpt, sizeof(*rpt));
}

void CleanUp(ArgStruct *p)
{
    close(pipe_to_parent[0]);
    close(pipe_to_parent[1]);
    close(pipe_to_child[0]);
    close(pipe_to_child[1]);

    close(vmsplice_to_parent[0]);
    close(vmsplice_to_parent[1]);
    close(vmsplice_to_child[0]);
    close(vmsplice_to_child[1]);

    /* Don't leave stale fifos around */
    if (NULL != fcontrol_to_parent) {
        unlink(fcontrol_to_parent);
    }
    if (NULL != fcontrol_to_child) {
        unlink(fcontrol_to_child);
    }
    if (NULL != fdata_to_parent) {
        unlink(fdata_to_parent);
    }
    if (NULL != fdata_to_child) {
        unlink(fdata_to_child);
    }
}



void Reset(ArgStruct *p)
{
}


void AfterAlignmentInit(ArgStruct *p)
{
}

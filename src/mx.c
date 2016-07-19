#include "netpipe.h"

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
  p->tr = 0;
  p->rcv = 1;
}

void Setup(ArgStruct *p)
{
  mx_return_t mx_ret;

  mx_ret = mx_init();
  if (mx_ret != MX_SUCCESS) {
    printf(" Couldn't initialize MX\n");
    exit(-1);
  }

  mx_ret = mx_open_endpoint(0, 0, 0x14072010, NULL, 0, &p->prot.ep);
  if (mx_ret != MX_SUCCESS) {
    printf(" Couldn't open board 0 endpoint 0\n");
    exit(-1);
  }

  printf("Opened board 0 endpoint 0\n");

  establish(p);
}

void establish(ArgStruct *p)
{
  mx_return_t mx_ret;

  if(p->tr){
    char mx_hostname[MX_MAX_HOSTNAME_LEN];
    uint64_t mx_nicid;
    mx_request_t mx_req;
    mx_status_t mx_status;
    uint32_t mx_result;

    snprintf(mx_hostname, sizeof(mx_hostname), "%s:0", p->host);
    mx_ret = mx_hostname_to_nic_id(mx_hostname, &mx_nicid);
    if (mx_ret != MX_SUCCESS) {
      printf("Couldn't find peer %s\n", mx_hostname);
      exit(-1);
    }

    mx_ret = mx_connect(p->prot.ep, mx_nicid, 0, 0x14072010, MX_INFINITE, &p->prot.addr);
    if (mx_ret != MX_SUCCESS) {
      printf("Couldn't connect to peer %s endpoint 0\n", mx_hostname);
      exit(-1);
    }

    mx_ret = mx_isend(p->prot.ep, NULL, 0, p->prot.addr, 0, NULL, &mx_req);
    if (mx_ret != MX_SUCCESS) {
      printf("Couldn't send connect to peer %s endpoint 0\n", mx_hostname);
      exit(-1);
    }

    mx_ret = mx_wait(p->prot.ep, &mx_req, MX_INFINITE, &mx_status, &mx_result);
    if (mx_ret != MX_SUCCESS || !mx_result || mx_status.code != MX_STATUS_SUCCESS) {
      printf("Send connect to peer %s endpoint 0 failed\n", mx_hostname);
      exit(-1);
    }

    mx_ret = mx_irecv(p->prot.ep, NULL, 0, 0, 0, NULL, &mx_req);
    if (mx_ret != MX_SUCCESS) {
      printf("Couldn't receive connect back\n");
      exit(-1);
    }

    mx_ret = mx_wait(p->prot.ep, &mx_req, MX_INFINITE, &mx_status, &mx_result);
    if (mx_ret != MX_SUCCESS || !mx_result || mx_status.code != MX_STATUS_SUCCESS) {
      printf("Receiver connect back failed\n");
      exit(-1);
    }

  }
  else
  {
    uint64_t mx_nicid;
    uint32_t mx_eid;
    mx_request_t mx_req;
    mx_status_t mx_status;
    uint32_t mx_result;

    mx_ret = mx_irecv(p->prot.ep, NULL, 0, 0, 0, NULL, &mx_req);
    if (mx_ret != MX_SUCCESS) {
      printf("Couldn't receive connect\n");
      exit(-1);
    }

    mx_ret = mx_wait(p->prot.ep, &mx_req, MX_INFINITE, &mx_status, &mx_result);
    if (mx_ret != MX_SUCCESS || !mx_result || mx_status.code != MX_STATUS_SUCCESS) {
      printf("Receive connect failed\n");
      exit(-1);
    }

    mx_ret = mx_decompose_endpoint_addr(mx_status.source, &mx_nicid, &mx_eid);
    if (mx_ret != MX_SUCCESS) {
      printf("Couldn't decompose sender address\n");
      exit(-1);
    }

    mx_ret = mx_connect(p->prot.ep, mx_nicid, mx_eid, 0x14072010, MX_INFINITE, &p->prot.addr);
    if (mx_ret != MX_SUCCESS) {
      printf("Couldn't connect to nic %llx endpoint %u\n", (unsigned long long) mx_nicid, (unsigned) mx_eid);
      exit(-1);
    }

    mx_ret = mx_isend(p->prot.ep, NULL, 0, p->prot.addr, 0, NULL, &mx_req);
    if (mx_ret != MX_SUCCESS) {
      printf("Couldn't send connect back\n");
      exit(-1);
    }

    mx_ret = mx_wait(p->prot.ep, &mx_req, MX_INFINITE, &mx_status, &mx_result);
    if (mx_ret != MX_SUCCESS || !mx_result || mx_status.code != MX_STATUS_SUCCESS) {
      printf("Send connect back failed\n");
      exit(-1);
    }

  }
}

void Sync(ArgStruct *p)
{
  char s[] = "SyncMe", response[] = "      ";
  mx_segment_t mx_seg;
  mx_request_t mx_req;
  mx_status_t mx_status;
  uint32_t mx_result;
  mx_return_t mx_ret;

  mx_seg.segment_ptr = s;
  mx_seg.segment_length = strlen(s);
  mx_ret = mx_isend(p->prot.ep, &mx_seg, 1, p->prot.addr, 0, NULL, &mx_req);
  if (mx_ret != MX_SUCCESS) {
    printf("Couldn't send sync\n");
    exit(-1);
  }

  mx_ret = mx_wait(p->prot.ep, &mx_req, MX_INFINITE, &mx_status, &mx_result);
  if (mx_ret != MX_SUCCESS || !mx_result || mx_status.code != MX_STATUS_SUCCESS) {
    printf("Send sync failed\n");
    exit(-1);
  }

  mx_seg.segment_ptr = response;
  mx_seg.segment_length = strlen(response);
  mx_ret = mx_irecv(p->prot.ep, &mx_seg, 1, 0, 0, NULL, &mx_req);
  if (mx_ret != MX_SUCCESS) {
    printf("Couldn't receive sync\n");
    exit(-1);
  }

  mx_ret = mx_wait(p->prot.ep, &mx_req, MX_INFINITE, &mx_status, &mx_result);
  if (mx_ret != MX_SUCCESS || !mx_result || mx_status.code != MX_STATUS_SUCCESS) {
    printf("Receive sync failed\n");
    exit(-1);
  }
}

void PrepareToReceive(ArgStruct *p)
{
  /* TODO use this */
}

void SendData(ArgStruct *p)
{
  mx_segment_t mx_seg;
  mx_request_t mx_req;
  mx_status_t mx_status;
  uint32_t mx_result;
  mx_return_t mx_ret;

  mx_seg.segment_ptr = p->s_ptr;
  mx_seg.segment_length = p->bufflen;
  mx_ret = mx_isend(p->prot.ep, &mx_seg, 1, p->prot.addr, 0, NULL, &mx_req);
  if (mx_ret != MX_SUCCESS) {
    printf("Couldn't send data\n");
    exit(-1);
  }

  do {
    mx_ret = mx_test(p->prot.ep, &mx_req, &mx_status, &mx_result);
    if (mx_ret != MX_SUCCESS) {
      printf("Send data failed\n");
      exit(-1);
    }
  } while (!mx_result);
  if (mx_status.code != MX_STATUS_SUCCESS) {
    printf("Send data failed\n");
    exit(-1);
  }
}

void RecvData(ArgStruct *p)
{
  mx_status_t mx_status;
  uint32_t mx_result;
  mx_segment_t mx_seg;
  mx_return_t mx_ret;

  mx_seg.segment_ptr = p->r_ptr;
  mx_seg.segment_length = p->bufflen;
  mx_ret = mx_irecv(p->prot.ep, &mx_seg, 1, 0, 0, NULL, &p->prot.rreq);
  if (mx_ret != MX_SUCCESS) {
    printf("Couldn't prepare receive\n");
    exit(-1);
  }

  do {
    mx_ret = mx_test(p->prot.ep, &p->prot.rreq, &mx_status, &mx_result);
    if (mx_ret != MX_SUCCESS) {
      printf("Recv data failed\n");
      exit(-1);
    }
  } while (!mx_result);
  if (mx_status.code != MX_STATUS_SUCCESS) {
    printf("Recv data failed\n");
    exit(-1);
  }
}

/* ********** NetPipe stuff ********* */
void SendTime(ArgStruct *p, double *t)
{
  mx_segment_t mx_seg;
  mx_request_t mx_req;
  mx_status_t mx_status;
  uint32_t mx_result;
  mx_return_t mx_ret;
  uint32_t ltime, ntime;

  /*
    Multiply the number of seconds by 1e6 to get time in microseconds
    and convert value to an unsigned 32-bit integer.
    */
  ltime = (uint32_t)(*t * 1.e6);

  /* Send time in network order */
  ntime = htonl(ltime);

  mx_seg.segment_ptr = &ntime;
  mx_seg.segment_length = sizeof(ntime);
  mx_ret = mx_isend(p->prot.ep, &mx_seg, 1, p->prot.addr, 0, NULL, &mx_req);
  if (mx_ret != MX_SUCCESS) {
    printf("Couldn't send time\n");
    exit(301);
  }

  mx_ret = mx_wait(p->prot.ep, &mx_req, MX_INFINITE, &mx_status, &mx_result);
  if (mx_ret != MX_SUCCESS || !mx_result || mx_status.code != MX_STATUS_SUCCESS) {
    printf("Send time failed\n");
    exit(301);
  }
}

void RecvTime(ArgStruct *p, double *t)
{
  uint32_t ltime, ntime;
  mx_segment_t mx_seg;
  mx_request_t mx_req;
  mx_status_t mx_status;
  uint32_t mx_result;
  mx_return_t mx_ret;

  mx_seg.segment_ptr = &ntime;
  mx_seg.segment_length = sizeof(ntime);
  mx_ret = mx_irecv(p->prot.ep, &mx_seg, 1, 0, 0, NULL, &mx_req);
  if (mx_ret != MX_SUCCESS) {
    printf("Couldn't prepare receive time\n");
    exit(302);
  }

  mx_ret = mx_wait(p->prot.ep, &mx_req, MX_INFINITE, &mx_status, &mx_result);
  if (mx_ret != MX_SUCCESS || !mx_result || mx_status.code != MX_STATUS_SUCCESS) {
    printf("Receive time failed\n");
    exit(302);
  }

  ltime = ntohl(ntime);

  /* Result is ltime (in microseconds) divided by 1.0e6 to get seconds */
  *t = (double)ltime / 1.0e6;
}

/* in the event of a send failure, re-send (tcp)*/
void SendRepeat(ArgStruct *p, int rpt)
{
  mx_segment_t mx_seg;
  mx_request_t mx_req;
  mx_status_t mx_status;
  uint32_t mx_result;
  mx_return_t mx_ret;
  uint32_t lrpt, nrpt;

  lrpt = rpt;
  /* Send repeat count as a long in network order */
  nrpt = htonl(lrpt);

  mx_seg.segment_ptr = &nrpt;
  mx_seg.segment_length = sizeof(nrpt);
  mx_ret = mx_isend(p->prot.ep, &mx_seg, 1, p->prot.addr, 0, NULL, &mx_req);
  if (mx_ret != MX_SUCCESS) {
    printf("Couldn't send repeat\n");
    exit(304);
  }

  mx_ret = mx_wait(p->prot.ep, &mx_req, MX_INFINITE, &mx_status, &mx_result);
  if (mx_ret != MX_SUCCESS || !mx_result || mx_status.code != MX_STATUS_SUCCESS) {
    printf("Send repeat failed\n");
    exit(304);
  }
}

/* in the event of a recv failure, resend (tcp)*/
void RecvRepeat(ArgStruct *p, int *rpt)
{
  uint32_t lrpt, nrpt;
  mx_segment_t mx_seg;
  mx_request_t mx_req;
  mx_status_t mx_status;
  uint32_t mx_result;
  mx_return_t mx_ret;

  mx_seg.segment_ptr = &nrpt;
  mx_seg.segment_length = sizeof(nrpt);
  mx_ret = mx_irecv(p->prot.ep, &mx_seg, 1, 0, 0, NULL, &mx_req);
  if (mx_ret != MX_SUCCESS) {
    printf("Couldn't prepare receive time\n");
    exit(302);
  }

  mx_ret = mx_wait(p->prot.ep, &mx_req, MX_INFINITE, &mx_status, &mx_result);
  if (mx_ret != MX_SUCCESS || !mx_result || mx_status.code != MX_STATUS_SUCCESS) {
    printf("Receive time failed\n");
    exit(302);
  }

  lrpt = ntohl(nrpt);

  *rpt = lrpt;
}

void CleanUp(ArgStruct *p)
{
   sleep(1);
   mx_close_endpoint(p->prot.ep);
   mx_finalize();
}


void Reset(ArgStruct *p)
{

}

void AfterAlignmentInit(ArgStruct *p)
{

}

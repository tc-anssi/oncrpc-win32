#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)clnt_rdp.c 1.37 87/10/05 Copyr 1984 Sun Micro";
#endif

/* This is experimental code to leverage RDP DVC as a RPC
 * communication channel. It is heavily inspired from Clnt_tcp.c.
 * This should not be used in production environment.
*/

#include "all_oncrpc.h"

#include <WtsApi32.h>
#include <pchannel.h>

#define CHANNEL_NAME "DVCSOCKET"

#pragma comment(lib, "wtsapi32.lib")

#define MCALL_MSG_SIZE 24

#ifndef WIN32
extern int errno;
#endif

static int	readrdp();
static int	writerdp();

static enum clnt_stat	clntrdp_call();
static void		clntrdp_abort();
static void		clntrdp_geterr();
static bool_t	clntrdp_freeres();
static bool_t   clntrdp_control();
static void		clntrdp_destroy();

static struct clnt_ops rdp_ops = {
	clntrdp_call,
	clntrdp_abort,
	clntrdp_geterr,
	clntrdp_freeres,
	clntrdp_destroy,
	clntrdp_control
};

struct ct_data {
	HANDLE		ct_sock;
	bool_t		ct_closeit;
	struct timeval	ct_wait;
	bool_t          ct_waitset;       /* wait set by clnt_control? */
	struct sockaddr_in ct_addr;
	struct rpc_err	ct_error;
	char		ct_mcall[MCALL_MSG_SIZE];	/* marshalled callmsg */
	u_int		ct_mpos;			/* pos after marshal */
	XDR		ct_xdrs;
};

/*
*  Open a dynamic channel with the name given in szChannelName.
*  The output file handle can be used in ReadFile/WriteFile calls.
*/
DWORD OpenChannel(
    LPCSTR szChannelName, 
    HANDLE *phFile )
{
    HANDLE hWTSHandle = NULL;
    HANDLE hWTSFileHandle;
    PVOID vcFileHandlePtr = NULL;
    DWORD len;
    DWORD rc = ERROR_SUCCESS;
	BOOL fSucc;

    hWTSHandle = WTSVirtualChannelOpenEx(
        WTS_CURRENT_SESSION,
        (LPSTR)szChannelName,
		WTS_CHANNEL_OPTION_DYNAMIC );
    if ( NULL == hWTSHandle )
    {
		nt_rpc_report("Error from WTSVirtualChannelOpenEx\n");
		printf("Error from WTSVirtualChannelOpenEx\n");
        rc = GetLastError();
        goto exitpt;
    }

    fSucc = WTSVirtualChannelQuery(
        hWTSHandle,
        WTSVirtualFileHandle,
        &vcFileHandlePtr,
        &len );
    if ( !fSucc )
    {
		nt_rpc_report("Error from WTSVirtualChannelQuery\n");
		printf("Error from WTSVirtualChannelQuery\n");
        rc = GetLastError();
        goto exitpt;
    }
	
    if ( len != sizeof( HANDLE ))
    {
		nt_rpc_report("INVALID PARAMETER\n");
		printf("INVALID PARAMETER\n");
        rc = ERROR_INVALID_PARAMETER;
        goto exitpt;
    }
	
    hWTSFileHandle = *(HANDLE *)vcFileHandlePtr;

    fSucc = DuplicateHandle(
        GetCurrentProcess(),
        hWTSFileHandle,
        GetCurrentProcess(),
        phFile,
        0,
        FALSE,
        DUPLICATE_SAME_ACCESS );
    if ( !fSucc )
    {
		nt_rpc_report("Error from DuplicateHandle\n");
		printf("Error from DuplicateHandle\n");
        rc = GetLastError();
        goto exitpt;
    }

    rc = ERROR_SUCCESS;

exitpt:
    if ( vcFileHandlePtr )
    {
        WTSFreeMemory( vcFileHandlePtr );
    }
    if ( hWTSHandle )
    {
        WTSVirtualChannelClose( hWTSHandle );
    }
    return rc;
}
/*
 * Create a client handle from an existing RDP connection.
 * The rpc/rdp package does buffering
 * similar to stdio, so the client must pick send and receive buffer sizes,];
 * 0 => use the default.
 * raddr is not used.
 * NB: *sockp is copied into a private area.
 * NB: It is the clients responsibility to close *sockp.
 * NB: The rpch->cl_auth is set null authentication.  Caller may wish to set this
 * something more useful.
 */
CLIENT *clntrdp_create(struct sockaddr_in *raddr,	u_long prog, u_long vers,	HANDLE *sockp,	u_int sendsz,	u_int recvsz)
{
	CLIENT *h;
	struct ct_data *ct;
	struct timeval now;
	struct rpc_msg call_msg;
	HANDLE hHandle;
	struct sockaddr_in tmp;

	memset(&tmp, 0, sizeof(tmp));
	h  = (CLIENT *)mem_alloc(sizeof(*h));
	if (h == NULL) {
#ifdef WIN32
		nt_rpc_report("clntrdp_create: out of memory\n");
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = ENOMEM;
#endif
		goto fooy;
	}
	ct = (struct ct_data *)mem_alloc(sizeof(*ct));
	if (ct == NULL) {
#ifdef WIN32
		nt_rpc_report("clntrdp_create: out of memory\n");
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = ENOMEM;
#endif
		goto fooy;
	}

	/*
	 * Always create the Channel
	 */
#ifdef WIN32
	if(OpenChannel(CHANNEL_NAME, &hHandle) != ERROR_SUCCESS){
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = WSAerrno;
#endif
			goto fooy;
	}
	*sockp = hHandle;
	ct->ct_closeit = TRUE;
	/*
	 * Set up private data struct
	 */
	ct->ct_sock = hHandle;
	ct->ct_wait.tv_usec = 0;
	ct->ct_waitset = FALSE;
	/* ct_addr is not meaningful in this ctx */
	ct->ct_addr = tmp;

	/*
	 * Initialize call message
	 */
	gettimeofday(&now, (struct timezone *)0);
	call_msg.rm_xid = getpid() ^ now.tv_sec ^ now.tv_usec;
	call_msg.rm_direction = CALL;
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = prog;
	call_msg.rm_call.cb_vers = vers;

	/*
	 * pre-serialize the staic part of the call msg and stash it away
	 */
	xdrmem_create(&(ct->ct_xdrs), ct->ct_mcall, MCALL_MSG_SIZE,
	    XDR_ENCODE);
	if (! xdr_callhdr(&(ct->ct_xdrs), &call_msg)) {
		if (ct->ct_closeit) {
#ifdef WIN32
			printf("Failed xdrmem_create\n");
			CloseHandle(*sockp);
#endif
		}
		goto fooy;
	}

	ct->ct_mpos = XDR_GETPOS(&(ct->ct_xdrs));
	XDR_DESTROY(&(ct->ct_xdrs));

	/*
	 * Create a client handle which uses xdrrec for serialization
	 * and authnone for authentication.
	 */
	xdrrec_create(&(ct->ct_xdrs), sendsz, recvsz,
	    (caddr_t)ct, readrdp, writerdp);

	h->cl_ops = &rdp_ops;
	h->cl_private = (caddr_t) ct;
	h->cl_auth = authnone_create();

	return h;

fooy:
	/*
	 * Something goofed, free stuff and barf
	 */
	mem_free((caddr_t)ct, sizeof(struct ct_data));
	mem_free((caddr_t)h, sizeof(CLIENT));
	return (CLIENT *)NULL;
}

static enum clnt_stat clntrdp_call(CLIENT *h, u_long proc,	xdrproc_t xdr_args,	caddr_t args_ptr,	xdrproc_t xdr_results,
	                                 caddr_t results_ptr,	struct timeval timeout)
{

	struct ct_data *ct = (struct ct_data *) h->cl_private;
	XDR *xdrs = &(ct->ct_xdrs);
	struct rpc_msg reply_msg;
	u_long x_id;
	u_long *msg_x_id = (u_long *)(ct->ct_mcall);	/* yuk */
	bool_t shipnow;
	int refreshes = 2;

	if (!ct->ct_waitset) {
		ct->ct_wait = timeout;
	}

	shipnow =
	    (xdr_results == (xdrproc_t)0 && timeout.tv_sec == 0
	    && timeout.tv_usec == 0) ? FALSE : TRUE;

call_again:
	xdrs->x_op = XDR_ENCODE;
	ct->ct_error.re_status = RPC_SUCCESS;
	x_id = ntohl(--(*msg_x_id));
	if ((! XDR_PUTBYTES(xdrs, ct->ct_mcall, ct->ct_mpos)) ||
	    (! XDR_PUTLONG(xdrs, (long *)&proc)) ||
	    (! AUTH_MARSHALL(h->cl_auth, xdrs)) ||
	    (! (*xdr_args)(xdrs, args_ptr))) {
		if (ct->ct_error.re_status == RPC_SUCCESS)
			ct->ct_error.re_status = RPC_CANTENCODEARGS;
		xdrrec_endofrecord(xdrs, TRUE);
		return ct->ct_error.re_status;
	}
	if (! xdrrec_endofrecord(xdrs, shipnow))
		return ct->ct_error.re_status = RPC_CANTSEND;
	if (! shipnow)
		return RPC_SUCCESS;
	/*
	 * Hack to provide rpc-based message passing
	 */
	if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
		return(ct->ct_error.re_status = RPC_TIMEDOUT);
	}


	/*
	 * Keep receiving until we get a valid transaction id
	 */
	xdrs->x_op = XDR_DECODE;
	while (TRUE) {
		reply_msg.acpted_rply.ar_verf = _null_auth;
		reply_msg.acpted_rply.ar_results.where = NULL;
		reply_msg.acpted_rply.ar_results.proc = xdr_void;
		if (! xdrrec_skiprecord(xdrs))
			return ct->ct_error.re_status;
		/* now decode and validate the response header */
		if (! xdr_replymsg(xdrs, &reply_msg)) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				continue;
			return ct->ct_error.re_status;
		}
		if (reply_msg.rm_xid == x_id)
			break;
	}

	/*
	 * process header
	 */
	_seterr_reply(&reply_msg, &(ct->ct_error));
	if (ct->ct_error.re_status == RPC_SUCCESS) {
		if (! AUTH_VALIDATE(h->cl_auth, &reply_msg.acpted_rply.ar_verf)) {
			ct->ct_error.re_status = RPC_AUTHERROR;
			ct->ct_error.re_why = AUTH_INVALIDRESP;
		} else if (! (*xdr_results)(xdrs, results_ptr)) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				ct->ct_error.re_status = RPC_CANTDECODERES;
		}
		/* free verifier ... */
		if (reply_msg.acpted_rply.ar_verf.oa_base != NULL) {
			xdrs->x_op = XDR_FREE;
			xdr_opaque_auth(xdrs, &(reply_msg.acpted_rply.ar_verf));
		}
	}  /* end successful completion */
	else {
		/* maybe our credentials need to be refreshed ... */
		if (refreshes-- && AUTH_REFRESH(h->cl_auth))
			goto call_again;
	}  /* end of unsuccessful completion */
	return ct->ct_error.re_status;
}

static void clntrdp_geterr(CLIENT *h,	struct rpc_err *errp)
{
	struct ct_data *ct =
	    (struct ct_data *) h->cl_private;

	*errp = ct->ct_error;
}

static bool_t clntrdp_freeres(CLIENT *cl,	xdrproc_t xdr_res, caddr_t res_ptr)
{
	struct ct_data *ct = (struct ct_data *)cl->cl_private;
	XDR *xdrs = &(ct->ct_xdrs);

	xdrs->x_op = XDR_FREE;
	return ((*xdr_res)(xdrs, res_ptr));
}

static void clntrdp_abort()
{
}

static bool_t clntrdp_control(CLIENT *cl,	int request, char *info)
{
	struct ct_data *ct = (struct ct_data *)cl->cl_private;

	switch (request) {
	case CLSET_TIMEOUT:
		ct->ct_wait = *(struct timeval *)info;
		ct->ct_waitset = TRUE;
		break;
	case CLGET_TIMEOUT:
		*(struct timeval *)info = ct->ct_wait;
		break;
	case CLGET_SERVER_ADDR:
		*(struct sockaddr_in *)info = ct->ct_addr;
		break;
	default:
		return FALSE;
	}
	return TRUE;
}


static void clntrdp_destroy(CLIENT *h)
{
	struct ct_data *ct =
	    (struct ct_data *) h->cl_private;

	if (ct->ct_closeit) {
#ifdef WIN32
		if(CloseHandle(ct->ct_sock) == 0){
			nt_rpc_report("clntrdp_destroy: error Closing Handle to VirtualChannel\n");
		}
#endif
	}
	XDR_DESTROY(&(ct->ct_xdrs));
	mem_free((caddr_t)ct, sizeof(struct ct_data));
	mem_free((caddr_t)h, sizeof(CLIENT));
}

/*
 * Strips the header from the received PDU.
 */
static int remove_header(caddr_t in_buffer, DWORD in_len, caddr_t out_buffer){
    CHANNEL_PDU_HEADER *pHdr;

#ifdef DEBUG
	int i = 0;
	printf("in_len %d, in_buffer %p, pHder len %d, new_addr %p\n", in_len, in_buffer, sizeof(*pHdr),  in_buffer+(sizeof(CHANNEL_PDU_HEADER)));
#endif
	pHdr = (CHANNEL_PDU_HEADER*)in_buffer;

	if(in_len != (pHdr->length + sizeof(*pHdr))){
		fprintf(stderr, "remove_header, length mismatch PDU len %d, read %d\n", pHdr->length, in_len);
		return -1;
	}
	if(pHdr->flags & CHANNEL_FLAG_LAST){
#ifdef DEBUG
		fprintf(stderr, "Last chunk\n");
#endif
	}

	memcpy(out_buffer, in_buffer+(sizeof(CHANNEL_PDU_HEADER)), pHdr->length);

#ifdef DEBUG
	for (i=0; i<pHdr->length; i++){
		printf("%02x", (unsigned char)out_buffer[i]);
	}
	printf("\n");
#endif

	return pHdr->length;
}

/*
 * Function used to read some data using async primitives.
 */
static int readrdp(struct ct_data *ct,	caddr_t buf, int len)
{
    HANDLE hFile;
    DWORD dwRead;
    BOOL fSucc;
    HANDLE hEvent;
	BYTE ReadBuffer[4096] = {0};

	struct timeval *timeout;
	long long requested_timeout;

    OVERLAPPED  Overlapped = {0};
	DWORD dw;
	DWORD error;

	if (len == 0)
		return 0;

    hFile = ct->ct_sock;

	timeout = &(ct->ct_wait);
	requested_timeout = timeout->tv_sec * 1000 + (timeout->tv_usec + 500) / 1000;

	hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );

    while(1){
        Overlapped.hEvent = hEvent;
        // Read the entire message.
        fSucc = ReadFile(
            hFile,
			ReadBuffer,
            len,
            &dwRead,
            &Overlapped);
        if ( !fSucc ){
            if ( GetLastError() == ERROR_IO_PENDING ){
                dw = WaitForSingleObject( Overlapped.hEvent, requested_timeout);
				switch (dw){
					case WAIT_TIMEOUT:{
						ct->ct_error.re_status = RPC_TIMEDOUT;
						return -1;
					}
					case WAIT_OBJECT_0:{
						fSucc = GetOverlappedResult(
							hFile,
							&Overlapped,
							&dwRead,
							FALSE );
					        if ( !fSucc ){
								ct->ct_error.re_status = RPC_CANTRECV;
								ct->ct_error.re_errno = WSAerrno;
								return -1;
							}
							dwRead = remove_header((caddr_t)ReadBuffer, dwRead, buf);
							if(dwRead == -1){
								ct->ct_error.re_status = WSAECONNRESET;
								ct->ct_error.re_errno = WSAerrno;
								return -1;
							}
							return dwRead;
					}
					default:
						ct->ct_error.re_status = RPC_CANTRECV;
						ct->ct_error.re_errno = WSAerrno;
						return -1;
				}
			}
			else{//Failed but not with ERROR_IO_PENDING
				error = GetLastError();
				ct->ct_error.re_status = WSAECONNRESET;
				ct->ct_error.re_errno = WSAerrno;
				return -1;
			}
        }
		//IO completed right away
		dwRead = remove_header((caddr_t)ReadBuffer, dwRead, buf);

		return dwRead;
	}
}

/*
 * Function used to write some data using async primitives.
 */
static int writerdp(struct ct_data *ct,	caddr_t buf, int len)
{
    HANDLE  hFile;
    DWORD   dwWritten;
    BOOL    fSucc;
    HANDLE  hEvent;
	DWORD	error;
	OVERLAPPED  Overlapped = {0};

    hFile = ct->ct_sock;

    hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );

	Overlapped.hEvent = hEvent;

     fSucc = WriteFile(
            hFile,
            buf,
            len,
            &dwWritten,
            &Overlapped );
    if ( !fSucc ){
        if ( GetLastError() == ERROR_IO_PENDING ){
			DWORD dw = WaitForSingleObject( Overlapped.hEvent, INFINITE );
			switch (dw){
				case WAIT_OBJECT_0:{
					fSucc = GetOverlappedResult(
						hFile,
						&Overlapped,
						&dwWritten,
						FALSE );
					if ( !fSucc ){
						DWORD error = GetLastError();
						ct->ct_error.re_errno = WSAerrno;
						ct->ct_error.re_status = RPC_CANTSEND;
						return -1;
					}
					break;
				}
				default:
					error = GetLastError();
					ct->ct_error.re_errno = WSAerrno;
					ct->ct_error.re_status = RPC_CANTSEND;
					return -1;
			}
        }
		else{//Failed but not with ERROR_IO_PENDING
			error = GetLastError();
			ct->ct_error.re_errno = WSAerrno;
			ct->ct_error.re_status = RPC_CANTSEND;
			return -1;
		}
    }
	return dwWritten;
}

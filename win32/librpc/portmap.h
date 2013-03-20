struct pmaplist *pmaplist;

static struct pmaplist *
find_service(
  u_long prog,
  u_long vers,
  u_long prot
  );

void
reg_service(
  struct svc_req *rqstp,
  SVCXPRT *xprt
  );

typedef struct encap_parms {
  u_long arglen;
  char *args;
} encap_parms_t;

typedef struct rmtcallargs {
  u_long	rmt_prog;
  u_long	rmt_vers;
  u_long	rmt_port;
  u_long	rmt_proc;
  struct encap_parms rmt_args;
} rmtcallargs_t;

static bool_t
xdr_encap_parms(
  XDR *xdrs,
  struct encap_parms *epp
  );

static bool_t
xdr_rmtcall_args(
  register XDR *xdrs,
  register struct rmtcallargs *cap
  );

static bool_t
xdr_rmtcall_result(
  register XDR *xdrs,
  register struct rmtcallargs *cap
  );

static bool_t
xdr_opaque_parms(
  XDR *xdrs,
  struct rmtcallargs *cap
  );

static bool_t
xdr_len_opaque_parms(
  register XDR *xdrs,
  struct rmtcallargs *cap
  );

static void
callit(
  struct svc_req *rqstp,
  SVCXPRT *xprt
  );
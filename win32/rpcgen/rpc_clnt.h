#ifndef __RPC_CLNT_HEADER__
#define __RPC_CLNT_HEADER__

void write_stubs();

static void write_program(definition *def);

static char *ampr(char *type);

static void printbody(proc_list *proc);

#endif //__RPC__CLNT_HEADER__
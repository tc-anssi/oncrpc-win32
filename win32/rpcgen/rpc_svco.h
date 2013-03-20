#ifndef __RPC_SVCO_HEADER__
#define __RPC_SVCO_HEADER__

void write_most();
void write_register(char *transp);
void write_rest();
void write_programs(char *storage);
static void write_program(definition *def, char *storage);
static void printerr(char *err, char *transp);
static void printif(char *proc, char *transp, char *prefix, char *arg);
int nullproc(proc_list *proc);

#endif //__RPC_SVCO_HEADER__
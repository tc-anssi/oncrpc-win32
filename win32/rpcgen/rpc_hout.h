#ifndef __RPC_HOUT_HEADER__
#define __RPC_HOUT_HEADER__

void print_datadef(definition *def);

static void pconstdef(definition *def);

static void pstructdef(definition *def);

static void puniondef(definition *def);

static void pdefine(char *name, char *num);

static void puldefine(char *name, char *num);

static int define_printed(proc_list *stop, version_list *start);

static void pprogramdef(definition *def);

void pprocdef(proc_list *proc, version_list *vp);

static void penumdef(definition *def);

static void ptypedef(definition *def);

static void pdeclaration(char *name, declaration *dec, int tab);

static int undefined2(char *type, char *stop);

#endif //__RPC_HOUT_HEADER__
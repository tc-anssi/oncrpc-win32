void
emit(
  definition *def
  );

static int
findtype(
  definition *def,
  char *type
  );

static int
undefined(
  char *type
  );

static void
print_header(
  definition *def
  );

static void
print_trailer();

static void
print_ifopen(
  int indent,
  char *name
  );

static void
print_ifarg(
  char *arg
  );

static void
print_ifclose(
  int indent
  );

static void
space();

static void
print_ifstat(
  int indent,
  char *prefix,
  char *type,
  relation rel,
  char *amax,
  char *objname,
  char *name
  );

static void
emit_enum(
  definition *def
  );

static void
emit_union(
  definition *def
  );

static void
emit_struct(
  definition *def
  );

static void
emit_typedef(
  definition *def
  );

static void
print_stat(
  declaration *dec
  );
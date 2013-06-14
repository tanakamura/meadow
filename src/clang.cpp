#define HAVE_VOLATILE
#undef MEADOW

#undef PTR

#include "clang/CodeGen/CodeGenAction.h"

extern "C" {
#include <config.h>
#include <lisp.h>
}

int clang_value;

void
syms_of_clang(void)
{
    DEFVAR_INT("clang",
               &clang_value,
               doc:);
}

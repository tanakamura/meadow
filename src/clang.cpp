#define HAVE_VOLATILE
#undef MEADOW

#undef PTR

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/StoredValueRef.h"
#include "cling/MetaProcessor/MetaProcessor.h"
#include "cling/UserInterface/UserInterface.h"


#include "clang/Basic/LangOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"

#include "llvm/Support/Signals.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/ManagedStatic.h"

#include <windows.h>

extern "C" {
#include <config.h>
#include <lisp.h>
#include <buffer.h>
}

#undef fopen

static cling::Interpreter *interp;

static cling::MetaProcessor *metaproc;
static FILE *debug_out;
static std::string message_buffer;
static llvm::raw_string_ostream *buffer_output;

static void
eval_string(const char *str)
{
    cling::Interpreter::CompilationResult r;

    message_buffer.clear();

    metaproc->process(str, r, NULL);

    if (r != cling::Interpreter::kSuccess) {
        fprintf(debug_out, "error = %d\n", (int)r);
        error("compilation error (fixme obtaion error mesage)");
    }
}

static void
eval(Lisp_Object prog)
{
    CHECK_STRING(prog);

    char *prog_data = (char*)SDATA(prog);
    eval_string(prog_data);
}

DEFUN("cling-eval", Fcling_eval, Scling_eval, 1, 1, "sString: \n",
      doc: /* eval PROGRAM as a toplevel of cling interpreter. */)
#ifndef __cplusplus
    (program)
Lisp_Object  program;
#else
(Lisp_Object program)
#endif
{
    interp->enableRawInput(false);

    eval(program);

    return Qnil;
}

DEFUN("clang-eval", Fclang_eval, Sclang_eval, 1, 1, "sString: \n",
      doc: /* eval PROGRAM as a C++ string. */)
#ifndef __cplusplus
    (program)
Lisp_Object  program;
#else
(Lisp_Object program)
#endif
{
    interp->enableRawInput(true);

    eval(program);

    return Qnil;
}

static void
eval_region(Lisp_Object start,
            Lisp_Object end)
{
    int start_bytepos, start_charpos;
    int end_bytepos, end_charpos;

    validate_region(&start, &end);

    DECODE_POSITION(start_bytepos, start_charpos, start);
    DECODE_POSITION(end_bytepos, end_charpos, end);

    int len = end_bytepos - start_bytepos;

    start_bytepos--;
    end_bytepos--;

    char *buffer = (char*)xmalloc(len + 1);
    const char *buf_data = (const char *)BEGV_ADDR;

    memcpy(buffer, buf_data+start_bytepos, len);
    buffer[len] = '\0';

    fprintf(debug_out, "%d %d %s\n", start_bytepos, end_bytepos, buffer);

    eval_string(buffer);

    xfree(buffer);
}


DEFUN("cling-eval-region", Fcling_eval_region, Scling_eval_region, 2, 2, "r",
      doc: /* eval region as a C++ string. */)
#ifndef __cplusplus
(start, end)
Lisp_Object start, end;
#else
(Lisp_Object start,Lisp_Object end)
#endif
{
    interp->enableRawInput(false);

    eval_region(start, end);

    return Qnil;
}

DEFUN("clang-eval-region", Fclang_eval_region, Sclang_eval_region, 2, 2, "r",
      doc: /* eval region as a C++ string. */)
#ifndef __cplusplus
(start, end)
Lisp_Object start, end;
#else
(Lisp_Object start,Lisp_Object end)
#endif
{
    interp->enableRawInput(true);

    eval_region(start, end);

    return Qnil;
}

void
syms_of_clang(void)
{
    defsubr (&Scling_eval);
    defsubr (&Sclang_eval);
    defsubr (&Scling_eval_region);
    defsubr (&Sclang_eval_region);
}

void
init_clang(void)
{
    debug_out = fopen("c:/logs/debug.txt", "wb");

    char *argv[] = {"meadow.exe", NULL};
    int argc = 1;

    llvm::sys::Path exe_path = llvm::sys::Path::GetMainExecutable(argv[0], (void*)syms_of_clang);
    llvm::StringRef bin_dir = exe_path.getDirname();
    const llvm::StringRef emacs_dir = llvm::sys::path::parent_path(bin_dir);
    llvm::Twine inc_dirx = (emacs_dir + "/include");
    std::string inc_dir = inc_dirx.str();

    buffer_output = new llvm::raw_string_ostream(message_buffer);
    interp = new cling::Interpreter(argc, argv);

    static llvm::raw_fd_ostream outs (2, /*ShouldClose*/false);
    metaproc = new cling::MetaProcessor (*interp, outs);

    interp->AddIncludePath(inc_dir);
    interp->enableRawInput(true);

#if 0
    /* setup output */

    clang::CompilerInstance *ci = interp->getCI();
    clang::DiagnosticOptions* DefaultDiagnosticOptions = new clang::DiagnosticOptions();
    DefaultDiagnosticOptions->ShowColors = 0;
    clang::TextDiagnosticPrinter* DiagnosticPrinter
        = new clang::TextDiagnosticPrinter(llvm::errs(), DefaultDiagnosticOptions);
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> DiagIDs(new DiagnosticIDs());
    DiagnosticsEngine* Diagnostics
        = new DiagnosticsEngine(DiagIDs, DefaultDiagnosticOptions,
                                DiagnosticPrinter, /*Owns it*/ true); // LEAKS!
#endif

    //ci->createDiagnostics(DiagnosticPrinter, false, false);
    cling::Interpreter::CompilationResult r;

    metaproc->process(
"#include <windows.h>\n"
"#include <stdio.h>\n"
"#include <stdlib.h>\n"
"extern \"C\" {\n"
"#include <config.h>\n"
"#include <lisp.h>\n"
"}\n", r, NULL
);
    if (r != cling::Interpreter::kSuccess) {
        fprintf(debug_out, "inc fail %s %s %s\n", emacs_dir.data(), inc_dir.c_str(), bin_dir.data());
    }
}

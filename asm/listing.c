/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2019 The NASM Authors - All Rights Reserved
 *   See the file AUTHORS included with the NASM distribution for
 *   the specific copyright holders.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------- */

/*
 * listing.c    listing file generator for the Netwide Assembler
 */

#include "compiler.h"

#include "nctype.h"

#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "strlist.h"
#include "listing.h"

#define LIST_MAX_LEN 1024       /* something sensible */
#define LIST_INDENT  40
#define LIST_HEXBIT  18

typedef struct MacroInhibit MacroInhibit;

static struct MacroInhibit {
    MacroInhibit *next;
    int level;
    int inhibiting;
} *mistack;

static const char xdigit[] = "0123456789ABCDEF";

#define HEX(a,b) (*(a)=xdigit[((b)>>4)&15],(a)[1]=xdigit[(b)&15]);

static char listline[LIST_MAX_LEN];
static bool listlinep;

struct strlist *list_errors;

static char listdata[2 * LIST_INDENT];  /* we need less than that actually */
static int32_t listoffset;

static int32_t listlineno;

static int32_t listp;

static int suppress;            /* for INCBIN & TIMES special cases */

static int listlevel, listlevel_e;

static FILE *listfp;

static void list_emit(void)
{
    int i;
    const struct strlist_entry *e;

    if (listlinep || *listdata) {
        fprintf(listfp, "%6"PRId32" ", listlineno);

        if (listdata[0])
            fprintf(listfp, "%08"PRIX32" %-*s", listoffset, LIST_HEXBIT + 1,
                    listdata);
        else
            fprintf(listfp, "%*s", LIST_HEXBIT + 10, "");

        if (listlevel_e)
            fprintf(listfp, "%s<%d>", (listlevel < 10 ? " " : ""),
                    listlevel_e);
        else if (listlinep)
            fprintf(listfp, "    ");

        if (listlinep)
            fprintf(listfp, " %s", listline);

        putc('\n', listfp);
        listlinep = false;
        listdata[0] = '\0';
    }

    if (list_errors) {
        static const char fillchars[] = " --***XX";
        char fillchar;

        strlist_for_each(e, list_errors) {
            fprintf(listfp, "%6"PRId32"          ", listlineno);
            fillchar = fillchars[e->pvt.u & ERR_MASK];
            for (i = 0; i < LIST_HEXBIT; i++)
                putc(fillchar, listfp);

            if (listlevel_e)
                fprintf(listfp, " %s<%d>", (listlevel < 10 ? " " : ""),
                        listlevel_e);
            else
                fprintf(listfp, "     ");

            fprintf(listfp, "  %s\n", e->str);
        }

        strlist_free(&list_errors);
    }
}

static void list_init(const char *fname)
{
    if (!fname || fname[0] == '\0') {
	listfp = NULL;
	return;
    }

    listfp = nasm_open_write(fname, NF_TEXT);
    if (!listfp) {
        nasm_nonfatal("unable to open listing file `%s'", fname);
        return;
    }

    *listline = '\0';
    listlineno = 0;
    list_errors = NULL;
    listp = true;
    listlevel = 0;
    suppress = 0;
    mistack = nasm_malloc(sizeof(MacroInhibit));
    mistack->next = NULL;
    mistack->level = 0;
    mistack->inhibiting = true;
}

static void list_cleanup(void)
{
    if (!listp)
        return;

    while (mistack) {
        MacroInhibit *temp = mistack;
        mistack = temp->next;
        nasm_free(temp);
    }

    list_emit();
    fclose(listfp);
}

static void list_out(int64_t offset, char *str)
{
    if (strlen(listdata) + strlen(str) > LIST_HEXBIT) {
        strcat(listdata, "-");
        list_emit();
    }
    if (!listdata[0])
        listoffset = offset;
    strcat(listdata, str);
}

static void list_address(int64_t offset, const char *brackets,
			 int64_t addr, int size)
{
    char q[20];
    char *r = q;

    nasm_assert(size <= 8);

    *r++ = brackets[0];
    while (size--) {
	HEX(r, addr);
	addr >>= 8;
	r += 2;
    }
    *r++ = brackets[1];
    *r = '\0';
    list_out(offset, q);
}

static void list_output(const struct out_data *data)
{
    char q[24];
    uint64_t size = data->size;
    uint64_t offset = data->offset;
    const uint8_t *p = data->data;


    if (!listp || suppress || user_nolist)
        return;

    switch (data->type) {
    case OUT_ZERODATA:
        if (size > 16) {
            snprintf(q, sizeof(q), "<zero %08"PRIX64">", size);
            list_out(offset, q);
            break;
        } else {
            p = zero_buffer;
        }
        /* fall through */
    case OUT_RAWDATA:
    {
	if (size == 0 && !listdata[0])
	    listoffset = data->offset;
        while (size--) {
            HEX(q, *p);
            q[2] = '\0';
            list_out(offset++, q);
            p++;
        }
	break;
    }
    case OUT_ADDRESS:
        list_address(offset, "[]", data->toffset, size);
	break;
    case OUT_SEGMENT:
        q[0] = '[';
        memset(q+1, 's', size << 1);
        q[(size << 1)+1] = ']';
        q[(size << 1)+2] = '\0';
        list_out(offset, q);
        offset += size;
        break;
    case OUT_RELADDR:
	list_address(offset, "()", data->toffset, size);
	break;
    case OUT_RESERVE:
    {
        snprintf(q, sizeof(q), "<res %"PRIX64">", size);
        list_out(offset, q);
	break;
    }
    default:
        panic();
    }
}

static void list_line(int type, int32_t lineno, const char *line)
{
    if (!listp)
        return;

    if (user_nolist)
      return;

    if (mistack && mistack->inhibiting) {
        if (type == LIST_MACRO)
            return;
        else {                  /* pop the m i stack */
            MacroInhibit *temp = mistack;
            mistack = temp->next;
            nasm_free(temp);
        }
    }
    list_emit();
    if (lineno >= 0)
        listlineno = lineno;
    listlinep = true;
    strlcpy(listline, line, LIST_MAX_LEN-3);
    memcpy(listline + LIST_MAX_LEN-4, "...", 4);
    listlevel_e = listlevel;
}

static void mistack_push(bool inhibiting)
{
    MacroInhibit *temp = nasm_malloc(sizeof(MacroInhibit));
    temp->next = mistack;
    temp->level = listlevel;
    temp->inhibiting = inhibiting;
    mistack = temp;
}

static void list_uplevel(int type, int64_t size)
{
    char str[64];

    if (!listp)
        return;

    switch (type) {
    case LIST_INCBIN:
        suppress |= 1;
        snprintf(str, sizeof str, "<bin %"PRIX64">", size);
        list_out(listoffset, str);
        break;

    case LIST_TIMES:
        suppress |= 2;
        snprintf(str, sizeof str, "<rep %"PRIX64">", size);
        list_out(listoffset, str);
        break;

    case LIST_INCLUDE:
        listlevel++;
        if (mistack && mistack->inhibiting)
            mistack_push(false);
        break;

    case LIST_MACRO_NOLIST:
        listlevel++;
        mistack_push(true);
        break;

    default:
        listlevel++;
        break;
    }
}

static void list_downlevel(int type)
{
    if (!listp)
        return;

    switch (type) {
    case LIST_INCBIN:
        suppress &= ~1;
        break;

    case LIST_TIMES:
        suppress &= ~2;
        break;

    default:
        listlevel--;
        while (mistack && mistack->level > listlevel) {
            MacroInhibit *temp = mistack;
            mistack = temp->next;
            nasm_free(temp);
        }
        break;
    }
}

static void list_error(errflags severity, const char *fmt, ...)
{
    va_list ap;

    if (!listfp)
	return;

    if (!list_errors)
        list_errors = strlist_alloc(false);

    va_start(ap, fmt);
    strlist_vprintf(list_errors, fmt, ap);
    va_end(ap);
    strlist_tail(list_errors)->pvt.u = severity;

    if ((severity & ERR_MASK) >= ERR_FATAL)
	list_emit();
}

static void list_set_offset(uint64_t offset)
{
    listoffset = offset;
}

static const struct lfmt nasm_list = {
    list_init,
    list_cleanup,
    list_output,
    list_line,
    list_uplevel,
    list_downlevel,
    list_error,
    list_set_offset
};

const struct lfmt *lfmt = &nasm_list;

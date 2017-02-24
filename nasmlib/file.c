/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2017 The NASM Authors - All Rights Reserved
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

#include "file.h"

void nasm_write(const void *ptr, size_t size, FILE *f)
{
    size_t n = fwrite(ptr, 1, size, f);
    if (n != size || ferror(f) || feof(f))
        nasm_fatal(0, "unable to write output: %s", strerror(errno));
}

#ifdef WORDS_LITTLEENDIAN

void fwriteint16_t(uint16_t data, FILE * fp)
{
    nasm_write(&data, 2, fp);
}

void fwriteint32_t(uint32_t data, FILE * fp)
{
    nasm_write(&data, 4, fp);
}

void fwriteint64_t(uint64_t data, FILE * fp)
{
    nasm_write(&data, 8, fp);
}

void fwriteaddr(uint64_t data, int size, FILE * fp)
{
    nasm_write(&data, size, fp);
}

#else /* not WORDS_LITTLEENDIAN */

void fwriteint16_t(uint16_t data, FILE * fp)
{
    char buffer[2], *p = buffer;
    WRITESHORT(p, data);
    nasm_write(buffer, 2, fp);
}

void fwriteint32_t(uint32_t data, FILE * fp)
{
    char buffer[4], *p = buffer;
    WRITELONG(p, data);
    nasm_write(buffer, 4, fp);
}

void fwriteint64_t(uint64_t data, FILE * fp)
{
    char buffer[8], *p = buffer;
    WRITEDLONG(p, data);
    nasm_write(buffer, 8, fp);
}

void fwriteaddr(uint64_t data, int size, FILE * fp)
{
    char buffer[8], *p = buffer;
    WRITEADDR(p, data, size);
    nasm_write(buffer, size, fp);
}

#endif


void fwritezero(off_t bytes, FILE *fp)
{
    size_t blksize;

#ifdef nasm_ftruncate
    if (bytes >= BUFSIZ && !ferror(fp) && !feof(fp)) {
	off_t pos = ftello(fp);
	if (pos >= 0) {
            pos += bytes;
	    if (!fflush(fp) &&
		!nasm_ftruncate(fileno(fp), pos) &&
		!fseeko(fp, pos, SEEK_SET))
		    return;
	}
    }
#endif

    while (bytes > 0) {
	blksize = (bytes < ZERO_BUF_SIZE) ? bytes : ZERO_BUF_SIZE;

	nasm_write(zero_buffer, blksize, fp);
	bytes -= blksize;
    }
}

FILE *nasm_open_read(const char *filename, enum file_flags flags)
{
    FILE *f;
    bool again = true;

#ifdef __GLIBC__
    /*
     * Try to open this file with memory mapping for speed, unless we are
     * going to do it "manually" with nasm_map_file()
     */
    if (!(flags & NF_FORMAP)) {
        f = fopen(filename, (flags & NF_TEXT) ? "rtm" : "rbm");
        again = (!f) && (errno == EINVAL); /* Not supported, try without m */
    }
#endif

    if (again)
        f = fopen(filename, (flags & NF_TEXT) ? "rt" : "rb");

    if (!f && (flags & NF_FATAL))
        nasm_fatal(ERR_NOFILE, "unable to open input file: `%s': %s",
                   filename, strerror(errno));

    return f;
}

FILE *nasm_open_write(const char *filename, enum file_flags flags)
{
    FILE *f;

    f = fopen(filename, (flags & NF_TEXT) ? "wt" : "wb");

    if (!f && (flags & NF_FATAL))
        nasm_fatal(ERR_NOFILE, "unable to open output file: `%s': %s",
                   filename, strerror(errno));

    return f;
}

/*
 * Report the existence of a file
 */
bool nasm_file_exists(const char *filename)
{
#if defined(HAVE_FACCESSAT) && defined(AT_EACCESS)
    return faccessat(AT_FDCWD, filename, R_OK, AT_EACCESS) == 0;
#elif defined(HAVE_ACCESS)
    return access(filename, R_OK) == 0;
#else
    FILE *f;

    f = fopen(filename, "rb");
    if (f) {
        fclose(f);
        return true;
    } else {
        return false;
    }
#endif
}

/*
 * Report file size.  This MAY move the file pointer.
 */
off_t nasm_file_size(FILE *f)
{
#if defined(HAVE_FILENO) && defined(HAVE__FILELENGTHI64)
    return _filelengthi64(fileno(f));
#elif defined(nasm_fstat)
    struct nasm_stat st;

    if (nasm_fstat(fileno(f), &st))
        return (off_t)-1;

    return st.st_size;
#else
    if (fseeko(f, 0, SEEK_END))
        return (off_t)-1;

    return ftello(f);
#endif
}

/*
 * Report file size given pathname
 */
off_t nasm_file_size_by_path(const char *pathname)
{
#ifdef HAVE_STAT
    struct stat st;

    if (stat(pathname, &st))
        return (off_t)-1;

    return st.st_size;
#else
    FILE *fp;
    off_t len;

    fp = nasm_open_read(pathname, NF_BINARY);
    if (!fp)
        return (off_t)-1;

    len = nasm_file_size(fp);
    fclose(fp);

    return len;
#endif
}

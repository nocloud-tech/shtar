/*
Copyright (C) 2022 Michael Ainsworth.
Licensed under GPL-3.0-or-later.
See <https://www.gnu.org/licenses/>. 

This file is part of shtar.

shtar s free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

shtar is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
shtar. If not, see <https://www.gnu.org/licenses/>. 
*/

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

typedef int shtar_result_t;

#define SHTAR_ERROR ((shtar_result_t)-1)
#define SHTAR_OK    ((shtar_result_t)0)

#define ecleanup(msg)             \
do                                \
{                                 \
    fprintf(stderr, "%s\n", msg); \
    goto cleanup;                 \
} while (0);                      \

static char *ioe = "Error writing file.";
static char *default_sh_path = "/bin/sh";

shtar_result_t run(int argc, char **argv);
shtar_result_t quote(FILE *out, char *dst_name);
static char *shtar_basename(char *path);
shtar_result_t encode_shebang(FILE *out, int use_shebang, char *sh_path);
shtar_result_t encode_file(FILE *in, FILE *out, int use_shebang, char *sh_path, char *dst_name);
shtar_result_t encode_directory(DIR *in, FILE *out, int use_shebang, char *sh_path, char *dirname, char *dst_name);

int main(int argc, char **argv)
{
    return !run(argc - 1, argv + 1) ? EXIT_SUCCESS : EXIT_FAILURE;
}

shtar_result_t run(int argc, char **argv)
{
    shtar_result_t r = SHTAR_ERROR;
    char *in_name = NULL;
    char *out_name = NULL;
    FILE *in = NULL;
    DIR *in_dir = NULL;
    FILE *out = NULL;
    int use_shebang = 0;
    char *sh_path = NULL;
    char *dst_name = NULL;
    int i;

    for (i = 0; i < argc; ++i)
    {
        if (!i && (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")))
        {
            system("man 1 shtar");
            r = SHTAR_OK;
            goto cleanup;
        }
        else if (!strcmp(argv[i], "-i") || !strcmp(argv[i], "--input"))
        {
            if (i >= argc - 1) ecleanup("Expected input file name.");
            ++i;
            in_name = argv[i];
        }
        else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output"))
        {
            if (i >= argc - 1) ecleanup("Expected output file name.");
            ++i;
            out_name = argv[i];
        }
        else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--shebang"))
        {
            use_shebang = 1;
        }
        else if (!strcmp(argv[i], "-S") || !strcmp(argv[i], "--no-shebang"))
        {
            use_shebang = 0;
        }
        else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--sh-path"))
        {
            if (i >= argc - 1) ecleanup("Expected shell path.");
            ++i;
            sh_path = argv[i];
        }
        else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--destination"))
        {
            if (i >= argc - 1) ecleanup("Expected destination file name.");
            ++i;
            dst_name = argv[i];
        }
        else ecleanup("Unknown argument. Use 'shtar --help'.");
    }

    if (!in_name)
    {
        in = stdin;
    }
    else
    {
        in_dir = opendir(in_name);
        if (!in_dir)
        {
            if (ENOTDIR != errno)
            {
                ecleanup("Unable to open input.");
            }
        }

        if (!in_dir)
        {
            if (!strcmp(in_name, "-"))
            {
                in = stdin;
            }
            else
            {
                in = fopen(in_name, "rb");
                if (!in) ecleanup("Unable to open input file.");
            }
        }
    }

    if (!out_name)
    {
        out = stdout;
    }
    else
    {
        if (!strcmp(out_name, "-"))
        {
            out = stdout;
        }
        else
        {
            out = fopen(out_name, "wb");
            if (!out) ecleanup("Unable to open output file.");
        }
    }

    if (!sh_path)
    {
        sh_path = default_sh_path;
    }

    if (in_dir)
    {
        dst_name = dst_name ? dst_name : shtar_basename(in_name);
        if (encode_directory(in_dir, out, use_shebang, sh_path, in_name, dst_name))
            goto cleanup;
    }
    else
    {
        if (encode_file(in, out, use_shebang, sh_path, dst_name)) goto cleanup;
    }

    if (out && out != stdout)
    {
        if (fchmod(fileno(out), S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP))
        {
            ecleanup("Unable to set permissions on archive.");
        }
    }

    r = SHTAR_OK;

cleanup:
    if (out && out != stdout)
    {
        fclose(out);
        out = NULL;
    }

    if (in_dir)
    {
        closedir(in_dir);
        in_dir = NULL;
    }

    if (in && in != stdin)
    {
        fclose(in);
        in = NULL;
    }

    return r;
}

static char *shtar_basename(char *path)
{
    char *r = NULL;
    assert(path);
    r = rindex(path, '/');
    if (!r) r = path;
    else
    {
        r = r + 1;
        if (!r) r = path;
    }
    return r;
}

shtar_result_t quote(FILE *out, char *dst_name)
{
    shtar_result_t r = SHTAR_ERROR;
    size_t i, len;
    char c;

    assert(out);
    assert(dst_name);

    len = strlen(dst_name);
    if (!len) ecleanup("Invalid quoted string length.");

    if (0 > fprintf(out, "'")) ecleanup(ioe);
    for (i = 0; i < len; ++i)
    {
        c = dst_name[i];
        if ('\'' == c)
        {
            if (0 > fprintf(out, "'\"'\"'")) ecleanup(ioe);
        }
        else
        {
            if (0 > fprintf(out, "%c", c)) ecleanup(ioe);
        }
    }
    if (0 > fprintf(out, "'")) ecleanup(ioe);

    r = SHTAR_OK;

cleanup:
    return r;
}

shtar_result_t encode_shebang(FILE *out, int use_shebang, char *sh_path)
{
    shtar_result_t r = SHTAR_ERROR;

    assert(out);
    assert(sh_path);

    if (use_shebang)
    {
        if (0 > fprintf(out, "#!%s\n", sh_path)) ecleanup(ioe);
    }

    r = SHTAR_OK;

cleanup:
    return r;

}

shtar_result_t encode_file(FILE *in, FILE *out, int use_shebang, char *sh_path, char *dst_name)
{
    shtar_result_t r = SHTAR_ERROR;
    int c;

    assert(in);
    assert(out);
    assert(sh_path);

    if (encode_shebang(out, use_shebang, sh_path)) goto cleanup;

    if (0 > fprintf(out, "(\n")) ecleanup(ioe);

    while (1)
    {
        c = fgetc(in);
        if (EOF == c)
        {
            if (ferror(in)) ecleanup("Error reading file.");
            break;
        }

        if (0 > fprintf(out, "printf \"\\%03o\"\n", (unsigned int)c)) ecleanup(ioe);
    }

    if (0 > fprintf(out, ")")) ecleanup(ioe);
    if (dst_name)
    {
        if (0 > fprintf(out, " > ")) ecleanup(ioe);
        if (quote(out, dst_name)) ecleanup(ioe);
    }
    if (0 > fprintf(out, "\n")) ecleanup(ioe);

    r = SHTAR_OK;

cleanup:
    return r;
}

shtar_result_t encode_directory(DIR *in, FILE *out, int use_shebang, char *sh_path, char *dirname, char *dst_name)
{
    shtar_result_t r = SHTAR_ERROR;
    struct dirent *entry = NULL;
    DIR *subdir = NULL;
    FILE *subfile = NULL;
    int dir_changed = 0;

    assert(in);
    assert(out);
    assert(sh_path);

    if (encode_shebang(out, use_shebang, sh_path)) goto cleanup;

    if (0 > fprintf(out, "mkdir ")) ecleanup(ioe);
    if (quote(out, dst_name)) ecleanup(ioe);
    if (0 > fprintf(out, "\n")) ecleanup(ioe);

    if (0 > fprintf(out, "cd ")) ecleanup(ioe);
    if (quote(out, dst_name)) ecleanup(ioe);
    if (0 > fprintf(out, "\n")) ecleanup(ioe);

    if (chdir(dirname)) ecleanup("Unable to change directory.");
    dir_changed = 1;

    while (1)
    {
        entry = readdir(in);
        if (!entry) break;

        if (!strcmp(entry->d_name, ".")) continue;
        if (!strcmp(entry->d_name, "..")) continue;

        if (DT_DIR == entry->d_type)
        {
            if (subdir)
            {
                closedir(subdir);
                subdir = NULL;
            }

            subdir = opendir(entry->d_name);
            if (!subdir) ecleanup("Unable to open subdirectory.");
            if (encode_directory(subdir, out, 0, "", entry->d_name, entry->d_name)) goto cleanup;
        }
        else if (DT_LNK == entry->d_type || DT_REG == entry->d_type)
        {
            if (subfile)
            {
                fclose(subfile);
                subfile = NULL;
            }

            subfile = fopen(entry->d_name, "rb");
            if (!subfile) ecleanup("Unable to open subfile.");
            if (encode_file(subfile, out, 0, "", entry->d_name)) goto cleanup;
        }
        else
        {
            ecleanup("Unhandled directory entry type.");
        }
    }

    if (0 > fprintf(out, "cd ..\n")) ecleanup(ioe);

    r = SHTAR_OK;

cleanup:
    if (subfile)
    {
        fclose(subfile);
        subfile = NULL;
    }

    if (subdir)
    {
        closedir(subdir);
        subdir = NULL;
    }

    if (dir_changed)
    {
        if (chdir(".."))
        {
            fprintf(stderr, "Unable to go to higher level directory.");
            r = SHTAR_ERROR;
        }

    }

    return r;
}

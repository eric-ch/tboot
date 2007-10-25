/*
 * tb_polgen.h: types and definitions for tb_polgen
 *
 * Copyright (c) 2006-2007, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __TB_POLGEN_H__
#define __TB_POLGEN_H__

extern bool verbose;

#define error_msg(fmt, ...)         fprintf(stderr, fmt, ##__VA_ARGS__)
#define info_msg(fmt, ...)          if (verbose) printf(fmt , ##__VA_ARGS__)

#define ARRAY_SIZE(a)     (sizeof(a) / sizeof((a)[0]))

typedef enum {
    POLGEN_CMD_NONE, POLGEN_CMD_HELP, POLGEN_CMD_CREATE, POLGEN_CMD_SHOW
} polgen_cmd_t;

typedef struct {
    polgen_cmd_t       cmd;
    uuid_t             uuid;
    tb_policy_type_t   policy_type;
    tb_hash_type_t     hash_type;
    char               policy_file[FILENAME_MAX];
    char               cmdline[256];
    int                num_infiles;
    char               infiles[4][FILENAME_MAX];
} param_data_t;

/* in param.c */
extern bool parse_input_params(int argc, char **argv, param_data_t *params);
extern void display_help_msg(void);
extern void print_params(param_data_t *params);

/* in commands.c */
extern bool do_create(const param_data_t *params);
extern bool do_show(const param_data_t *params);

/* in policy.c */
extern bool read_policy_file(const char *policy_filename);
extern bool write_policy_file(const char *policy_filename);
extern void display_policy(void);
extern void modify_policy_index(tb_policy_type_t policy_type);
extern bool replace_policy(const uuid_t *uuid, tb_hash_type_t hash_type,
                           const tb_hash_t *hash);

#endif /* __TB_POLGEN_H__ */


/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/*
 * hash.c: support functions for tb_hash_t type
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <openssl/sha.h>
#define PRINT   printf
#include "../include/uuid.h"
#include "../include/hash.h"
#include "../include/tb_error.h"
#include "../include/tb_policy.h"
#include "tb_polgen.h"

/*
 * are_hashes_equal
 *
 * compare whether two hash values are equal.
 *
 */
bool are_hashes_equal(const tb_hash_t *hash1, const tb_hash_t *hash2,
		      uint8_t hash_alg)
{
    if ( ( hash1 == NULL ) || ( hash2 == NULL ) ) {
        error_msg("Error: hash pointer is zero.\n");
        return false;
    }

    if ( hash_alg == TB_HALG_SHA1 )
        return (memcmp(hash1, hash2, SHA1_LENGTH) == 0);
    else {
        error_msg("unsupported hash alg (%d)\n", hash_alg);
        return false;
    }
}

/*
 * hash_buffer
 *
 * hash the buffer according to the algorithm
 *
 */
bool hash_buffer(const unsigned char* buf, int size, tb_hash_t *hash,
		 uint8_t hash_alg)
{
    if ( hash == NULL ) {
        error_msg("Error: There is no space for output hash.\n");
        return false;
    }

    if ( hash_alg == TB_HALG_SHA1 ) {
        SHA1(buf, size, hash->sha1);
        return true;
    }
    else {
        error_msg("unsupported hash alg (%d)\n", hash_alg);
        return false;
    }
}

/*
 * extend_hash
 *
 * perform "extend" of two hashes (i.e. hash1 = SHA(hash1 || hash2)
 *
 */
bool extend_hash(tb_hash_t *hash1, const tb_hash_t *hash2, uint8_t hash_alg)
{
    uint8_t buf[2*sizeof(tb_hash_t)];

    if ( hash1 == NULL || hash2 == NULL ) {
        error_msg("Error: There is no space for output hash.\n");
        return false;
    }

    if ( hash_alg == TB_HALG_SHA1 ) {
        memcpy(buf, &(hash1->sha1), sizeof(hash1->sha1));
        memcpy(buf + sizeof(hash1->sha1), &(hash2->sha1), sizeof(hash1->sha1));
        SHA1(buf, 2*sizeof(hash1->sha1), hash1->sha1);
        return true;
    }
    else {
        error_msg("unsupported hash alg (%d)\n", hash_alg);
        return false;
    }
}

void print_hash(const tb_hash_t *hash, uint8_t hash_alg)
{
    int i;

    if ( hash == NULL ) {
        error_msg("NULL");
        return;
    }

    if ( hash_alg == TB_HALG_SHA1 ) {
        for ( i = 0; i < sizeof(hash->sha1); i++ )
            printf("%02x ", hash->sha1[i]);
        printf("\n");
    }
    else {
        error_msg("unsupported hash alg (%d)\n", hash_alg);
        return;
    }
}

void copy_hash(tb_hash_t *dest_hash, const tb_hash_t *src_hash,
               uint8_t hash_alg)
{
    if ( dest_hash == NULL || dest_hash == NULL ) {
        error_msg("hashes are NULL\n");
        return;
    }

    if ( hash_alg == TB_HALG_SHA1 )
        memcpy(dest_hash, src_hash, SHA1_LENGTH);
    else
        error_msg("unsupported hash alg (%d)\n", hash_alg);
}



/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

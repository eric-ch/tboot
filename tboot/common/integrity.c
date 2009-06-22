/*
 * integrity.c: routines for memory integrity measurement & 
 *          verification. Memory integrity is protected with tpm seal
 *
 * Copyright (c) 2007-2009, Intel Corporation
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

#include <config.h>
#include <types.h>
#include <stdbool.h>
#include <printk.h>
#include <misc.h>
#include <compiler.h>
#include <string2.h>
#include <hash.h>
#include <tpm.h>
#include <tboot.h>
#include <tb_policy.h>
#include <tb_error.h>
#include <vmac.h>
#include <integrity.h>

#include <page.h>
extern char _end[];

/* put in .data section to that they aren't cleared on S3 resume */

/* state whose integrity needs to be maintained across S3 */
__data pre_k_s3_state_t g_pre_k_s3_state;
__data post_k_s3_state_t g_post_k_s3_state;

/* state sealed before extending PCRs and launching kernel */
static __data uint8_t  sealed_pre_k_state[512];
static __data uint32_t sealed_pre_k_state_size;

/* state sealed just before entering S3 (after kernel shuts down) */
static __data uint8_t  sealed_post_k_state[512];
static __data uint32_t sealed_post_k_state_size;

/* PCR 17+18 values post-launch and before extending (used to seal verified
   launch hashes and memory integrity UMAC) */
static __data tpm_pcr_value_t post_launch_pcr17, post_launch_pcr18;

extern tboot_shared_t _tboot_shared;

extern bool hash_policy(tb_hash_t *hash, uint8_t hash_alg);


typedef struct {
    uint8_t mac_key[VMAC_KEY_LEN/8];
    uint8_t shared_key[sizeof(_tboot_shared.s3_key)];
} sealed_secrets_t;


static bool extend_pcrs(void)
{
    tpm_pcr_value_t pcr17, pcr18;

    tpm_pcr_read(2, 17, &pcr17);
    tpm_pcr_read(2, 18, &pcr18);
    printk("PCRs before extending:\n");
    printk("  PCR 17: "); print_hash((tb_hash_t *)&pcr17, TB_HALG_SHA1);
    printk("  PCR 18: "); print_hash((tb_hash_t *)&pcr18, TB_HALG_SHA1);

    for ( int i = 0; i < g_pre_k_s3_state.num_vl_entries; i++ ) {
        if ( tpm_pcr_extend(2, g_pre_k_s3_state.vl_entries[i].pcr,
                     (tpm_pcr_value_t *)&g_pre_k_s3_state.vl_entries[i].hash,
                     NULL) != TPM_SUCCESS )
            return false;
    }

    tpm_pcr_read(2, 17, &pcr17);
    tpm_pcr_read(2, 18, &pcr18);
    printk("PCRs after extending:\n");
    printk("  PCR 17: "); print_hash((tb_hash_t *)&pcr17, TB_HALG_SHA1);
    printk("  PCR 18: "); print_hash((tb_hash_t *)&pcr18, TB_HALG_SHA1);

    return true;
}

static void print_pre_k_s3_state(void)
{
    printk("pre_k_s3_state:\n");
    printk("\t vtd_pmr_lo_base: 0x%Lx\n", g_pre_k_s3_state.vtd_pmr_lo_base);
    printk("\t vtd_pmr_lo_size: 0x%Lx\n", g_pre_k_s3_state.vtd_pmr_lo_size);
    printk("\t vtd_pmr_hi_base: 0x%Lx\n", g_pre_k_s3_state.vtd_pmr_hi_base);
    printk("\t vtd_pmr_hi_size: 0x%Lx\n", g_pre_k_s3_state.vtd_pmr_hi_size);
    printk("\t pol_hash: ");
    print_hash(&g_pre_k_s3_state.pol_hash, TB_HALG_SHA1);
    printk("\t VL measurements:\n");
    for ( int i = 0; i < g_pre_k_s3_state.num_vl_entries; i++ ) {
        printk("\t   PCR %d: ", g_pre_k_s3_state.vl_entries[i].pcr);
        print_hash(&g_pre_k_s3_state.vl_entries[i].hash, TB_HALG_SHA1);
    }
}

static void print_post_k_s3_state(void)
{
    printk("post_k_s3_state:\n");
    printk("\t kernel_s3_resume_vector: 0x%Lx\n",
           g_post_k_s3_state.kernel_s3_resume_vector);
    printk("\t kernel_integ: ");
    print_hex(NULL, &g_post_k_s3_state.kernel_integ,
              sizeof(g_post_k_s3_state.kernel_integ));
}

static bool seal_data(const void *data, size_t data_size,
                   const void *secrets, size_t secrets_size,
                   uint8_t *sealed_data, uint32_t *sealed_data_size,
                   const uint8_t pcr_indcs_create[], int nr_pcr_indcs_create,
                   const uint8_t pcr_indcs_release[], int nr_pcr_indcs_release,
                   const tpm_pcr_value_t *pcr_values_release[])
{
    /* TPM_Seal can only seal small data (like key or hash), so hash data */
    struct __packed {
        tb_hash_t data_hash;
        uint8_t   secrets[secrets_size];
    } blob;
    uint32_t err;

    memset(&blob, 0, sizeof(blob));
    if ( !hash_buffer(data, data_size, &blob.data_hash, TB_HALG_SHA1) ) {
        printk("failed to hash data\n");
        return false;
    }

    if ( secrets != NULL && secrets_size > 0 )
        memcpy(blob.secrets, secrets, secrets_size);

    err = tpm_seal(2, TPM_LOC_TWO,
                   nr_pcr_indcs_create, pcr_indcs_create,
                   nr_pcr_indcs_release, pcr_indcs_release,
                   pcr_values_release,
                   sizeof(blob), (const uint8_t *)&blob,
                   sealed_data_size, sealed_data);
    if ( err != TPM_SUCCESS )
        printk("failed to seal data\n");

    /* since blob might contain secret, clear it */
    memset(&blob, 0, sizeof(blob));

    return (err == TPM_SUCCESS) ? true : false;
}

static bool verify_sealed_data(const uint8_t *sealed_data,
                               uint32_t sealed_data_size,
                               const void *curr_data, size_t curr_data_size,
                               void *secrets, size_t secrets_size)
{
    /* sealed data is hash of state data and optional secret */
    struct __packed {
        tb_hash_t data_hash;
        uint8_t   secrets[secrets_size];
    } blob;
    bool err = true;

    uint32_t data_size = sizeof(blob);
    if ( tpm_unseal(2, sealed_data_size, sealed_data,
                    &data_size, (uint8_t *)&blob) != TPM_SUCCESS ) {
        printk("failed to unseal blob\n");
        return false;
    }
    if ( data_size != sizeof(blob) ) {
        printk("unsealed state data size mismatch\n");
        goto done;
    }

    /* verify that (hash of) current data maches sealed hash */
    tb_hash_t curr_data_hash;
    memset(&curr_data_hash, 0, sizeof(curr_data_hash));
    if ( !hash_buffer(curr_data, curr_data_size, &curr_data_hash,
                      TB_HALG_SHA1) ) {
        printk("failed to hash state data\n");
        goto done;
    }
    if ( !are_hashes_equal(&blob.data_hash, &curr_data_hash, TB_HALG_SHA1) ) {
        printk("sealed hash does not match current hash\n");
        goto done;
    }

    if ( secrets != NULL && secrets_size > 0 )
        memcpy(secrets, &blob.secrets, secrets_size);

    err = false;

 done:
    /* clear secret from local memory */
    memset(&blob, 0, sizeof(blob));

    return !err;
}

/*
 * pre- PCR extend/kernel launch S3 data are sealed to PCRs 17+18 with
 * post-launch values (i.e. before extending)
 */
bool seal_pre_k_state(void)
{
    const uint8_t pcr_indcs_create[]  = {17, 18};
    const uint8_t pcr_indcs_release[] = {17, 18};
    const tpm_pcr_value_t *pcr_values_release[] = {&post_launch_pcr17,
                                                   &post_launch_pcr18};

    /* save hash of current policy into g_pre_k_s3_state */
    memset(&g_pre_k_s3_state.pol_hash, 0, sizeof(g_pre_k_s3_state.pol_hash));
    if ( !hash_policy(&g_pre_k_s3_state.pol_hash, TB_HALG_SHA1) ) {
        printk("failed to hash policy\n");
        return false;
    }

    print_pre_k_s3_state();

    /* read PCR 17/18 */
    if ( tpm_pcr_read(2, 17, &post_launch_pcr17) != TPM_SUCCESS )
        return false;
    if ( tpm_pcr_read(2, 18, &post_launch_pcr18) != TPM_SUCCESS )
        return false;

    sealed_pre_k_state_size = sizeof(sealed_pre_k_state);
    if ( !seal_data(&g_pre_k_s3_state, sizeof(g_pre_k_s3_state),
                    NULL, 0,
                    sealed_pre_k_state, &sealed_pre_k_state_size,
                    pcr_indcs_create, ARRAY_SIZE(pcr_indcs_create),
                    pcr_indcs_release, ARRAY_SIZE(pcr_indcs_release),
                    pcr_values_release) )
        return false;

    if ( !extend_pcrs() )
        return false;

    return true;
}

static bool measure_memory_integrity(vmac_t *mac, uint8_t key[VMAC_KEY_LEN/8])
{
    vmac_ctx_t ctx;
    uint8_t nonce[16] = {};

/* even though we require callers to 64 byte align, VMAC only needs 16 */
#define MAC_ALIGN   16

    vmac_set_key(key, &ctx);
    for ( unsigned int i = 0; i < _tboot_shared.num_mac_regions; i++ ) {
        uint64_t start = _tboot_shared.mac_regions[i].start & ~(MAC_ALIGN-1);
        uint32_t size = (_tboot_shared.mac_regions[i].size + MAC_ALIGN - 1) &
                        ~(MAC_ALIGN-1);

        printk("MACing region %u:  0x%Lx - 0x%Lx\n", i, start, start + size);
        /* TBD: don't handle addrs > 4GB yet, so error */
        if ( (start + size) & 0xffffffff00000000UL ) {
            printk("range is in memory > 4GB\n");
            return false;
        }
        vmac_update((uint8_t *)(uintptr_t)start, size, &ctx);
    }
    *mac = vmac(NULL, 0, nonce, NULL, &ctx);

    /* wipe ctx to ensure key not left in memory */
    memset(&ctx, 0, sizeof(ctx));

    return true;
}

/*
 * verify memory integrity and sealed VL hashes, then re-extend hashes
 *
 * this must be called post-launch but before extending any modules or other
 * measurements into PCRs
 */
bool verify_integrity(void)
{
    unsigned int i;
    uint8_t pcr_indcs_create[] = {17, 18};
    tpm_pcr_value_t  pcr17, pcr18;
    const tpm_pcr_value_t *pcr_values_create[] = {&pcr17, &pcr18};

    tpm_pcr_read(2, 17, &pcr17);
    tpm_pcr_read(2, 18, &pcr18);
    printk("PCRs before unseal:\n");
    printk("  PCR 17: "); print_hash((tb_hash_t *)&pcr17, TB_HALG_SHA1);
    printk("  PCR 18: "); print_hash((tb_hash_t *)&pcr18, TB_HALG_SHA1);

    /* verify integrity of pre-kernel state data */
    printk("verifying pre_k_s3_state\n");
    if ( !verify_sealed_data(sealed_pre_k_state, sealed_pre_k_state_size,
                             &g_pre_k_s3_state, sizeof(g_pre_k_s3_state),
                             NULL, 0) )
        return false;

    /* to prevent rollback attack using old sealed measurements,
       verify that (creation) PCRs at mem integrity seal time are same as
       if we extend current PCRs with unsealed VL measurements */
    /* TBD: we should check all DRTM PCRs */
    for ( i = 0; i < g_pre_k_s3_state.num_vl_entries; i++ ) {
        if ( g_pre_k_s3_state.vl_entries[i].pcr == 17 )
            extend_hash((tb_hash_t *)&pcr17,
                        &g_pre_k_s3_state.vl_entries[i].hash, TB_HALG_SHA1);
        else if ( g_pre_k_s3_state.vl_entries[i].pcr == 18 )
            extend_hash((tb_hash_t *)&pcr18,
                        &g_pre_k_s3_state.vl_entries[i].hash, TB_HALG_SHA1);
    }
    if ( !tpm_cmp_creation_pcrs(ARRAY_SIZE(pcr_indcs_create),
                                pcr_indcs_create, pcr_values_create, 
                                sealed_post_k_state_size,
                                sealed_post_k_state) ) {
        printk("extended PCR values don't match creation values in sealed blob.\n");
        return false;
    }

    /* verify integrity of post-kernel state data */
    printk("verifying post_k_s3_state\n");
    sealed_secrets_t secrets;
    if ( !verify_sealed_data(sealed_post_k_state, sealed_post_k_state_size,
                             &g_post_k_s3_state, sizeof(g_post_k_s3_state),
                             &secrets, sizeof(secrets)) )
        return false;

    /* Verify memory integrity against sealed value */
    vmac_t mac;
    if ( !measure_memory_integrity(&mac, secrets.mac_key) )
        return false;
    if ( memcmp(&mac, &g_post_k_s3_state.kernel_integ, sizeof(mac)) ) {
        printk("memory integrity lost on S3 resume\n");
        printk("MAC of current image is: ");
        print_hex(NULL, &mac, sizeof(mac));
        printk("MAC of pre-S3 image is: ");
        print_hex(NULL, &g_post_k_s3_state.kernel_integ,
                  sizeof(g_post_k_s3_state.kernel_integ));
        return false;
    }
    printk("memory integrity OK\n");

    /* re-extend PCRs with VL measurements */
    if ( !extend_pcrs() )
        return false;

    /* copy sealed shared key back to _tboot_shared.s3_key */
    memcpy(_tboot_shared.s3_key, secrets.shared_key,
           sizeof(_tboot_shared.s3_key));

    /* wipe secrets from memory */
    memset(&secrets, 0, sizeof(secrets));

    return true;
}

/*
 * post- kernel launch S3 state is sealed to PCRs 17+18 with post-launch
 * values (i.e. before extending with VL hashes)
 */
bool seal_post_k_state(void)
{
    const uint8_t pcr_indcs_create[]  = {17, 18};
    const uint8_t pcr_indcs_release[] = {17, 18};
    const tpm_pcr_value_t *pcr_values_release[] = {&post_launch_pcr17,
                                                   &post_launch_pcr18};
    sealed_secrets_t secrets;

    /* since tboot relies on the module it launches for resource protection,
       that module should have at least one region for itself, otherwise
       it will not be protected against S3 resume attacks */
    //    if ( _tboot_shared.num_mac_regions == 0 ) {
    //        printk("no memory regions to MAC\n");
    //        return false;
    //    }

    /* calculate the memory integrity hash */
    uint32_t key_size = sizeof(secrets.mac_key);
    /* key must be random and secret even though auth not necessary */
    if ( tpm_get_random(2, secrets.mac_key, &key_size) != TPM_SUCCESS ||
         key_size != sizeof(secrets.mac_key) )
        return false;
    if ( !measure_memory_integrity(&g_post_k_s3_state.kernel_integ,
                                   secrets.mac_key) )
        return false;

    /* copy s3_key into secrets to be sealed */
    memcpy(secrets.shared_key, _tboot_shared.s3_key, sizeof(secrets.shared_key));

    print_post_k_s3_state();

    sealed_post_k_state_size = sizeof(sealed_post_k_state);
    if ( !seal_data(&g_post_k_s3_state, sizeof(g_post_k_s3_state),
                    &secrets, sizeof(secrets),
                    sealed_post_k_state, &sealed_post_k_state_size,
                    pcr_indcs_create, ARRAY_SIZE(pcr_indcs_create),
                    pcr_indcs_release, ARRAY_SIZE(pcr_indcs_release),
                    pcr_values_release) )
        return false;

    /* wipe secrets from memory */
    memset(&secrets, 0, sizeof(secrets));

    return true;
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

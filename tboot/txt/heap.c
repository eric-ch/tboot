/*
 * heap.c: fns for verifying and printing the Intel(r) TXT heap data structs
 *
 * Copyright (c) 2003-2008, Intel Corporation
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
#include <compiler.h>
#include <string.h>
#include <printk.h>
#include <multiboot.h>
#include <uuid.h>
#include <mle.h>
#include <txt/mtrrs.h>
#include <txt/config_regs.h>
#include <txt/heap.h>

static void print_hash(sha1_hash_t hash)
{
    for ( int i = 0; i < SHA1_SIZE; i++ )
        printk("%02x ", hash[i]);
    printk("\n");
}

static void print_bios_data(bios_data_t *bios_data)
{
    printk("bios_data (@%p, %Lx):\n", bios_data,
           *((uint64_t *)bios_data - 1));
    printk("\t version: %u\n", bios_data->version);
    printk("\t bios_sinit_size: 0x%x (%u)\n", bios_data->bios_sinit_size,
           bios_data->bios_sinit_size);
    printk("\t lcp_pd_base: 0x%Lx\n", bios_data->lcp_pd_base);
    printk("\t lcp_pd_size: 0x%Lx (%Lu)\n", bios_data->lcp_pd_size,
           bios_data->lcp_pd_size);
    printk("\t num_logical_procs: %u\n", bios_data->num_logical_procs);
    if ( bios_data->version >= 3 )
        printk("\t flags: 0x%08Lx\n", bios_data->flags);
}

bool verify_bios_data(txt_heap_t *txt_heap)
{
    uint64_t size, heap_size;
    bios_data_t *bios_data;

    /* check size */
    heap_size = read_priv_config_reg(TXTCR_HEAP_SIZE);
    size = get_bios_data_size(txt_heap);
    if ( size == 0 ) {
        printk("BIOS data size is 0\n");
        return false;
    }
    if ( size > heap_size ) {
        printk("BIOS data size is larger than heap size "
               "(%Lx, heap size=%Lx)\n", size, heap_size);
        return false;
    }

    bios_data = get_bios_data_start(txt_heap);

    /* check version */
    if ( bios_data->version < 2 ) {
        printk("unsupported BIOS data version (%u)\n", bios_data->version);
        return false;
    }
    /* we assume backwards compatibility but print a warning */
    if ( bios_data->version > 3 )
        printk("unsupported BIOS data version (%u)\n", bios_data->version);

    /* all TXT-capable CPUs support at least 2 cores */
    if ( bios_data->num_logical_procs < 2 ) {
        printk("BIOS data has incorrect num_logical_procs (%u)\n",
               bios_data->num_logical_procs);
        return false;
    }
    else if ( bios_data->num_logical_procs >= NR_CPUS ) {
        printk("BIOS data specifies too many CPUs (%u)\n",
               bios_data->num_logical_procs);
        return false;
    }

    print_bios_data(bios_data);

    return true;
}

static void print_os_mle_data(os_mle_data_t *os_mle_data)
{
    printk("os_mle_data (@%p, %Lx):\n", os_mle_data,
           *((uint64_t *)os_mle_data - 1));
    printk("\t version: %u\n", os_mle_data->version);
    /* TBD: perhaps eventually print saved_mtrr_state field */
    printk("\t mbi: 0x%p\n", os_mle_data->mbi);
}

static bool verify_os_mle_data(txt_heap_t *txt_heap)
{
    uint64_t size, heap_size;
    os_mle_data_t *os_mle_data;

    /* check size */
    heap_size = read_priv_config_reg(TXTCR_HEAP_SIZE);
    size = get_os_mle_data_size(txt_heap);
    if ( size == 0 ) {
        printk("OS to MLE data size is 0\n");
        return false;
    }
    if ( size > heap_size ) {
        printk("OS to MLE data size is larger than heap size "
               "(%Lx, heap size=%Lx)\n", size, heap_size);
        return false;
    }
    if ( size < sizeof(os_mle_data_t) ) {
        printk("OS to MLE data size (%Lx) is smaller than "
               "os_mle_data_t size (%x)\n", size, sizeof(os_mle_data_t));
        return false;
    }

    os_mle_data = get_os_mle_data_start(txt_heap);

    /* check version */
    /* since this data is from our pre-launch to post-launch code only, it */
    /* should always be this */
    if ( os_mle_data->version != 1 ) {
        printk("unsupported OS to MLE data version (%u)\n",
               os_mle_data->version);
        return false;
    }

    /* field checks */
    if ( os_mle_data->mbi == NULL ) {
        printk("OS to MLE data mbi field is NULL\n");
        return false;
    }

    print_os_mle_data(os_mle_data);

    return true;
}

void print_os_sinit_data(os_sinit_data_t *os_sinit_data)
{
    printk("os_sinit_data (@%p, %Lx):\n", os_sinit_data,
           *((uint64_t *)os_sinit_data - 1));
    printk("\t version: %u\n", os_sinit_data->version);
    printk("\t mle_ptab: 0x%Lx\n", os_sinit_data->mle_ptab);
    printk("\t mle_size: 0x%Lx (%Lu)\n", os_sinit_data->mle_size,
           os_sinit_data->mle_size);
    printk("\t mle_hdr_base: 0x%Lx\n", os_sinit_data->mle_hdr_base);
    printk("\t vtd_pmr_lo_base: 0x%Lx\n", os_sinit_data->vtd_pmr_lo_base);
    printk("\t vtd_pmr_lo_size: 0x%Lx\n", os_sinit_data->vtd_pmr_lo_size);
    printk("\t vtd_pmr_hi_base: 0x%Lx\n", os_sinit_data->vtd_pmr_hi_base);
    printk("\t vtd_pmr_hi_size: 0x%Lx\n", os_sinit_data->vtd_pmr_hi_size);
    printk("\t lcp_po_base: 0x%Lx\n", os_sinit_data->lcp_po_base);
    printk("\t lcp_po_size: 0x%Lx (%Lu)\n", os_sinit_data->lcp_po_size,
           os_sinit_data->lcp_po_size);
    print_txt_caps("\t ", os_sinit_data->capabilities);
}

static bool verify_os_sinit_data(txt_heap_t *txt_heap)
{
    uint64_t size, heap_size;
    os_sinit_data_t *os_sinit_data;

    /* check size */
    heap_size = read_priv_config_reg(TXTCR_HEAP_SIZE);
    size = get_os_sinit_data_size(txt_heap);
    if ( size == 0 ) {
        printk("OS to SINIT data size is 0\n");
        return false;
    }
    if ( size > heap_size ) {
        printk("OS to SINIT data size is larger than heap size "
               "(%Lx, heap size=%Lx)\n", size, heap_size);
        return false;
    }

    os_sinit_data = get_os_sinit_data_start(txt_heap);

    /* check version (but since we create this, it should always be OK) */
    if ( os_sinit_data->version > 4 ) {
        printk("unsupported OS to SINIT data version (%u)\n",
               os_sinit_data->version);
        return false;
    }

    /* only check minimal size */
    if ( size < sizeof(os_sinit_data_t) ) {
        printk("OS to SINIT data size (%Lx) is smaller than "
               "os_sinit_data_t (%x)\n", size, sizeof(os_sinit_data_t));
        return false;
    }

    print_os_sinit_data(os_sinit_data);

    return true;
}

static void print_sinit_mdrs(sinit_mdr_t mdrs[], uint32_t num_mdrs)
{
    static char *mem_types[] = {"GOOD", "SMRAM OVERLAY", "SMRAM NON-OVERLAY",
                                "PCIE EXTENDED CONFIG", "PROTECTED"};

    printk("\t sinit_mdrs:\n");
    for ( int i = 0; i < num_mdrs; i++ ) {
        printk("\t\t %016Lx - %016Lx ", mdrs[i].base,
               mdrs[i].base + mdrs[i].length);
        if ( mdrs[i].mem_type < sizeof(mem_types)/sizeof(mem_types[0]) )
            printk("(%s)\n", mem_types[mdrs[i].mem_type]);
        else
            printk("(%d)\n", (int)mdrs[i].mem_type);
    }
}

static void print_sinit_mle_data(sinit_mle_data_t *sinit_mle_data)
{
    printk("sinit_mle_data (@%p, %Lx):\n", sinit_mle_data,
           *((uint64_t *)sinit_mle_data - 1));
    printk("\t version: %u\n", sinit_mle_data->version);
    printk("\t bios_acm_id: \n\t");
        print_hash(sinit_mle_data->bios_acm_id);
    printk("\t edx_senter_flags: 0x%08x\n",
           sinit_mle_data->edx_senter_flags);
    printk("\t mseg_valid: 0x%Lx\n", sinit_mle_data->mseg_valid);
    printk("\t sinit_hash:\n\t"); print_hash(sinit_mle_data->sinit_hash);
    printk("\t mle_hash:\n\t"); print_hash(sinit_mle_data->mle_hash);
    printk("\t stm_hash:\n\t"); print_hash(sinit_mle_data->stm_hash);
    printk("\t lcp_policy_hash:\n\t");
        print_hash(sinit_mle_data->lcp_policy_hash);
    printk("\t lcp_policy_control: 0x%08x\n",
           sinit_mle_data->lcp_policy_control);
    printk("\t rlp_wakeup_addr: 0x%x\n", sinit_mle_data->rlp_wakeup_addr);
    printk("\t num_mdrs: %u\n", sinit_mle_data->num_mdrs);
    printk("\t mdrs_off: 0x%x\n", sinit_mle_data->mdrs_off);
    printk("\t num_vtd_dmars: %u\n", sinit_mle_data->num_vtd_dmars);
    printk("\t vtd_dmars_off: 0x%x\n", sinit_mle_data->vtd_dmars_off);
    print_sinit_mdrs((sinit_mdr_t *)
                     (((void *)sinit_mle_data - sizeof(uint64_t)) +
                      sinit_mle_data->mdrs_off), sinit_mle_data->num_mdrs);
}

static bool verify_sinit_mle_data(txt_heap_t *txt_heap)
{
    uint64_t size, heap_size;
    sinit_mle_data_t *sinit_mle_data;

    /* check size */
    heap_size = read_priv_config_reg(TXTCR_HEAP_SIZE);
    size = get_sinit_mle_data_size(txt_heap);
    if ( size == 0 ) {
        printk("SINIT to MLE data size is 0\n");
        return false;
    }
    if ( size > heap_size ) {
        printk("SINIT to MLE data size is larger than heap size\n"
               "(%Lx, heap size=%Lx)\n", size, heap_size);
        return false;
    }

    sinit_mle_data = get_sinit_mle_data_start(txt_heap);

    /* check version */
    sinit_mle_data = get_sinit_mle_data_start(txt_heap);
    if ( sinit_mle_data->version < 6 ) {
        printk("unsupported SINIT to MLE data version (%u)\n",
               sinit_mle_data->version);
        return false;
    }
    else if ( sinit_mle_data->version > 6 ) {
        printk("unsupported SINIT to MLE data version (%u)\n",
               sinit_mle_data->version);
    }

    /* this data is generated by SINIT and so is implicitly trustworthy, */
    /* so we don't need to validate it's fields */

    print_sinit_mle_data(sinit_mle_data);

    return true;
}

bool verify_txt_heap(txt_heap_t *txt_heap, bool bios_data_only)
{
    uint64_t size1, size2, size3, size4;

    /* verify BIOS to OS data */
    if ( !verify_bios_data(txt_heap) )
        return false;

    if ( bios_data_only )
        return true;

    /* check that total size is within the heap */
    size1 = get_bios_data_size(txt_heap);
    size2 = get_os_mle_data_size(txt_heap);
    size3 = get_os_sinit_data_size(txt_heap);
    size4 = get_sinit_mle_data_size(txt_heap);
    if ( (size1 + size2 + size3 + size4) >
         read_priv_config_reg(TXTCR_HEAP_SIZE) ) {
        printk("TXT heap data sizes (%Lx, %Lx, %Lx, %Lx) are larger than\n"
               "heap total size (%Lx)\n", size1, size2, size3, size4,
               read_priv_config_reg(TXTCR_HEAP_SIZE));
        return false;
    }

    /* verify OS to MLE data */
    if ( !verify_os_mle_data(txt_heap) )
        return false;

    /* verify OS to SINIT data */
    if ( !verify_os_sinit_data(txt_heap) )
        return false;

    /* verify SINIT to MLE data */
    if ( !verify_sinit_mle_data(txt_heap) )
        return false;

    return true;
}


/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
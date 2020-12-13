/**
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srslte/common/test_common.h"
#include "srslte/phy/ch_estimation/dmrs_pdsch.h"
#include "srslte/phy/ue/ue_dl_nr_data.h"
#include "srslte/srslte.h"
#include <complex.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

static srslte_carrier_nr_t carrier = {
    0,  // cell_id
    0,  // numerology
    50, // nof_prb
    0,  // start
    1   // max_mimo_layers
};

typedef struct {
  srslte_pdsch_mapping_type_t   mapping_type;
  srslte_dmrs_pdsch_typeA_pos_t typeA_pos;
  srslte_dmrs_pdsch_len_t       max_length;
  srslte_dmrs_pdsch_add_pos_t   additional_pos;
  srslte_dmrs_pdsch_type_t      type;
  uint32_t                      symbol_idx[SRSLTE_DMRS_PDSCH_MAX_SYMBOLS];
  uint32_t                      nof_symbols;
  uint32_t                      sc_idx[SRSLTE_NRE];
  uint32_t                      nof_sc;
} golden_t;

// Golden values extracted from https://www.sharetechnote.com/html/5G/5G_PDSCH_DMRS.html
static const golden_t gold[] = {{.mapping_type   = srslte_pdsch_mapping_type_A,
                                 .typeA_pos      = srslte_dmrs_pdsch_typeA_pos_2,
                                 .max_length     = srslte_dmrs_pdsch_len_1,
                                 .additional_pos = srslte_dmrs_pdsch_add_pos_0,
                                 .type           = srslte_dmrs_pdsch_type_2,
                                 .nof_symbols    = 1,
                                 .symbol_idx     = {2},
                                 .nof_sc         = 4,
                                 .sc_idx         = {0, 1, 6, 7}},
                                {.mapping_type   = srslte_pdsch_mapping_type_A,
                                 .typeA_pos      = srslte_dmrs_pdsch_typeA_pos_3,
                                 .max_length     = srslte_dmrs_pdsch_len_1,
                                 .additional_pos = srslte_dmrs_pdsch_add_pos_0,
                                 .type           = srslte_dmrs_pdsch_type_2,
                                 .nof_symbols    = 1,
                                 .symbol_idx     = {3},
                                 .nof_sc         = 4,
                                 .sc_idx         = {0, 1, 6, 7}},
                                {.mapping_type   = srslte_pdsch_mapping_type_A,
                                 .typeA_pos      = srslte_dmrs_pdsch_typeA_pos_2,
                                 .max_length     = srslte_dmrs_pdsch_len_2,
                                 .additional_pos = srslte_dmrs_pdsch_add_pos_0,
                                 .type           = srslte_dmrs_pdsch_type_2,
                                 .nof_symbols    = 2,
                                 .symbol_idx     = {2, 3},
                                 .nof_sc         = 4,
                                 .sc_idx         = {0, 1, 6, 7}},
                                {.mapping_type   = srslte_pdsch_mapping_type_A,
                                 .typeA_pos      = srslte_dmrs_pdsch_typeA_pos_2,
                                 .max_length     = srslte_dmrs_pdsch_len_1,
                                 .additional_pos = srslte_dmrs_pdsch_add_pos_1,
                                 .type           = srslte_dmrs_pdsch_type_2,
                                 .nof_symbols    = 2,
                                 .symbol_idx     = {2, 11},
                                 .nof_sc         = 4,
                                 .sc_idx         = {0, 1, 6, 7}},
                                {.mapping_type   = srslte_pdsch_mapping_type_A,
                                 .typeA_pos      = srslte_dmrs_pdsch_typeA_pos_2,
                                 .max_length     = srslte_dmrs_pdsch_len_1,
                                 .additional_pos = srslte_dmrs_pdsch_add_pos_2,
                                 .type           = srslte_dmrs_pdsch_type_2,
                                 .nof_symbols    = 3,
                                 .symbol_idx     = {2, 7, 11},
                                 .nof_sc         = 4,
                                 .sc_idx         = {0, 1, 6, 7}},
                                {.mapping_type   = srslte_pdsch_mapping_type_A,
                                 .typeA_pos      = srslte_dmrs_pdsch_typeA_pos_2,
                                 .max_length     = srslte_dmrs_pdsch_len_1,
                                 .additional_pos = srslte_dmrs_pdsch_add_pos_3,
                                 .type           = srslte_dmrs_pdsch_type_2,
                                 .nof_symbols    = 4,
                                 .symbol_idx     = {2, 5, 8, 11},
                                 .nof_sc         = 4,
                                 .sc_idx         = {0, 1, 6, 7}},
                                {.mapping_type   = srslte_pdsch_mapping_type_A,
                                 .typeA_pos      = srslte_dmrs_pdsch_typeA_pos_2,
                                 .max_length     = srslte_dmrs_pdsch_len_1,
                                 .additional_pos = srslte_dmrs_pdsch_add_pos_0,
                                 .type           = srslte_dmrs_pdsch_type_1,
                                 .nof_symbols    = 1,
                                 .symbol_idx     = {2},
                                 .nof_sc         = 6,
                                 .sc_idx         = {0, 2, 4, 6, 8, 10}},
                                {.mapping_type   = srslte_pdsch_mapping_type_A,
                                 .typeA_pos      = srslte_dmrs_pdsch_typeA_pos_2,
                                 .max_length     = srslte_dmrs_pdsch_len_2,
                                 .additional_pos = srslte_dmrs_pdsch_add_pos_0,
                                 .type           = srslte_dmrs_pdsch_type_1,
                                 .nof_symbols    = 2,
                                 .symbol_idx     = {2, 3},
                                 .nof_sc         = 6,
                                 .sc_idx         = {0, 2, 4, 6, 8, 10}},
                                {.mapping_type   = srslte_pdsch_mapping_type_A,
                                 .typeA_pos      = srslte_dmrs_pdsch_typeA_pos_2,
                                 .max_length     = srslte_dmrs_pdsch_len_1,
                                 .additional_pos = srslte_dmrs_pdsch_add_pos_3,
                                 .type           = srslte_dmrs_pdsch_type_1,
                                 .nof_symbols    = 4,
                                 .symbol_idx     = {2, 5, 8, 11},
                                 .nof_sc         = 6,
                                 .sc_idx         = {0, 2, 4, 6, 8, 10}},
                                {}};

static void usage(char* prog)
{
  printf("Usage: %s [recov]\n", prog);

  printf("\t-r nof_prb [Default %d]\n", carrier.nof_prb);

  printf("\t-c cell_id [Default %d]\n", carrier.id);

  printf("\t-v increase verbosity\n");
}

static void parse_args(int argc, char** argv)
{
  int opt;
  while ((opt = getopt(argc, argv, "rcov")) != -1) {
    switch (opt) {
      case 'r':
        carrier.nof_prb = (uint32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'c':
        carrier.id = (uint32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'v':
        srslte_verbose++;
        break;
      default:
        usage(argv[0]);
        exit(-1);
    }
  }
}

static int assert_cfg(const srslte_pdsch_cfg_nr_t* pdsch_cfg, const srslte_pdsch_grant_nr_t* grant)
{
  for (uint32_t i = 0; gold[i].nof_sc != 0; i++) {
    // Gold examples are done for more than 12 symbols
    if (grant->L <= 12) {
      continue;
    }

    if (grant->mapping != gold[i].mapping_type) {
      continue;
    }

    if (pdsch_cfg->dmrs_cfg_typeA.typeA_pos != gold[i].typeA_pos) {
      continue;
    }

    if (pdsch_cfg->dmrs_cfg_typeA.additional_pos != gold[i].additional_pos) {
      continue;
    }

    if (pdsch_cfg->dmrs_cfg_typeA.length != gold[i].max_length) {
      continue;
    }

    if (pdsch_cfg->dmrs_cfg_typeA.type != gold[i].type) {
      continue;
    }

    uint32_t symbols[SRSLTE_DMRS_PDSCH_MAX_SYMBOLS] = {};
    int      nof_symbols                            = srslte_dmrs_pdsch_get_symbols_idx(pdsch_cfg, grant, symbols);

    TESTASSERT(nof_symbols == gold[i].nof_symbols);

    for (uint32_t j = 0; j < gold[i].nof_symbols; j++) {
      TESTASSERT(symbols[j] == gold[i].symbol_idx[j]);
    }

    uint32_t sc[SRSLTE_NRE] = {};
    srslte_dmrs_pdsch_get_sc_idx(&pdsch_cfg->dmrs_cfg_typeA, SRSLTE_NRE, sc);

    for (uint32_t j = 0; j < gold[i].nof_sc; j++) {
      TESTASSERT(sc[j] == gold[i].sc_idx[j]);
    }

    return SRSLTE_SUCCESS;
  }

  return SRSLTE_SUCCESS;
}

static int run_test(srslte_dmrs_pdsch_t*           dmrs_pdsch,
                    const srslte_pdsch_cfg_nr_t*   pdsch_cfg,
                    const srslte_pdsch_grant_nr_t* grant,
                    cf_t*                          sf_symbols,
                    srslte_chest_dl_res_t*         chest_res)
{
  TESTASSERT(assert_cfg(pdsch_cfg, grant) == SRSLTE_SUCCESS);

  srslte_dl_slot_cfg_t slot_cfg = {};
  for (slot_cfg.idx = 0; slot_cfg.idx < SRSLTE_NSLOTS_PER_FRAME_NR(dmrs_pdsch->carrier.numerology); slot_cfg.idx++) {
    srslte_dmrs_pdsch_put_sf(dmrs_pdsch, &slot_cfg, pdsch_cfg, grant, sf_symbols);

    srslte_dmrs_pdsch_estimate(dmrs_pdsch, &slot_cfg, pdsch_cfg, grant, sf_symbols, chest_res);

    float mse = 0.0f;
    for (uint32_t i = 0; i < chest_res->nof_re; i++) {
      cf_t err = chest_res->ce[0][0][i] - 1.0f;
      mse += cabsf(err);
    }
    mse /= (float)chest_res->nof_re;

    TESTASSERT(!isnan(mse));
    TESTASSERT(mse < 1e-6f);
  }

  return SRSLTE_SUCCESS;
}

int main(int argc, char** argv)
{
  int ret = SRSLTE_ERROR;

  parse_args(argc, argv);

  srslte_dmrs_pdsch_t     dmrs_pdsch   = {};
  srslte_pdsch_cfg_nr_t   pdsch_cfg    = {};
  srslte_pdsch_grant_nr_t grant        = {};
  srslte_chest_dl_res_t   chest_dl_res = {};

  uint32_t nof_re     = carrier.nof_prb * SRSLTE_NRE * SRSLTE_NOF_SLOTS_PER_SF * SRSLTE_MAX_NSYMB;
  cf_t*    sf_symbols = srslte_vec_cf_malloc(nof_re);

  uint32_t test_counter = 0;
  uint32_t test_passed  = 0;

  // Initialise object DMRS for PDSCH
  if (srslte_dmrs_pdsch_init(&dmrs_pdsch, true) != SRSLTE_SUCCESS) {
    ERROR("Init\n");
    goto clean_exit;
  }

  // Set carrier configuration
  if (srslte_dmrs_pdsch_set_carrier(&dmrs_pdsch, &carrier) != SRSLTE_SUCCESS) {
    ERROR("Setting carrier\n");
    goto clean_exit;
  }

  if (srslte_chest_dl_res_init(&chest_dl_res, carrier.nof_prb) != SRSLTE_SUCCESS) {
    ERROR("Initiating channel estimation  result\n");
    goto clean_exit;
  }

  // For each DCI m param
  for (uint32_t m = 0; m < 16; m++) {
    srslte_dmrs_pdsch_type_t type_begin = srslte_dmrs_pdsch_type_1;
    srslte_dmrs_pdsch_type_t type_end   = srslte_dmrs_pdsch_type_2;

    for (pdsch_cfg.dmrs_cfg_typeA.type = type_begin; pdsch_cfg.dmrs_cfg_typeA.type <= type_end;
         pdsch_cfg.dmrs_cfg_typeA.type++) {
      srslte_dmrs_pdsch_typeA_pos_t typeA_pos_begin = srslte_dmrs_pdsch_typeA_pos_2;
      srslte_dmrs_pdsch_typeA_pos_t typeA_pos_end   = srslte_dmrs_pdsch_typeA_pos_3;

      for (pdsch_cfg.dmrs_cfg_typeA.typeA_pos = typeA_pos_begin; pdsch_cfg.dmrs_cfg_typeA.typeA_pos <= typeA_pos_end;
           pdsch_cfg.dmrs_cfg_typeA.typeA_pos++) {
        srslte_dmrs_pdsch_add_pos_t add_pos_begin = srslte_dmrs_pdsch_add_pos_2;
        srslte_dmrs_pdsch_add_pos_t add_pos_end   = srslte_dmrs_pdsch_add_pos_3;

        if (pdsch_cfg.dmrs_cfg_typeA.typeA_pos == srslte_dmrs_pdsch_typeA_pos_3) {
          add_pos_end = srslte_dmrs_pdsch_add_pos_1;
        }

        for (pdsch_cfg.dmrs_cfg_typeA.additional_pos = add_pos_begin;
             pdsch_cfg.dmrs_cfg_typeA.additional_pos <= add_pos_end;
             pdsch_cfg.dmrs_cfg_typeA.additional_pos++) {

          srslte_dmrs_pdsch_len_t max_len_begin = srslte_dmrs_pdsch_len_1;
          srslte_dmrs_pdsch_len_t max_len_end   = srslte_dmrs_pdsch_len_2;

          for (pdsch_cfg.dmrs_cfg_typeA.length = max_len_begin; pdsch_cfg.dmrs_cfg_typeA.length <= max_len_end;
               pdsch_cfg.dmrs_cfg_typeA.length++) {

            for (uint32_t bw = 1; bw <= carrier.nof_prb; bw++) {

              for (uint32_t i = 0; i < carrier.nof_prb; i++) {
                grant.prb_idx[i] = (i < bw);
              }

              // Load default type A grant
              srslte_ue_dl_nr_pdsch_time_resource_default_A(0, pdsch_cfg.dmrs_cfg_typeA.typeA_pos, &grant);

              // Copy configuration
              pdsch_cfg.dmrs_cfg_typeB = pdsch_cfg.dmrs_cfg_typeA;

              int n = run_test(&dmrs_pdsch, &pdsch_cfg, &grant, sf_symbols, &chest_dl_res);

              if (n == SRSLTE_SUCCESS) {
                test_passed++;
              } else {
                const srslte_pdsch_dmrs_cfg_t* dmrs_cfg = grant.mapping == srslte_pdsch_mapping_type_A
                                                              ? &pdsch_cfg.dmrs_cfg_typeA
                                                              : &pdsch_cfg.dmrs_cfg_typeB;

                char str[64] = {};
                srslte_dmrs_pdsch_cfg_to_str(dmrs_cfg, str, 64);

                ERROR("Test %d failed. %s.\n", test_counter, str);
              }

              test_counter++;
            }
          }
        }
      }
    }
  }

clean_exit:

  if (sf_symbols) {
    free(sf_symbols);
  }
  srslte_chest_dl_res_free(&chest_dl_res);
  srslte_dmrs_pdsch_free(&dmrs_pdsch);

  ret = test_passed == test_counter ? SRSLTE_SUCCESS : SRSLTE_ERROR;
  printf("%s, %d of %d test passed successfully.\n", ret ? "Failed" : "Passed", test_passed, test_counter);

  return ret;
}

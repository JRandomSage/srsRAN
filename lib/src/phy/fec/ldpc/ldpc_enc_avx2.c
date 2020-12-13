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

/*!
 * \file ldpc_enc_avx2.c
 * \brief Definition of the LDPC encoder inner functions (AVX2 version, small lifting size).
 * \author David Gregoratti
 * \date 2020
 *
 * \copyright Software Radio Systems Limited
 *
 */

#include <stdint.h>

#include "../utils_avx2.h"
#include "ldpc_enc_all.h"
#include "srslte/phy/fec/ldpc/base_graph.h"
#include "srslte/phy/fec/ldpc/ldpc_encoder.h"
#include "srslte/phy/utils/debug.h"
#include "srslte/phy/utils/vector.h"

#ifdef LV_HAVE_AVX2

#include <immintrin.h>

#include "ldpc_avx2_consts.h"

/*!
 * \brief Represents a node of the base factor graph.
 */
typedef union bg_node_t {
  uint8_t c[SRSLTE_AVX2_B_SIZE]; /*!< Each base node may contain up to \ref SRSLTE_AVX2_B_SIZE lifted nodes. */
  __m256i v;                     /*!< All the lifted nodes of the current base node as a 256-bit line. */
} bg_node_t;

/*!
 * \brief Inner registers for the optimized LDPC encoder.
 */
struct ldpc_enc_avx2 {
  bg_node_t* codeword; /*!< \brief Contains the entire codeword, before puncturing. */
  __m256i*   aux;      /*!< \brief Auxiliary register. */
};

/*!
 * Rotate the content of an __m256i vector (first input) towards the left by
 * the number of chars specified by the second input (i.e., the \b imm * 8 least
 * significant bits become the \b imm * 8 most significant bits).
 * \param[in]  a    Vector to circularly shift.
 * \param[in]  imm  The shift order in chars.
 * \return          The shifted vector.
 */
static __m256i _mm256_rotatelli_si256(__m256i a, int imm);

/*!
 * Rotate the content of an __m256i vector (first input) towards the right by
 * the number of chars specified by the second input (i.e., the \b imm * 8 most
 * significant bits become the \b imm * 8 least significant bits).
 * \param[in]  a    Vector to circularly shift.
 * \param[in]  imm  The shift order in chars.
 * \return          The shifted vector.
 */
static __m256i _mm256_rotaterli_si256(__m256i a, int imm);

/*!
 * Rotate the contents of a node towards the left by \b imm chars, that is the
 * \b imm * 8 most significant bits become the least significant ones.
 * \param[in]  a    The node to rotate.
 * \param[in]  imm  The order of the rotation in number of chars.
 * \param[in]  ls   The size of the node (lifting size).
 * \return     The rotated node.
 */
static __m256i rotate_node_left(__m256i a, int imm, uint16_t ls);

/*!
 * Rotate the contents of a node towards the right by \b imm chars, that is the
 * \b imm * 8 most significant bits become the least significant ones.
 * \param[in]  a    The node to rotate.
 * \param[in]  imm  The order of the rotation in number of chars.
 * \param[in]  ls   The size of the node (lifting size).
 * \return     The rotated node.
 */
static __m256i rotate_node_right(__m256i a, int imm, uint16_t ls);

void* create_ldpc_enc_avx2(srslte_ldpc_encoder_t* q)
{
  struct ldpc_enc_avx2* vp = NULL;

  if ((vp = malloc(sizeof(struct ldpc_enc_avx2))) == NULL) {
    return NULL;
  }

  if ((vp->codeword = srslte_vec_malloc(q->bgN * sizeof(bg_node_t))) == NULL) {
    free(vp);
    return NULL;
  }

  if ((vp->aux = srslte_vec_malloc(q->bgM * sizeof(__m256i))) == NULL) {
    free(vp->codeword);
    free(vp);
    return NULL;
  }

  return vp;
}

void delete_ldpc_enc_avx2(void* p)
{
  struct ldpc_enc_avx2* vp = p;

  if (vp != NULL) {
    free(vp->aux);
    free(vp->codeword);
    free(vp);
  }
}

int load_avx2(void* p, const uint8_t* input, const uint8_t msg_len, const uint8_t cdwd_len, const uint16_t ls)
{
  struct ldpc_enc_avx2* vp = p;

  if (p == NULL) {
    return -1;
  }

  int i = 0;
  int k = 0;
  for (; i < msg_len; i++) {
    for (k = 0; k < ls; k++) {
      vp->codeword[i].c[k] = input[i * ls + k];
    }
    bzero(&(vp->codeword[i].c[k]), (SRSLTE_AVX2_B_SIZE - k) * sizeof(uint8_t));
  }

  bzero(vp->codeword + i, (cdwd_len - msg_len) * sizeof(__m256i));

  return 0;
}

int return_codeword_avx2(void* p, uint8_t* output, const uint8_t cdwd_len, const uint16_t ls)
{
  struct ldpc_enc_avx2* vp = p;

  if (p == NULL) {
    return -1;
  }

  int k = 0;
  for (int i = 0; i < cdwd_len - 2; i++) {
    for (k = 0; k < ls; k++) {
      output[i * ls + k] = vp->codeword[i + 2].c[k];
    }
  }
  return 0;
}

void encode_ext_region_avx2(srslte_ldpc_encoder_t* q, uint8_t n_layers)
{
  struct ldpc_enc_avx2* vp = q->ptr;

  int m    = 0;
  int skip = 0;
  int k    = 0;

  uint16_t* this_shift = NULL;

  __m256i tmp_epi8;

  // Encode the extended region. In case of puncturing or IR-HARQ, we could focus on
  // specific check nodes instead of processing all of them from m = 4 to m = M - 1.
  for (m = 4; m < n_layers; m++) {
    skip = q->bgK + m;

    // the systematic part has already been computed
    vp->codeword[skip].v = vp->aux[m];

    // sum the contribution due to the high-rate region, with the proper circular shifts
    for (k = 0; k < 4; k++) {
      this_shift = q->pcm + q->bgK + k + m * q->bgN;
      if (*this_shift != NO_CNCT) {
        tmp_epi8             = rotate_node_right(vp->codeword[q->bgK + k].v, *this_shift, q->ls);
        vp->codeword[skip].v = _mm256_xor_si256(vp->codeword[skip].v, tmp_epi8);
      }
    }
  }
}

void preprocess_systematic_bits_avx2(srslte_ldpc_encoder_t* q)
{
  struct ldpc_enc_avx2* vp = q->ptr;

  int       N   = q->bgN;
  int       K   = q->bgK;
  int       M   = q->bgM;
  int       ls  = q->ls;
  uint16_t* pcm = q->pcm;

  int       k          = 0;
  int       m          = 0;
  uint16_t* this_shift = NULL;

  __m256i tmp_epi8;

  bzero(vp->aux, M * sizeof(__m256i));

  // split the input message into K chunks of ls bits each and, for all chunks
  for (k = 0; k < K; k++) {
    // for all check nodes
    // NB: if looking for performance you can do the following loop only over the high-rate
    // region of the PCM (m=0,1,2,3) and over the check nodes that result in a transmitted
    // coded bit after puncturing or IR-HARQ (see Deliverable D1 Section 3.4).
    for (m = 0; m < M; m++) {
      // entry of pcm corresponding to the current input chunk and the current check node
      this_shift = pcm + k + m * N;

      // xor array aux[m] with a circularly shifted version of the current input chunk, unless
      // the current check node and variable node are not connected.
      if (*this_shift != NO_CNCT) {
        tmp_epi8   = rotate_node_right(vp->codeword[k].v, *this_shift, ls);
        tmp_epi8   = _mm256_and_si256(tmp_epi8, one_epi8);
        vp->aux[m] = _mm256_xor_si256(vp->aux[m], tmp_epi8);
      }
    }
  }
}

void encode_high_rate_case1_avx2(void* o)
{
  srslte_ldpc_encoder_t* q  = o;
  struct ldpc_enc_avx2*  vp = q->ptr;

  int ls = q->ls;

  int skip0 = q->bgK;
  int skip1 = q->bgK + 1;
  int skip2 = q->bgK + 2;
  int skip3 = q->bgK + 3;

  // first chunk of parity bits
  vp->codeword[skip0].v = _mm256_xor_si256(vp->aux[0], vp->aux[1]);
  vp->codeword[skip0].v = _mm256_xor_si256(vp->codeword[skip0].v, vp->aux[2]);
  vp->codeword[skip0].v = _mm256_xor_si256(vp->codeword[skip0].v, vp->aux[3]);

  __m256i tmp_epi8 = rotate_node_right(vp->codeword[skip0].v, 1, ls);
  // second chunk of parity bits
  vp->codeword[skip1].v = _mm256_xor_si256(vp->aux[0], tmp_epi8);
  // fourth chunk of parity bits
  vp->codeword[skip3].v = _mm256_xor_si256(vp->aux[3], tmp_epi8);
  // third chunk of parity bits
  vp->codeword[skip2].v = _mm256_xor_si256(vp->aux[2], vp->codeword[skip3].v);
}

void encode_high_rate_case2_avx2(void* o)
{
  srslte_ldpc_encoder_t* q  = o;
  struct ldpc_enc_avx2*  vp = q->ptr;

  int ls = q->ls;

  int skip0 = q->bgK;
  int skip1 = q->bgK + 1;
  int skip2 = q->bgK + 2;
  int skip3 = q->bgK + 3;

  // first chunk of parity bits
  __m256i tmp_epi8      = _mm256_xor_si256(vp->aux[0], vp->aux[1]);
  tmp_epi8              = _mm256_xor_si256(tmp_epi8, vp->aux[2]);
  tmp_epi8              = _mm256_xor_si256(tmp_epi8, vp->aux[3]);
  vp->codeword[skip0].v = rotate_node_left(tmp_epi8, 105 % ls, ls);

  // second chunk of parity bits
  vp->codeword[skip1].v = _mm256_xor_si256(vp->aux[0], vp->codeword[skip0].v);
  // fourth chunk of parity bits
  vp->codeword[skip3].v = _mm256_xor_si256(vp->aux[3], vp->codeword[skip0].v);
  // third chunk of parity bits
  vp->codeword[skip2].v = _mm256_xor_si256(vp->aux[2], vp->codeword[skip3].v);
}

void encode_high_rate_case3_avx2(void* o)
{
  srslte_ldpc_encoder_t* q  = o;
  struct ldpc_enc_avx2*  vp = q->ptr;

  int ls = q->ls;

  int skip0 = q->bgK;
  int skip1 = q->bgK + 1;
  int skip2 = q->bgK + 2;
  int skip3 = q->bgK + 3;

  // first chunk of parity bits
  __m256i tmp_epi8      = _mm256_xor_si256(vp->aux[0], vp->aux[1]);
  tmp_epi8              = _mm256_xor_si256(tmp_epi8, vp->aux[2]);
  tmp_epi8              = _mm256_xor_si256(tmp_epi8, vp->aux[3]);
  vp->codeword[skip0].v = rotate_node_left(tmp_epi8, 1, ls);

  // second chunk of parity bits
  vp->codeword[skip1].v = _mm256_xor_si256(vp->aux[0], vp->codeword[skip0].v);
  // third chunk of parity bits
  vp->codeword[skip2].v = _mm256_xor_si256(vp->aux[1], vp->codeword[skip1].v);
  // fourth chunk of parity bits
  vp->codeword[skip3].v = _mm256_xor_si256(vp->aux[3], vp->codeword[skip0].v);
}

void encode_high_rate_case4_avx2(void* o)
{
  srslte_ldpc_encoder_t* q  = o;
  struct ldpc_enc_avx2*  vp = q->ptr;

  int ls = q->ls;

  int skip0 = q->bgK;
  int skip1 = q->bgK + 1;
  int skip2 = q->bgK + 2;
  int skip3 = q->bgK + 3;

  // first chunk of parity bits
  vp->codeword[skip0].v = _mm256_xor_si256(vp->aux[0], vp->aux[1]);
  vp->codeword[skip0].v = _mm256_xor_si256(vp->codeword[skip0].v, vp->aux[2]);
  vp->codeword[skip0].v = _mm256_xor_si256(vp->codeword[skip0].v, vp->aux[3]);

  __m256i tmp_epi8 = rotate_node_right(vp->codeword[skip0].v, 1, ls);
  // second chunk of parity bits
  vp->codeword[skip1].v = _mm256_xor_si256(vp->aux[0], tmp_epi8);
  // third chunk of parity bits
  vp->codeword[skip2].v = _mm256_xor_si256(vp->aux[1], vp->codeword[skip1].v);
  // fourth chunk of parity bits
  vp->codeword[skip3].v = _mm256_xor_si256(vp->aux[3], tmp_epi8);
}

static __m256i _mm256_rotatelli_si256(__m256i a, int imm)
{
  __m256i rotated_block_a[4];

  // rotate left a as if made of 64-bit blocks: rotated_block_a[i] contains the
  // rotation by i units
  rotated_block_a[0] = a;                                // blocks 0 - 1 - 2 - 3
  rotated_block_a[1] = _mm256_permute4x64_epi64(a, 147); // 3 - 0 - 1 - 2
  rotated_block_a[2] = _mm256_permute4x64_epi64(a, 78);  // 2 - 3 - 0 - 1
  rotated_block_a[3] = _mm256_permute4x64_epi64(a, 57);  // 1 - 2 - 3 - 0

  // rotation index we are interested in
  int step1 = imm / 8;
  // small-step rotation
  int left = imm % 8;
  // next block, for carry-over
  int step2 = (step1 + 1) % 4;

  // shift right each block
  __m256i reg1 = _mm256_slli_epi64(rotated_block_a[step1], left * 8);
  // carry-over from the next block
  __m256i reg2 = _mm256_srli_epi64(rotated_block_a[step2], (8 - left) * 8);

  return _mm256_xor_si256(reg1, reg2);
}

static __m256i _mm256_rotaterli_si256(__m256i a, int imm)
{
  __m256i rotated_block_a[4];

  // rotate right a as if made of 64-bit blocks: rotated_block_a[i] contains the
  // rotation by i units
  rotated_block_a[0] = a;                                // blocks 0 - 1 - 2 - 3
  rotated_block_a[1] = _mm256_permute4x64_epi64(a, 57);  // 1 - 2 - 3 - 0
  rotated_block_a[2] = _mm256_permute4x64_epi64(a, 78);  // 2 - 3 - 0 - 1
  rotated_block_a[3] = _mm256_permute4x64_epi64(a, 147); // 3 - 0 - 1 - 2

  // rotation index we are interested in
  int step1 = imm / 8;
  // small-step rotation
  int left = imm % 8;
  // next block, for carry-over
  int step2 = (step1 + 1) % 4;

  // shift right each block
  __m256i reg1 = _mm256_srli_epi64(rotated_block_a[step1], left * 8);
  // carry-over from the next block
  __m256i reg2 = _mm256_slli_epi64(rotated_block_a[step2], (8 - left) * 8);

  return _mm256_xor_si256(reg1, reg2);
}

static __m256i rotate_node_left(__m256i a, int imm, uint16_t ls)
{
  if (imm == 0) {
    return a;
  }
  __m256i step1 = _mm256_rotatelli_si256(a, imm);
  if (ls == SRSLTE_AVX2_B_SIZE) {
    return step1;
  }

  __m256i step2 = _mm256_rotaterli_si256(a, ls - imm);

  step1 = _mm256_and_si256(step1, mask_most_epi8[imm]);
  step2 = _mm256_and_si256(step2, mask_least_epi8[imm]);

  step1 = _mm256_xor_si256(step1, step2);

  return step1;
  ;
}

static __m256i rotate_node_right(__m256i a, int imm, uint16_t ls)
{
  if (imm == 0) {
    return a;
  }
  __m256i step1 = _mm256_rotaterli_si256(a, imm);
  if (ls == SRSLTE_AVX2_B_SIZE) {
    return step1;
  }

  __m256i step2 = _mm256_rotatelli_si256(a, ls - imm);

  step1 = _mm256_and_si256(step1, mask_least_epi8[ls - imm]);
  step2 = _mm256_and_si256(step2, mask_most_epi8[ls - imm]);

  step1 = _mm256_xor_si256(step1, step2);

  return step1;
}

#endif // LV_HAVE_AVX2

/*
 * Copyright(c) 2019 Netflix, Inc.
 * SPDX - License - Identifier: BSD - 2 - Clause - Patent
 */

/******************************************************************************
 * @file pixel_proj_err_test.cc
 *
 * @brief Unit test of pixel projection error:
 *
 * - av1_lowbd_pixel_proj_error_avx2
 * - av1_highbd_pixel_proj_error_avx2
 *
 * @author Cidana-Edmond
 *
 ******************************************************************************/

#include "gtest/gtest.h"
#include "aom_dsp_rtcd.h"
#include "EbDefinitions.h"
#include "EbUtility.h"
#include "EbRestoration.h"
#include "random.h"
#include "util.h"

#define MAX_DATA_BLOCK 384

namespace {
using std::make_tuple;
using svt_av1_test_tool::SVTRandom;

static const int min_test_times = 10;

typedef int64_t (*PixelProjFcn)(const uint8_t* src8, int32_t width,
                                int32_t height, int32_t src_stride,
                                const uint8_t* dat8, int32_t dat_stride,
                                int32_t* flt0, int32_t flt0_stride,
                                int32_t* flt1, int32_t flt1_stride,
                                int32_t xq[2], const SgrParamsType* params);

typedef std::tuple<const PixelProjFcn, const PixelProjFcn>
    PixelProjErrorTestParam;

/**
 * @brief Unit test for pixel projection error:
 * - av1_lowbd_pixel_proj_error_avx2
 * - av1_highbd_pixel_proj_error_avx2
 *
 * Test strategy:
 * Verify this assembly code by comparing with reference c implementation.
 * Feed the same random data and check test output and reference output.
 * Define a template class to handle the common process, and
 * declare sub class to handle different bitdepth.
 *
 * Expected result:
 * Output from assemble functions should be the same with output from c.
 *
 * Test coverage:
 * Test cases:
 * input value: Fill with random values
 * test mode: fixed block size, random block size and extrenely data check
 *
 */

template <typename Sample>
class PixelProjErrorTest
    : public ::testing::TestWithParam<PixelProjErrorTestParam> {
  public:
    PixelProjErrorTest()
        : rnd8_(8, false),
          rnd16_(16, false),
          rnd15s_(15, true),
          rnd_bk_size_(1, MAX_DATA_BLOCK) {
        tst_func_ = TEST_GET_PARAM(0);
        ref_func_ = TEST_GET_PARAM(1);
        src_ = nullptr;
        dgd_ = nullptr;
        flt0_ = nullptr;
        flt1_ = nullptr;
    }

    virtual void SetUp() {
        src_ = (Sample*)(aom_malloc(MAX_DATA_BLOCK * MAX_DATA_BLOCK *
                                    sizeof(*src_)));
        ASSERT_NE(src_, nullptr);
        dgd_ = (Sample*)(aom_malloc(MAX_DATA_BLOCK * MAX_DATA_BLOCK *
                                    sizeof(*dgd_)));
        ASSERT_NE(dgd_, nullptr);
        flt0_ = (int32_t*)(aom_malloc(MAX_DATA_BLOCK * MAX_DATA_BLOCK *
                                      sizeof(*flt0_)));
        ASSERT_NE(flt0_, nullptr);
        flt1_ = (int32_t*)(aom_malloc(MAX_DATA_BLOCK * MAX_DATA_BLOCK *
                                      sizeof(*flt1_)));
        ASSERT_NE(flt1_, nullptr);
    }

    virtual void TearDown() {
        aom_free(src_);
        aom_free(dgd_);
        aom_free(flt0_);
        aom_free(flt1_);
    }

    virtual void prepare_data() = 0;
    virtual void prepare_extreme_data() = 0;
    void run_and_check_data(const int index, const int fixed_size) {
        const int dgd_stride = MAX_DATA_BLOCK;
        const int src_stride = MAX_DATA_BLOCK;
        const int flt0_stride = MAX_DATA_BLOCK;
        const int flt1_stride = MAX_DATA_BLOCK;
        int h_end = fixed_size;
        int v_end = fixed_size;
        bool is_fixed_size = true;
        if (fixed_size == 0) {
            h_end = rnd_bk_size_.random();
            v_end = rnd_bk_size_.random();
            is_fixed_size = false;
        }

        int xq[2];
        xq[0] = rnd8_.random() % (1 << SGRPROJ_PRJ_BITS);
        xq[1] = rnd8_.random() % (1 << SGRPROJ_PRJ_BITS);
        SgrParamsType params;
        params.r[0] =
            !is_fixed_size ? (rnd8_.random() % MAX_RADIUS) : (index % 2);
        params.r[1] =
            !is_fixed_size ? (rnd8_.random() % MAX_RADIUS) : (index / 2);
        params.s[0] =
            !is_fixed_size ? (rnd8_.random() % MAX_RADIUS) : (index % 2);
        params.s[1] =
            !is_fixed_size ? (rnd8_.random() % MAX_RADIUS) : (index / 2);
        uint8_t* dgd =
            (sizeof(*dgd_) == 2) ? (CONVERT_TO_BYTEPTR(dgd_)) : (uint8_t*)dgd_;
        uint8_t* src =
            (sizeof(*src_) == 2) ? (CONVERT_TO_BYTEPTR(src_)) : (uint8_t*)src_;

        int64_t err_ref = ref_func_(src,
                                    h_end,
                                    v_end,
                                    src_stride,
                                    dgd,
                                    dgd_stride,
                                    flt0_,
                                    flt0_stride,
                                    flt1_,
                                    flt1_stride,
                                    xq,
                                    &params);
        int64_t err_test = tst_func_(src,
                                     h_end,
                                     v_end,
                                     src_stride,
                                     dgd,
                                     dgd_stride,
                                     flt0_,
                                     flt0_stride,
                                     flt1_,
                                     flt1_stride,
                                     xq,
                                     &params);
        ASSERT_EQ(err_ref, err_test);
    }

    virtual void run_pixel_proj_err_test(const int run_times,
                                         const bool is_fixed_size) {
        const int iters = max(run_times, min_test_times);
        for (int iter = 0; iter < iters && !HasFatalFailure(); ++iter) {
            prepare_data();
            run_and_check_data(iter, is_fixed_size ? 128 : 0);
        }
    }

    virtual void run_pixel_proj_err_extreme_test() {
        const int iters = min_test_times;
        for (int iter = 0; iter < iters && !HasFatalFailure(); ++iter) {
            prepare_extreme_data();
            run_and_check_data(iter, 192);
        }
    }

  protected:
    PixelProjFcn tst_func_;
    PixelProjFcn ref_func_;
    Sample* src_;
    Sample* dgd_;
    int32_t* flt0_;
    int32_t* flt1_;

    SVTRandom rnd8_;
    SVTRandom rnd16_;
    SVTRandom rnd15s_;
    SVTRandom rnd_bk_size_;
};

class PixelProjErrorLbdTest : public PixelProjErrorTest<uint8_t> {
    void prepare_data() override {
        for (int i = 0; i < MAX_DATA_BLOCK * MAX_DATA_BLOCK; ++i) {
            dgd_[i] = rnd8_.random();
            src_[i] = rnd8_.random();
            flt0_[i] = rnd15s_.random();
            flt1_[i] = rnd15s_.random();
        }
    }
    void prepare_extreme_data() override {
        for (int i = 0; i < MAX_DATA_BLOCK * MAX_DATA_BLOCK; ++i) {
            dgd_[i] = 0;
            src_[i] = 255;
            flt0_[i] = rnd15s_.random();
            flt1_[i] = rnd15s_.random();
        }
    }
};

TEST_P(PixelProjErrorLbdTest, run_check_output) {
    run_pixel_proj_err_test(50, true);
}
TEST_P(PixelProjErrorLbdTest, run_random_size_check_output) {
    run_pixel_proj_err_test(50, false);
}
TEST_P(PixelProjErrorLbdTest, run_extreme_check_output) {
    run_pixel_proj_err_extreme_test();
}

static const PixelProjErrorTestParam lbd_test_vector[] = {
    make_tuple(av1_lowbd_pixel_proj_error_avx2, av1_lowbd_pixel_proj_error_c)};

INSTANTIATE_TEST_CASE_P(RST, PixelProjErrorLbdTest,
                        ::testing::ValuesIn(lbd_test_vector));

class PixelProjErrorHbdTest : public PixelProjErrorTest<uint16_t> {
  protected:
    PixelProjErrorHbdTest() : rnd12_(12, false) {
    }
    void prepare_data() override {
        for (int i = 0; i < MAX_DATA_BLOCK * MAX_DATA_BLOCK; ++i) {
            dgd_[i] = rnd12_.random();
            src_[i] = rnd12_.random();
            flt0_[i] = rnd15s_.random();
            flt1_[i] = rnd15s_.random();
        }
    }
    void prepare_extreme_data() override {
        for (int i = 0; i < MAX_DATA_BLOCK * MAX_DATA_BLOCK; ++i) {
            dgd_[i] = 0;
            src_[i] = (1 << 12) - 1;
            flt0_[i] = rnd15s_.random();
            flt1_[i] = rnd15s_.random();
        }
    }

  private:
    SVTRandom rnd12_;
};

TEST_P(PixelProjErrorHbdTest, run_check_output) {
    run_pixel_proj_err_test(50, true);
}
TEST_P(PixelProjErrorHbdTest, run_random_size_check_output) {
    run_pixel_proj_err_test(50, false);
}
TEST_P(PixelProjErrorHbdTest, run_extreme_check_output) {
    run_pixel_proj_err_extreme_test();
}

static const PixelProjErrorTestParam hbd_test_vector[] = {make_tuple(
    av1_highbd_pixel_proj_error_avx2, av1_highbd_pixel_proj_error_c)};

INSTANTIATE_TEST_CASE_P(RST, PixelProjErrorHbdTest,
                        ::testing::ValuesIn(hbd_test_vector));

}  // namespace

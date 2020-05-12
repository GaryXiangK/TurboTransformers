// Copyright (C) 2020 THL A29 Limited, a Tencent company.
// All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); you may
// not use this file except in compliance with the License. You may
// obtain a copy of the License at
// https://opensource.org/licenses/BSD-3-Clause
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.
// See the AUTHORS file for names of contributors.

#include "turbo_transformers/layers/positionwise_ffn.h"

#include <loguru.hpp>

#include "turbo_transformers/core/memory.h"
#include "turbo_transformers/layers/kernels/activation.h"
#include "turbo_transformers/layers/kernels/common.h"
#include "turbo_transformers/layers/kernels/layer_norm.h"
#include "turbo_transformers/layers/kernels/mat_mul.h"
#include "turbo_transformers/layers/kernels/utils.h"

namespace turbo_transformers {
namespace layers {

void PositionwiseFeedForward::operator()(const core::Tensor& input_tensor,
                                         core::Tensor* output_tensor) const {
  TT_ENFORCE_EQ(
      dense_weight_1_.shape(0), input_tensor.shape(2),
      "dense weight and input tensor should have the same model_dim.");

  auto devType = input_tensor.device_type();
  auto devId = input_tensor.device_id();

  // input tensor size (batch_size, input_len, model_dim)
  auto batch_size = input_tensor.shape(0);
  auto input_len = input_tensor.shape(1);
  auto model_dim = input_tensor.shape(2);
  auto d_ff = dense_weight_1_.shape(1);

  // allocate memory for temp data
  core::Tensor input_tensor_copy(nullptr);
  input_tensor_copy.Reshape<float>({batch_size, input_len, model_dim}, devType,
                                   devId);
  core::Tensor temp_tensor(nullptr);
  temp_tensor.Reshape<float>({batch_size * input_len, d_ff}, devType, devId);

  // start computation
  core::Copy<float>(input_tensor, input_tensor_copy);

  output_tensor->Reshape<float>({batch_size, input_len, model_dim}, devType,
                                devId);

  kernels::LayerNorm<float>(layer_norm_weight_, layer_norm_bias_,
                            &input_tensor_copy);
  kernels::MatMul(input_tensor_copy, false, dense_weight_1_, false,
                  1.0,  // input (b*seq, model) X dense_weight_1_ (model_dim,
                        // d_ff) -> temp_tensor (B*seq, d_ff)
                  &temp_tensor, 0.0);
  kernels::AddBiasAct<float, types::ActivationType::Relu>(dense_bias_1_,
                                                          &temp_tensor);
  kernels::MatMul(temp_tensor, false, dense_weight_2_, false, 1.0,
                  &input_tensor_copy, 0.0);

  kernels::AddInputBias(input_tensor, input_tensor_copy, dense_bias_2_,
                        output_tensor);
}

void PositionwiseFeedForward::EnforceShapeAndType() const {}

}  // namespace layers
}  // namespace turbo_transformers
#!/usr/bin/env python
# Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse
import numpy as np
from PIL import Image

import grpc
from tritongrpcclient import grpc_service_v2_pb2
from tritongrpcclient import grpc_service_v2_pb2_grpc
import tritongrpcclient.model_config_pb2 as mc

FLAGS = None


def model_dtype_to_np(model_dtype):
    if model_dtype == "BOOL":
        return np.bool
    elif model_dtype == "INT8":
        return np.int8
    elif model_dtype == "INT16":
        return np.int16
    elif model_dtype == "INT32":
        return np.int32
    elif model_dtype == "INT64":
        return np.int64
    elif model_dtype == "UINT8":
        return np.uint8
    elif model_dtype == "UINT16":
        return np.uint16
    elif model_dtype == "FP16":
        return np.float16
    elif model_dtype == "FP32":
        return np.float32
    elif model_dtype == "FP64":
        return np.float64
    elif model_dtype == "BYTES":
        return np.dtype(object)
    return None


def parse_model(model_metadata, model_config):
    """
    Check the configuration of a model to make sure it meets the
    requirements for an image classification network (as expected by
    this client)
    """
    if len(model_metadata.inputs) != 1:
        raise Exception("expecting 1 input, got {}".format(
            len(model_metadata.inputs)))
    if len(model_metadata.outputs) != 1:
        raise Exception("expecting 1 output, got {}".format(
            len(model_metadata.outputs)))

    if len(model_config.input) != 1:
        raise Exception(
            "expecting 1 input in model configuration, got {}".format(
                len(model_config.input)))

    input_metadata = model_metadata.inputs[0]
    input_config = model_config.input[0]
    output_metadata = model_metadata.outputs[0]

    if output_metadata.datatype != "FP32":
        raise Exception("expecting output datatype to be FP32, model '" +
                        model_metadata.name + "' output type is " +
                        ouput.datatype)

    # Output is expected to be a vector. But allow any number of
    # dimensions as long as all but 1 is size 1 (e.g. { 10 }, { 1, 10
    # }, { 10, 1, 1 } are all ok).
    non_one_cnt = 0
    for dim in output_metadata.shape:
        if dim > 1:
            non_one_cnt += 1
            if non_one_cnt > 1:
                raise Exception("expecting model output to be a vector")

    # Model input must have 3 dims, either CHW or HWC
    if len(input_metadata.shape) != 3:
        raise Exception(
            "expecting input to have 3 dimensions, model '{}' input has {}".
            format(model_name, len(input_metadata.shape)))

    if ((input_config.format != mc.ModelInput.FORMAT_NCHW) and
        (input_config.format != mc.ModelInput.FORMAT_NHWC)):
        raise Exception("unexpected input format " +
                        mc.ModelInput.Format.Name(input_config.format) +
                        ", expecting " +
                        mc.ModelInput.Format.Name(mc.ModelInput.FORMAT_NCHW) +
                        " or " +
                        mc.ModelInput.Format.Name(mc.ModelInput.FORMAT_NHWC))

    if input_config.format == mc.ModelInput.FORMAT_NHWC:
        h = input_metadata.shape[0]
        w = input_metadata.shape[1]
        c = input_metadata.shape[2]
    else:
        c = input_metadata.shape[0]
        h = input_metadata.shape[1]
        w = input_metadata.shape[2]

    return (input_metadata.name, output_metadata.name, c, h, w,
            input_config.format, input_metadata.datatype)


def preprocess(img, format, dtype, c, h, w, scaling):
    """
    Pre-process an image to meet the size, type and format
    requirements specified by the parameters.
    """
    #np.set_printoptions(threshold='nan')

    if c == 1:
        sample_img = img.convert('L')
    else:
        sample_img = img.convert('RGB')

    resized_img = sample_img.resize((w, h), Image.BILINEAR)
    resized = np.array(resized_img)
    if resized.ndim == 2:
        resized = resized[:, :, np.newaxis]

    npdtype = model_dtype_to_np(dtype)
    typed = resized.astype(npdtype)

    if scaling == 'INCEPTION':
        scaled = (typed / 128) - 1
    elif scaling == 'VGG':
        if c == 1:
            scaled = typed - np.asarray((128,), dtype=npdtype)
        else:
            scaled = typed - np.asarray((123, 117, 104), dtype=npdtype)
    else:
        scaled = typed

    # Swap to CHW if necessary
    if format == mc.ModelInput.FORMAT_NCHW:
        ordered = np.transpose(scaled, (2, 0, 1))
    else:
        ordered = scaled

    # Channels are in RGB order. Currently model configuration data
    # doesn't provide any information as to other channel orderings
    # (like BGR) so we just assume RGB.
    return ordered


def postprocess(results, batch_size):
    """
    Post-process results to show classifications.
    """
    if len(results) != 1:
        raise Exception("expected 1 result, got {}".format(len(results)))

    # FIXMEV2 should get datatype from result
    contents = np.frombuffer(
        results[0].contents.raw_contents,
        dtype=model_dtype_to_np("FP32"))  #results[0].datatype
    contents = np.reshape(contents, results[0].shape)
    print(contents)


def requestGenerator(input_name, output_name, c, h, w, format, dtype, FLAGS):
    request = grpc_service_v2_pb2.ModelInferRequest()
    request.model_name = FLAGS.model_name
    request.model_version = FLAGS.model_version

    output = grpc_service_v2_pb2.ModelInferRequest().InferRequestedOutputTensor(
    )
    output.name = output_name
    request.outputs.extend([output])

    input = grpc_service_v2_pb2.ModelInferRequest().InferInputTensor()
    input.name = input_name
    input.datatype = dtype
    if format == mc.ModelInput.FORMAT_NHWC:
        input.shape.extend([FLAGS.batch_size, h, w, c])
    else:
        input.shape.extend([FLAGS.batch_size, c, h, w])

    # Preprocess image into input data according to model requirements
    image_data = None
    with Image.open(FLAGS.image_filename) as img:
        image_data = preprocess(img, format, dtype, c, h, w, FLAGS.scaling)

    # Send requests of FLAGS.batch_size images.
    input_bytes = None
    for idx in range(FLAGS.batch_size):
        if input_bytes is None:
            input_bytes = image_data.tobytes()
        else:
            input_bytes += image_data.tobytes()

    input_contents = grpc_service_v2_pb2.InferTensorContents()
    input_contents.raw_contents = input_bytes
    input.contents.CopyFrom(input_contents)
    request.inputs.extend([input])

    yield request


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-v',
                        '--verbose',
                        action="store_true",
                        required=False,
                        default=False,
                        help='Enable verbose output')
    parser.add_argument('-m',
                        '--model-name',
                        type=str,
                        required=True,
                        help='Name of model')
    parser.add_argument(
        '-x',
        '--model-version',
        type=str,
        required=False,
        default="",
        help='Version of model. Default is to use latest version.')
    parser.add_argument('-b',
                        '--batch-size',
                        type=int,
                        required=False,
                        default=1,
                        help='Batch size. Default is 1.')
    parser.add_argument('-c',
                        '--classes',
                        type=int,
                        required=False,
                        default=1,
                        help='Number of class results to report. Default is 1.')
    parser.add_argument(
        '-s',
        '--scaling',
        type=str,
        choices=['NONE', 'INCEPTION', 'VGG'],
        required=False,
        default='NONE',
        help='Type of scaling to apply to image pixels. Default is NONE.')
    parser.add_argument('-u',
                        '--url',
                        type=str,
                        required=False,
                        default='localhost:8001',
                        help='Inference server URL. Default is localhost:8001.')
    parser.add_argument('image_filename', type=str, help='Input image.')
    FLAGS = parser.parse_args()

    # Create gRPC stub for communicating with the server
    channel = grpc.insecure_channel(FLAGS.url)
    grpc_stub = grpc_service_v2_pb2_grpc.GRPCInferenceServiceStub(channel)

    # Make sure the model matches our requirements, and get some
    # properties of the model that we need for preprocessing
    metadata_request = grpc_service_v2_pb2.ModelMetadataRequest(
        name=FLAGS.model_name, version=FLAGS.model_version)
    metadata_response = grpc_stub.ModelMetadata(metadata_request)

    config_request = grpc_service_v2_pb2.ModelConfigRequest(
        name=FLAGS.model_name, version=FLAGS.model_version)
    config_response = grpc_stub.ModelConfig(config_request)

    input_name, output_name, c, h, w, format, dtype = parse_model(
        metadata_response, config_response.config)

    # Send requests of FLAGS.batch_size images. If the number of
    # images isn't an exact multiple of FLAGS.batch_size then just
    # start over with the first images until the batch is filled.
    requests = []
    responses = []

    # Send request
    for request in requestGenerator(input_name, output_name, c, h, w, format,
                                    dtype, FLAGS):
        responses.append(grpc_stub.ModelInfer(request))

    for response in responses:
        postprocess(response.outputs, FLAGS.batch_size)

    print("PASS")
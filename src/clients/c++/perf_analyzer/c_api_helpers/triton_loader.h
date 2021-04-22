// Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include "src/clients/c++/perf_analyzer/c_api_helpers/shared_library.h"
#include "src/clients/c++/perf_analyzer/error.h"
#include "triton/core/tritonserver.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

// If TRITONSERVER error is non-OK, return the corresponding status.
#define RETURN_IF_TRITONSERVER_ERROR(E, MSG)                \
  do {                                                      \
    TRITONSERVER_Error* err__ = (E);                        \
    if (err__ != nullptr) {                                 \
      std::cout << "error: " << (MSG) << ": "               \
                << error_code_to_string_fn_(err__) << " - " \
                << error_message_fn_(err__) << std::endl;   \
      Error newErr = Error(MSG);                            \
      error_delete_fn_(err__);                              \
      return newErr;                                        \
    }                                                       \
  } while (false)

#define REPORT_TRITONSERVER_ERROR(E)                      \
  do {                                                    \
    TRITONSERVER_Error* err__ = (E);                      \
    if (err__ != nullptr) {                               \
      std::cout << error_message_fn_(err__) << std::endl; \
      error_delete_fn_(err__);                            \
    }                                                     \
  } while (false)

namespace perfanalyzer { namespace clientbackend {

class TritonLoader {
 public:
  static Error Create(
      const std::string& library_directory, const std::string& model_repository,
      const std::string& memory_type, std::shared_ptr<TritonLoader>* loader);
  ~TritonLoader()
  {
    FAIL_IF_ERR(
        CloseLibraryHandle(dlhandle_), "error on closing triton loader");
    ClearHandles();
  }

  TritonLoader(
      const std::string& library_directory, const std::string& model_repository,
      const std::string& memory_type);

  Error StartTriton(const std::string& memory_type, bool isVerbose);

  Error LoadModel(
      const std::string& model_name, const std::string& model_version);

  Error ModelMetadata(rapidjson::Document* model_metadata) const;

  Error ModelConfig(rapidjson::Document* model_config) const;

  Error ServerMetaData(rapidjson::Document* server_metadata) const;

  bool ModelIsLoaded() const { return model_is_loaded_; }
  bool ServerIsReady() const { return server_is_ready_; }

  // TRITONSERVER_ApiVersion
  typedef TRITONSERVER_Error* (*TritonServerApiVersionFn_t)(
      uint32_t* major, uint32_t* minor);
  // TRITONSERVER_ServerOptionsNew
  typedef TRITONSERVER_Error* (*TritonServerOptionsNewFn_t)(
      TRITONSERVER_ServerOptions** options);
  // TRITONSERVER_ServerOptionsSetModelRepositoryPath
  typedef TRITONSERVER_Error* (*TritonServerOptionSetModelRepoPathFn_t)(
      TRITONSERVER_ServerOptions* options, const char* model_repository_path);
  // TRITONSERVER_ServerOptionsSetLogVerbose
  typedef TRITONSERVER_Error* (*TritonServerSetLogVerboseFn_t)(
      TRITONSERVER_ServerOptions* options, int level);

  // TRITONSERVER_ServerOptionsSetBackendDirectory
  typedef TRITONSERVER_Error* (*TritonServerSetBackendDirFn_t)(
      TRITONSERVER_ServerOptions* options, const char* backend_dir);
  // TRITONSERVER_ServerOptionsSetRepoAgentDirectory
  typedef TRITONSERVER_Error* (*TritonServerSetRepoAgentDirFn_t)(
      TRITONSERVER_ServerOptions* options, const char* repoagent_dir);
  // TRITONSERVER_ServerOptionsSetStrictModelConfig
  typedef TRITONSERVER_Error* (*TritonServerSetStrictModelConfigFn_t)(
      TRITONSERVER_ServerOptions* options, bool strict);
  // TRITONSERVER_ServerOptionsSetMinSupportedComputeCapability
  typedef TRITONSERVER_Error* (
      *TritonServerSetMinSupportedComputeCapabilityFn_t)(
      TRITONSERVER_ServerOptions* options, double cc);

  // TRITONSERVER_ServerNew
  typedef TRITONSERVER_Error* (*TritonServerNewFn_t)(
      TRITONSERVER_Server** server, TRITONSERVER_ServerOptions* option);
  // TRITONSERVER_ServerOptionsDelete
  typedef TRITONSERVER_Error* (*TritonServerOptionsDeleteFn_t)(
      TRITONSERVER_ServerOptions* options);
  // TRITONSERVER_ServerDelete
  typedef TRITONSERVER_Error* (*TritonServerDeleteFn_t)(
      TRITONSERVER_Server* server);
  // TRITONSERVER_ServerIsLive
  typedef TRITONSERVER_Error* (*TritonServerIsLiveFn_t)(
      TRITONSERVER_Server* server, bool* live);

  // TRITONSERVER_ServerIsReady
  typedef TRITONSERVER_Error* (*TritonServerIsReadyFn_t)(
      TRITONSERVER_Server* server, bool* ready);
  // TRITONSERVER_ServerMetadata
  typedef TRITONSERVER_Error* (*TritonServerMetadataFn_t)(
      TRITONSERVER_Server* server, TRITONSERVER_Message** server_metadata);
  // TRITONSERVER_MessageSerializeToJson
  typedef TRITONSERVER_Error* (*TritonServerMessageSerializeToJsonFn_t)(
      TRITONSERVER_Message* message, const char** base, size_t* byte_size);
  // TRITONSERVER_MessageDelete
  typedef TRITONSERVER_Error* (*TritonServerMessageDeleteFn_t)(
      TRITONSERVER_Message* message);

  // TRITONSERVER_ServerModelIsReady
  typedef TRITONSERVER_Error* (*TritonServerModelIsReadyFn_t)(
      TRITONSERVER_Server* server, const char* model_name,
      const int64_t model_version, bool* ready);
  // TRITONSERVER_ServerModelMetadata
  typedef TRITONSERVER_Error* (*TritonServerModelMetadataFn_t)(
      TRITONSERVER_Server* server, const char* model_name,
      const int64_t model_version, TRITONSERVER_Message** model_metadata);
  // TRITONSERVER_ResponseAllocatorNew
  typedef TRITONSERVER_Error* (*TritonServerResponseAllocatorNewFn_t)(
      TRITONSERVER_ResponseAllocator** allocator,
      TRITONSERVER_ResponseAllocatorAllocFn_t alloc_fn,
      TRITONSERVER_ResponseAllocatorReleaseFn_t release_fn,
      TRITONSERVER_ResponseAllocatorStartFn_t start_fn);
  // TRITONSERVER_InferenceRequestNew
  typedef TRITONSERVER_Error* (*TritonServerInferenceRequestNewFn_t)(
      TRITONSERVER_InferenceRequest** inference_request,
      TRITONSERVER_Server* server, const char* model_name,
      const int64_t model_version);

  // TRITONSERVER_InferenceRequestSetId
  typedef TRITONSERVER_Error* (*TritonServerInferenceRequestSetIdFn_t)(
      TRITONSERVER_InferenceRequest* inference_request, const char* id);
  // TRITONSERVER_InferenceRequestSetReleaseCallback
  typedef TRITONSERVER_Error* (
      *TritonServerInferenceRequestSetReleaseCallbackFn_t)(
      TRITONSERVER_InferenceRequest* inference_request,
      TRITONSERVER_InferenceRequestReleaseFn_t request_release_fn,
      void* request_release_userp);
  // TRITONSERVER_InferenceRequestAddInput
  typedef TRITONSERVER_Error* (*TritonServerInferenceRequestAddInputFn_t)(
      TRITONSERVER_InferenceRequest* inference_request, const char* name,
      const TRITONSERVER_DataType datatype, const int64_t* shape,
      uint64_t dim_count);
  // TRITONSERVER_InferenceRequestAddRequestedOutput
  typedef TRITONSERVER_Error* (
      *TritonServerInferenceRequestAddRequestedOutputFn_t)(
      TRITONSERVER_InferenceRequest* inference_request, const char* name);

  // TRITONSERVER_InferenceRequestAppendInputData
  typedef TRITONSERVER_Error* (
      *TritonServerInferenceRequestAppendInputDataFn_t)(
      TRITONSERVER_InferenceRequest* inference_request, const char* name,
      const void* base, size_t byte_size, TRITONSERVER_MemoryType memory_type,
      int64_t memory_type_i);
  // TRITONSERVER_InferenceRequestSetResponseCallback
  typedef TRITONSERVER_Error* (
      *TritonServerInferenceRequestSetResponseCallbackFn_t)(
      TRITONSERVER_InferenceRequest* inference_request,
      TRITONSERVER_ResponseAllocator* response_allocator,
      void* response_allocator_userp,
      TRITONSERVER_InferenceResponseCompleteFn_t response_fn,
      void* response_userp);
  // TRITONSERVER_ServerInferAsync
  typedef TRITONSERVER_Error* (*TritonServerInferAsyncFn_t)(
      TRITONSERVER_Server* server,
      TRITONSERVER_InferenceRequest* inference_request,
      TRITONSERVER_InferenceTrace* trace);
  // TRITONSERVER_InferenceResponseError
  typedef TRITONSERVER_Error* (*TritonServerInferenceResponseErrorFn_t)(
      TRITONSERVER_InferenceResponse* inference_response);

  // TRITONSERVER_InferenceResponseDelete
  typedef TRITONSERVER_Error* (*TritonServerInferenceResponseDeleteFn_t)(
      TRITONSERVER_InferenceResponse* inference_response);
  // TRITONSERVER_InferenceRequestRemoveAllInputData
  typedef TRITONSERVER_Error* (
      *TritonServerInferenceRequestRemoveAllInputDataFn_t)(
      TRITONSERVER_InferenceRequest* inference_request, const char* name);
  // TRITONSERVER_ResponseAllocatorDelete
  typedef TRITONSERVER_Error* (*TritonServerResponseAllocatorDeleteFn_t)(
      TRITONSERVER_ResponseAllocator* allocator);
  // TRITONSERVER_ErrorNew
  typedef TRITONSERVER_Error* (*TritonServerErrorNewFn_t)(
      TRITONSERVER_Error_Code code, const char* msg);

  // TRITONSERVER_MemoryTypeString
  typedef const char* (*TritonServerMemoryTypeStringFn_t)(
      TRITONSERVER_MemoryType memtype);
  // TRITONSERVER_InferenceResponseOutputCount
  typedef TRITONSERVER_Error* (*TritonServerInferenceResponseOutputCountFn_t)(
      TRITONSERVER_InferenceResponse* inference_response, uint32_t* count);
  // TRITONSERVER_DataTypeString
  typedef const char* (*TritonServerDataTypeStringFn_t)(
      TRITONSERVER_DataType datatype);
  // TRITONSERVER_ErrorMessage
  typedef const char* (*TritonServerErrorMessageFn_t)(
      TRITONSERVER_Error* error);
  // TRITONSERVER_ErrorDelete
  typedef void (*TritonServerErrorDeleteFn_t)(TRITONSERVER_Error* error);
  // TRITONSERVER_ErrorCodeString
  typedef const char* (*TritonServerErrorCodeToStringFn_t)(
      TRITONSERVER_Error* error);
  // TRITONSERVER_ServerModelConfig
  typedef TRITONSERVER_Error* (*TritonServerModelConfigFn_t)(
      TRITONSERVER_Server* server, const char* model_name,
      const int64_t model_version, const uint32_t config_version,
      TRITONSERVER_Message** model_config);

 private:
  /// Load all tritonserver.h functions onto triton_loader
  /// internal handles
  Error LoadServerLibrary();

  void ClearHandles();

  /// Check if file exists in the current directory
  /// \param filepath Path of library to check
  /// \return perfanalyzer::clientbackend::Error
  Error FileExists(std::string& filepath);

  TRITONSERVER_Error* ParseModelMetadata(
      const rapidjson::Document& model_metadata, bool* is_int,
      bool* is_torch_model);

  void* dlhandle_;
  TritonServerApiVersionFn_t api_version_fn_;
  TritonServerOptionsNewFn_t options_new_fn_;
  TritonServerOptionSetModelRepoPathFn_t options_set_model_repo_path_fn_;
  TritonServerSetLogVerboseFn_t set_log_verbose_fn_;

  TritonServerSetBackendDirFn_t set_backend_directory_fn_;
  TritonServerSetRepoAgentDirFn_t set_repo_agent_directory_fn_;
  TritonServerSetStrictModelConfigFn_t set_strict_model_config_fn_;
  TritonServerSetMinSupportedComputeCapabilityFn_t
      set_min_supported_compute_capability_fn_;

  TritonServerNewFn_t server_new_fn_;
  TritonServerOptionsDeleteFn_t server_options_delete_fn_;
  TritonServerDeleteFn_t server_delete_fn_;
  TritonServerIsLiveFn_t server_is_live_fn_;

  TritonServerIsReadyFn_t server_is_ready_fn_;
  TritonServerMetadataFn_t server_metadata_fn_;
  TritonServerMessageSerializeToJsonFn_t message_serialize_to_json_fn_;
  TritonServerMessageDeleteFn_t message_delete_fn_;

  TritonServerModelIsReadyFn_t model_is_ready_fn_;
  TritonServerModelMetadataFn_t model_metadata_fn_;
  TritonServerResponseAllocatorNewFn_t response_allocator_new_fn_;
  TritonServerInferenceRequestNewFn_t inference_request_new_fn_;

  TritonServerInferenceRequestSetIdFn_t inference_request_set_id_fn_;
  TritonServerInferenceRequestSetReleaseCallbackFn_t
      inference_request_set_release_callback_fn_;
  TritonServerInferenceRequestAddInputFn_t inference_request_add_input_fn_;
  TritonServerInferenceRequestAddRequestedOutputFn_t
      inference_request_add_requested_output_fn_;

  TritonServerInferenceRequestAppendInputDataFn_t
      inference_request_append_input_data_fn_;
  TritonServerInferenceRequestSetResponseCallbackFn_t
      inference_request_set_response_callback_fn_;
  TritonServerInferAsyncFn_t infer_async_fn_;
  TritonServerInferenceResponseErrorFn_t inference_response_error_fn_;

  TritonServerInferenceResponseDeleteFn_t inference_response_delete_fn_;
  TritonServerInferenceRequestRemoveAllInputDataFn_t
      inference_request_remove_all_input_data_fn_;
  TritonServerResponseAllocatorDeleteFn_t response_allocator_delete_fn_;
  TritonServerErrorNewFn_t error_new_fn_;

  TritonServerMemoryTypeStringFn_t memory_type_string_fn_;
  TritonServerInferenceResponseOutputCountFn_t
      inference_response_output_count_fn_;
  TritonServerDataTypeStringFn_t data_type_string_fn_;
  TritonServerErrorMessageFn_t error_message_fn_;
  TritonServerErrorDeleteFn_t error_delete_fn_;
  TritonServerErrorCodeToStringFn_t error_code_to_string_fn_;
  TritonServerModelConfigFn_t model_config_fn_;

  TRITONSERVER_ServerOptions* options_;
  TRITONSERVER_Server* server_ptr_;
  TRITONSERVER_ResponseAllocator* allocator_ = nullptr;
  std::shared_ptr<TRITONSERVER_Server> server_;
  std::string library_directory_;
  const std::string SERVER_LIBRARY_PATH = "/lib/libtritonserver.so";
  int verbose_level_ = 0;
  bool enforce_memory_type_ = false;
  std::string model_repository_path_;
  std::string model_name_;
  int64_t model_version_;
  TRITONSERVER_memorytype_enum requested_memory_type_ = TRITONSERVER_MEMORY_CPU;
  bool model_is_loaded_ = false;
  bool server_is_ready_ = false;
};


}}  // namespace perfanalyzer::clientbackend
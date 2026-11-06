// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

static void check(VkResult result, const char *operation) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(std::string(operation) + ": " +
                             std::to_string(result));
}

#define VK_CHECK(expr) check((expr), #expr)

static VkShaderModule create_shader(VkDevice device, const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("cannot open " + path);
  const auto length = file.tellg();
  if (length < 20 || length % 4)
    throw std::runtime_error("invalid SPIR-V size");
  std::vector<uint32_t> code(static_cast<size_t>(length) / 4);
  file.seekg(0);
  if (!file.read(reinterpret_cast<char *>(code.data()), length))
    throw std::runtime_error("SPIR-V read failed");
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = code.size() * 4;
  info.pCode = code.data();
  VkShaderModule shader;
  VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &shader));
  return shader;
}

struct Buffer {
  VkDevice device;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  float *host = nullptr;
  size_t count;

  Buffer(VkDevice dev, uint32_t memory_type, size_t elements)
      : device(dev), count(elements) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = (elements + 20) * sizeof(float);
    info.usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(device, &info, nullptr, &buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type;
    VK_CHECK(vkAllocateMemory(device, &allocate, nullptr, &memory));
    VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    VK_CHECK(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0,
                         reinterpret_cast<void **>(&host)));
    std::fill(host, host + elements + 20, -9999.0f);
  }
  ~Buffer() {
    if (host) vkUnmapMemory(device, memory);
    if (buffer) vkDestroyBuffer(device, buffer, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
  }
  float *values() { return host + 4; }
  void transfer(bool flush) {
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = memory;
    range.size = VK_WHOLE_SIZE;
    VK_CHECK(flush ? vkFlushMappedMemoryRanges(device, 1, &range)
                   : vkInvalidateMappedMemoryRanges(device, 1, &range));
  }
  void verify_guards() {
    for (size_t i = 0; i < 4; i++)
      if (host[i] != -9999.0f)
        throw std::runtime_error("buffer prefix overwritten");
    for (size_t i = count + 4; i < count + 20; i++)
      if (host[i] != -9999.0f)
        throw std::runtime_error("buffer suffix overwritten");
  }
};

static VkResult create_pipeline(
    VkDevice device, VkPipelineLayout layout, const std::string &path,
    VkPipeline *pipeline, const VkSpecializationInfo *specialization = nullptr,
    VkPipelineCreateFlags flags = 0) {
  VkShaderModule shader = create_shader(device, path);
  VkComputePipelineCreateInfo info{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  info.layout = layout;
  info.flags = flags;
  info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  info.stage.module = shader;
  info.stage.pName = "main";
  info.stage.pSpecializationInfo = specialization;
  VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info,
                                             nullptr, pipeline);
  vkDestroyShaderModule(device, shader, nullptr);
  return result;
}

static void barrier(VkCommandBuffer command, VkAccessFlags source,
                    VkAccessFlags destination) {
  VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  memory.srcAccessMask = source;
  memory.dstAccessMask = destination;
  constexpr auto stages =
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT;
  vkCmdPipelineBarrier(command, stages, stages, 0, 1, &memory, 0, nullptr, 0,
                       nullptr);
}

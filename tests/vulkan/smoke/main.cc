// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
// SPDX-License-Identifier: Apache-2.0

#include <unistd.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../common/vulkan.h"

struct Image {
  VkDevice device;
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  Image(VkDevice dev, uint32_t memory_type, VkExtent3D extent, uint32_t layers)
      : device(dev) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = extent.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    info.extent = extent;
    info.mipLevels = 1;
    info.arrayLayers = layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VK_CHECK(vkCreateImage(device, &info, nullptr, &image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type;
    VK_CHECK(vkAllocateMemory(device, &allocate, nullptr, &memory));
    VK_CHECK(vkBindImageMemory(device, image, memory, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image;
    vi.viewType = extent.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D
                  : layers > 1     ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                   : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = info.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    VK_CHECK(vkCreateImageView(device, &vi, nullptr, &view));
  }
  ~Image() {
    vkDestroyImageView(device, view, nullptr);
    vkDestroyImage(device, image, nullptr);
    vkFreeMemory(device, memory, nullptr);
  }
};

static void expect(float actual, float expected, const char *name,
                   size_t index) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-4f) {
    std::fprintf(stderr, "%s[%zu]: actual=%g expected=%g\n", name, index,
                 actual, expected);
    throw std::runtime_error("CPU oracle mismatch");
  }
}

static float input(unsigned buffer, unsigned i) {
  return buffer ? (int(i % 11) - 5) * 0.5f : (int(i % 17) - 8) * 0.25f;
}

static uint32_t subgroup_checksum(uint32_t i, uint32_t subgroup_width,
                                  uint32_t n) {
  auto hash32 = [](uint32_t v) {
    v = (v ^ (v >> 16)) * 0x7feb352du;
    v = (v ^ (v >> 15)) * 0x846ca68bu;
    return v ^ (v >> 16);
  };
  uint32_t checksum = 17;
  const uint32_t group = i / 40 * 40;
  const uint32_t first = group + (i % 40) / subgroup_width * subgroup_width;
  const uint32_t end =
      std::min<uint32_t>({first + subgroup_width, group + 40, uint32_t(n)});
  for (uint32_t round = 0; round < 1 + i % 3; ++round) {
    uint64_t mask = 0;
    uint32_t participants = 0, first_active = UINT32_MAX;
    uint32_t count = 0, inclusive = 0, exclusive = 0;
    for (uint32_t j = first; j < end; ++j) {
      if (round >= 1 + j % 3) continue;
      ++participants;
      first_active = std::min(first_active, j);
      if (!(round == 1 || (round == 0 && (hash32(j ^ round) & 1)))) continue;
      mask |= UINT64_C(1) << (j - first);
      ++count;
      inclusive += j <= i;
      exclusive += j < i;
    }
    const uint32_t predicate =
        round == 1 || (round == 0 && (hash32(i ^ round) & 1));
    const uint32_t values[]{uint32_t(mask),
                            uint32_t(mask >> 32),
                            0,
                            0,
                            uint32_t(count != 0),
                            uint32_t(count == participants),
                            1,
                            uint32_t(participants == 1),
                            1,
                            0,
                            count,
                            inclusive,
                            exclusive,
                            (first_active * 31337u) ^ 0x80000000u,
                            predicate,
                            predicate};
    for (auto v : values) checksum = hash32(checksum ^ v);
    if (mask) {
      checksum = hash32(checksum ^ uint32_t(__builtin_ctzll(mask)));
      checksum = hash32(checksum ^ uint32_t(63 - __builtin_clzll(mask)));
    }
    if (round == 0)
      checksum = hash32(checksum ^ ((first * 31337u) ^ 0x80000000u));
  }
  return checksum;
}

static void run_smoke(VkDevice device, VkQueue queue, uint32_t memory_type,
                      const std::string &directory, uint32_t width,
                      const VkPhysicalDeviceLimits &limits) {
  constexpr unsigned n = 259, workgroup = 40, math_count = n * 21,
                     image_count = 300;
  constexpr unsigned texel_count = 60, query_index = image_count + 120;
  struct Metadata {
    uint32_t counter, padding;
    uint64_t wide;
    uint32_t touched[n];
    uint64_t words[n];
    int8_t bytes[n + 3];
    int16_t shorts[n + 3];
    uint32_t dispatch[3];
  };
  Buffer a(device, memory_type, n), b(device, memory_type, 19 * 17);
  Buffer metadata(device, memory_type, sizeof(Metadata) / 4);
  Buffer output(device, memory_type, math_count + image_count + texel_count);
  Buffer copied(device, memory_type, query_index + 12 + 4);
  if (limits.maxImageDimension1D < 8192 || limits.maxImageDimension2D < 8192 ||
      limits.maxImageDimension3D < 2048 || limits.maxImageArrayLayers < 2048 ||
      limits.maxTexelBufferElements < limits.maxStorageBufferRange / 16)
    throw std::runtime_error("image limits below the OpenCL baseline");
  if (reinterpret_cast<uintptr_t>(a.host) % limits.minMemoryMapAlignment)
    throw std::runtime_error("mapped pointer lost base alignment");
  vkUnmapMemory(device, a.memory);
  a.host = nullptr;
  void *mapped;
  VK_CHECK(vkMapMemory(device, a.memory, 1, 17, 0, &mapped));
  bool aligned = (reinterpret_cast<uintptr_t>(mapped) - 1) %
                     limits.minMemoryMapAlignment ==
                 0;
  vkUnmapMemory(device, a.memory);
  if (!aligned) throw std::runtime_error("offset mapping lost base alignment");
  VK_CHECK(vkMapMemory(device, a.memory, 0, VK_WHOLE_SIZE, 0,
                       reinterpret_cast<void **>(&a.host)));
  for (unsigned i = 0; i < a.count; i++) a.values()[i] = input(0, i);
  for (unsigned i = 0; i < b.count; i++) b.values()[i] = input(1, i);
  Metadata data{};
  std::memset(data.bytes, 0xa5, sizeof(data.bytes));
  std::memset(data.shorts, 0x5a, sizeof(data.shorts));
  for (unsigned i = 0; i < n; i++) {
    data.bytes[i] = int(i % 61) - 30;
    data.shorts[i] = int(i % 141) - 70;
  }
  std::memcpy(metadata.values(), &data, sizeof(data));
  for (auto buffer : {&a, &b, &metadata, &output, &copied})
    buffer->transfer(true);

  Image source_array(device, memory_type, {5, 3, 1}, 2);
  Image source3d(device, memory_type, {5, 3, 2}, 1);
  Image result2d(device, memory_type, {5, 3, 1}, 1);
  Image result3d(device, memory_type, {5, 3, 2}, 1);
  Image result_array(device, memory_type, {5, 3, 1}, 2);
  Image *images[] = {&source_array, &source3d, &result2d, &result3d,
                     &result_array};
  const uint32_t layers[] = {2, 1, 1, 1, 2};
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = source_array.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView source2d;
  VK_CHECK(vkCreateImageView(device, &vi, nullptr, &source2d));
  VkSampler samplers[2];
  for (unsigned i = 0; i < 2; i++) {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = i ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device, &si, nullptr, &samplers[i]));
  }
  VkBufferView texels[2];
  for (unsigned i = 0; i < 2; i++) {
    VkBufferViewCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
    info.buffer = i ? output.buffer : a.buffer;
    info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    info.offset = 16 + (i ? (math_count + image_count) * 4 : 0);
    info.range = texel_count * 4;
    VK_CHECK(vkCreateBufferView(device, &info, nullptr, &texels[i]));
  }
  const VkDescriptorType types[] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                    VK_DESCRIPTOR_TYPE_SAMPLER,
                                    VK_DESCRIPTOR_TYPE_SAMPLER,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                                    VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
  VkDescriptorSetLayoutBinding bindings[13]{};
  for (unsigned i = 0; i < 13; i++) {
    bindings[i] = {i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    if (i == 4 || i == 6) bindings[i].pImmutableSamplers = &samplers[0];
  }
  VkDescriptorSetLayout layouts[5];
  VkDescriptorSetLayoutCreateInfo li{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  li.bindingCount = 13;
  li.pBindings = bindings;
  VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &layouts[0]));
  for (unsigned i = 1; i < 4; i++) {
    VkDescriptorSetLayoutBinding binding{
        i == 1 ? 3u : 7u,
        i == 1 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
               : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        i == 1 ? 2u : 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    li.bindingCount = i == 2 ? 0 : 1;
    li.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &layouts[i]));
  }
  layouts[4] = layouts[3];
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
  VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pi.setLayoutCount = 4;
  pi.pSetLayouts = layouts;
  pi.pushConstantRangeCount = 1;
  pi.pPushConstantRanges = &push;
  VkPipelineLayout layout;
  VK_CHECK(vkCreatePipelineLayout(device, &pi, nullptr, &layout));
  float scale = 3.25f;
  VkSpecializationMapEntry entry{0, 0, sizeof(scale)};
  VkSpecializationInfo specialization{1, &entry, sizeof(scale), &scale};
  VkPipeline pipelines[2];
  VK_CHECK(create_pipeline(device, layout, directory + "/smoke.spv",
                           &pipelines[0], &specialization));
  VK_CHECK(create_pipeline(device, layout, directory + "/images.spv",
                           &pipelines[1]));
  VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 2},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
      {VK_DESCRIPTOR_TYPE_SAMPLER, 2},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1}};
  VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpi.maxSets = 5;
  dpi.poolSizeCount = 8;
  dpi.pPoolSizes = sizes;
  VkDescriptorPool descriptor_pool;
  VK_CHECK(vkCreateDescriptorPool(device, &dpi, nullptr, &descriptor_pool));
  VkDescriptorSetAllocateInfo dai{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dai.descriptorPool = descriptor_pool;
  dai.descriptorSetCount = 5;
  dai.pSetLayouts = layouts;
  VkDescriptorSet sets[5];
  VK_CHECK(vkAllocateDescriptorSets(device, &dai, sets));
  VkDescriptorBufferInfo buffers[] = {
      {metadata.buffer, 16, sizeof(Metadata)},
      {a.buffer, 0, a.count * 4},
      {b.buffer, 0, b.count * 4},
      {output.buffer, 16, math_count * 4},
      {output.buffer, 16 + math_count * 4, image_count * 4}};
  // Ignored descriptor fields need not contain valid handles.
  auto ignored_sampler = reinterpret_cast<VkSampler>(uintptr_t{1});
  auto ignored_view = reinterpret_cast<VkImageView>(uintptr_t{1});
  VkDescriptorImageInfo image_infos[10] = {
      {ignored_sampler, source2d, VK_IMAGE_LAYOUT_GENERAL},
      {ignored_sampler, result2d.view, VK_IMAGE_LAYOUT_GENERAL},
      {samplers[1], ignored_view, VK_IMAGE_LAYOUT_UNDEFINED},
      {},
      {samplers[1], source3d.view, VK_IMAGE_LAYOUT_GENERAL},
      {},
      {samplers[0], source_array.view, VK_IMAGE_LAYOUT_GENERAL},
      {ignored_sampler, result3d.view, VK_IMAGE_LAYOUT_GENERAL},
      {ignored_sampler, result_array.view, VK_IMAGE_LAYOUT_GENERAL}};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.descriptorCount = 1;
  for (unsigned i = 0; i < 12; i++) {
    if (i == 4 || i == 6) continue;
    write.dstSet = sets[0];
    write.dstBinding = i;
    write.descriptorType = types[i];
    write.pBufferInfo = i == 0 ? &buffers[0] : nullptr;
    write.pImageInfo = i > 0 && i < 10 ? &image_infos[i - 1] : nullptr;
    write.pTexelBufferView = i >= 10 ? &texels[i - 10] : nullptr;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  write.pImageInfo = nullptr;
  write.pTexelBufferView = nullptr;
  for (unsigned i = 1; i < 5; i++) {
    write.dstSet = sets[i < 3 ? 1 : i];
    write.dstBinding = i < 3 ? 3 : 7;
    write.dstArrayElement = i == 2 ? 1 : 0;
    write.descriptorType = i < 3 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                                 : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffers[i];
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  VkBufferCreateInfo unused_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  unused_info.size = 16;
  unused_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  VkBuffer unused;
  VK_CHECK(vkCreateBuffer(device, &unused_info, nullptr, &unused));
  VK_CHECK(vkBindBufferMemory(device, unused, a.memory, 0));
  VkDescriptorBufferInfo unused_buffer{unused, 0, 16};
  write.dstSet = sets[0];
  write.dstBinding = 12;
  write.pBufferInfo = &unused_buffer;
  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  // Statically unused descriptors may outlive their resources.
  vkDestroyBuffer(device, unused, nullptr);
  for (auto pair : {std::pair{4u, 3u}, std::pair{5u, 6u}, std::pair{6u, 5u}}) {
    VkCopyDescriptorSet copy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
    copy.srcSet = copy.dstSet = sets[0];
    copy.srcBinding = pair.first;
    copy.dstBinding = pair.second;
    copy.descriptorCount = 1;
    vkUpdateDescriptorSets(device, 0, nullptr, 1, &copy);
  }

  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = 2;
  VkQueryPool queries;
  VK_CHECK(vkCreateQueryPool(device, &qi, nullptr, &queries));
  auto reset_queries = reinterpret_cast<PFN_vkResetQueryPoolEXT>(
      vkGetDeviceProcAddr(device, "vkResetQueryPoolEXT"));
  reset_queries(device, queries, 0, 2);
  uint64_t unavailable[2] = {123, 456};
  if (vkGetQueryPoolResults(
          device, queries, 0, 1, sizeof(unavailable), unavailable,
          sizeof(unavailable),
          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) !=
          VK_NOT_READY ||
      unavailable[1] != 0)
    throw std::runtime_error("unavailable query incorrectly completed");
  VkEventCreateInfo ei{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
  VkEvent gate, done;
  VK_CHECK(vkCreateEvent(device, &ei, nullptr, &gate));
  VK_CHECK(vkCreateEvent(device, &ei, nullptr, &done));
  if (vkGetEventStatus(device, gate) != VK_EVENT_RESET)
    throw std::runtime_error("new event incorrectly signaled");
  VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  VkCommandPool pool;
  VK_CHECK(vkCreateCommandPool(device, &ci, nullptr, &pool));
  VkCommandBufferAllocateInfo ai{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = pool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer command, primary;
  VK_CHECK(vkAllocateCommandBuffers(device, &ai, &command));
  VkCommandBufferInheritanceInfo inheritance{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.pInheritanceInfo = &inheritance;
  VK_CHECK(vkBeginCommandBuffer(command, &bi));
  vkCmdResetQueryPool(command, queries, 0, 2);
  vkCmdWaitEvents(command, 1, &gate, VK_PIPELINE_STAGE_HOST_BIT,
                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, nullptr, 0, nullptr, 0,
                  nullptr);
  const VkDeviceSize query_offset = 16 + query_index * 4;
  vkCmdCopyQueryPoolResults(
      command, queries, 0, 1, copied.buffer, query_offset + 32, 16,
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries, 0);
  VkImageMemoryBarrier transitions[5]{};
  for (unsigned i = 0; i < 5; i++) {
    transitions[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    transitions[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    transitions[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    transitions[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT |
                                   VK_ACCESS_SHADER_READ_BIT |
                                   VK_ACCESS_SHADER_WRITE_BIT;
    transitions[i].srcQueueFamilyIndex = transitions[i].dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    transitions[i].image = images[i]->image;
    transitions[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                       layers[i]};
  }
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                       nullptr, 5, transitions);
  VkBufferImageCopy upload{};
  upload.bufferOffset = 16;
  upload.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
  upload.imageExtent = {5, 3, 1};
  VkBufferImageCopy parts[] = {upload, upload};
  parts[0].imageExtent.height = 1;
  parts[1].imageOffset.y = 1;
  parts[1].imageExtent.height = 2;
  parts[1].bufferOffset += 5 * 4 * sizeof(float);
  parts[0].bufferRowLength = parts[1].bufferRowLength = 5;
  parts[0].bufferImageHeight = parts[1].bufferImageHeight = 3;
  vkCmdCopyBufferToImage(command, a.buffer, source_array.image,
                         VK_IMAGE_LAYOUT_GENERAL, 2, parts);
  for (unsigned pass = 0; pass < 4; pass++) {
    if (pass == 1) {
      barrier(command, VK_ACCESS_TRANSFER_READ_BIT,
              VK_ACCESS_TRANSFER_WRITE_BIT);
      VkClearColorValue zero{};
      VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
      vkCmdClearColorImage(command, source_array.image, VK_IMAGE_LAYOUT_GENERAL,
                           &zero, 1, &range);
    }
    barrier(command, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy copy{};
    copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
    copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.extent = {5, 3, 2};
    bool reverse = pass & 1;
    if (reverse) std::swap(copy.srcSubresource, copy.dstSubresource);
    if (pass >= 2) {
      // Shift each direction so omitting either partial copy changes the
      // result.
      copy.srcOffset.x = 1;
      copy.extent.width--;
    }
    vkCmdCopyImage(command, reverse ? source3d.image : source_array.image,
                   VK_IMAGE_LAYOUT_GENERAL,
                   reverse ? source_array.image : source3d.image,
                   VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
  }
  barrier(command, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  VkBufferImageCopy echo{};
  echo.bufferOffset = 16 + image_count * 4;
  echo.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  echo.imageExtent = {5, 3, 2};
  vkCmdCopyImageToBuffer(command, source3d.image, VK_IMAGE_LAYOUT_GENERAL,
                         copied.buffer, 1, &echo);
  VkClearColorValue clear{{0.125f, 0.25f, 0.375f, 0.5f}};
  for (unsigned i = 2; i < 5; i++) {
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                  layers[i]};
    vkCmdClearColorImage(command, images[i]->image, VK_IMAGE_LAYOUT_GENERAL,
                         &clear, 1, &range);
  }
  const VkDeviceSize indirect_offset = 16 + offsetof(Metadata, dispatch);
  uint32_t groups = (n + workgroup - 1) / workgroup;
  vkCmdFillBuffer(command, metadata.buffer, indirect_offset + 4, 8, 1);
  vkCmdUpdateBuffer(command, metadata.buffer, indirect_offset, sizeof(groups),
                    &groups);
  barrier(command, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
              VK_ACCESS_SHADER_WRITE_BIT);
  const uint32_t offsets[] = {16, 16}, parameters[] = {n, 13, 17, 19};
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[0]);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 4,
                          sets, 2, offsets);
  vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(parameters), parameters);
  vkCmdDispatchIndirect(command, metadata.buffer, indirect_offset);
  barrier(command, VK_ACCESS_SHADER_WRITE_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[1]);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 3, 1,
                          &sets[4], 0, nullptr);
  const uint32_t extent[] = {5, 3, 2, 0};
  vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(extent), extent);
  vkCmdDispatch(command, 1, 1, 2);
  barrier(command, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  for (unsigned i = 2, offset = 0; i < 5; i++) {
    VkBufferImageCopy copy{};
    copy.bufferOffset = 16 + offset * 4;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers[i]};
    copy.imageExtent = {5, 3, i == 3 ? 2u : 1u};
    vkCmdCopyImageToBuffer(command, images[i]->image, VK_IMAGE_LAYOUT_GENERAL,
                           copied.buffer, 1, &copy);
    offset += i == 2 ? 60 : 120;
  }
  VkBufferCopy copy{16, query_offset + 48, 16};
  vkCmdCopyBuffer(command, output.buffer, copied.buffer, 1, &copy);
  vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries,
                      1);
  vkCmdCopyQueryPoolResults(command, queries, 0, 2, copied.buffer, query_offset,
                            16,
                            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT |
                                VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  vkCmdSetEvent(command, done, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  vkCmdResetEvent(command, gate, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  VK_CHECK(vkEndCommandBuffer(command));
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  VK_CHECK(vkAllocateCommandBuffers(device, &ai, &primary));
  bi.pInheritanceInfo = nullptr;
  VK_CHECK(vkBeginCommandBuffer(primary, &bi));
  vkCmdExecuteCommands(primary, 1, &command);
  VK_CHECK(vkEndCommandBuffer(primary));

  auto counter = reinterpret_cast<PFN_vkGetSemaphoreCounterValueKHR>(
      vkGetDeviceProcAddr(device, "vkGetSemaphoreCounterValueKHR"));
  auto signal = reinterpret_cast<PFN_vkSignalSemaphoreKHR>(
      vkGetDeviceProcAddr(device, "vkSignalSemaphoreKHR"));
  auto wait = reinterpret_cast<PFN_vkWaitSemaphoresKHR>(
      vkGetDeviceProcAddr(device, "vkWaitSemaphoresKHR"));
  VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  si.pNext = &type;
  VkSemaphore timeline, binary;
  VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &timeline));
  si.pNext = nullptr;
  VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &binary));
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence fence;
  VK_CHECK(vkCreateFence(device, &fi, nullptr, &fence));
  if (vkWaitForFences(device, 1, &fence, VK_TRUE, 0) != VK_TIMEOUT)
    throw std::runtime_error("unsignaled fence incorrectly completed");
  uint64_t one = 1, two = 2, value = 99;
  VkTimelineSemaphoreSubmitInfo ti{
      VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
  ti.waitSemaphoreValueCount = ti.signalSemaphoreValueCount = 1;
  ti.pWaitSemaphoreValues = &one;
  ti.pSignalSemaphoreValues = &two;
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.pNext = &ti;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &primary;
  submit.waitSemaphoreCount = submit.signalSemaphoreCount = 1;
  submit.pWaitSemaphores = submit.pSignalSemaphores = &timeline;
  submit.pWaitDstStageMask = &stage;
  // Set before submission: a host cannot signal an event waited on by a pending
  // CB.
  VK_CHECK(vkSetEvent(device, gate));
  VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
  VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
  wi.semaphoreCount = 1;
  wi.pSemaphores = &timeline;
  wi.pValues = &one;
  bool blocked = wait(device, &wi, 0) == VK_TIMEOUT &&
                 vkGetFenceStatus(device, fence) == VK_NOT_READY;
  VK_CHECK(counter(device, timeline, &value));
  std::fflush(stdout);
  FILE *printed = std::tmpfile();
  int saved_stdout = dup(STDOUT_FILENO);
  if (!printed || saved_stdout < 0 || dup2(fileno(printed), STDOUT_FILENO) < 0)
    throw std::runtime_error("cannot capture shader printf");
  VkSemaphoreSignalInfo signal_info{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
  signal_info.semaphore = timeline;
  signal_info.value = 1;
  VkResult signaled = signal(device, &signal_info);
  VkResult completed = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
  std::fflush(stdout);
  dup2(saved_stdout, STDOUT_FILENO);
  close(saved_stdout);
  std::rewind(printed);
  std::string text;
  char chunk[1024];
  size_t length;
  while ((length = std::fread(chunk, 1, sizeof(chunk), printed)) != 0)
    text.append(chunk, length);
  std::fclose(printed);
  check(signaled, "host timeline signal");
  check(completed, "smoke completion");
  if (!blocked || value != 0)
    throw std::runtime_error("timeline wait-before-signal failed");
  VK_CHECK(counter(device, timeline, &value));
  if (value != 2) throw std::runtime_error("timeline completion mismatch");
  if (std::count(text.begin(), text.end(), '\n') != n)
    throw std::runtime_error("shader printf record count mismatch");
  for (unsigned i = 0; i < n; i++)
    if (text.find("Formosa smoke id=" + std::to_string(i) + " value=1.25\n") ==
        std::string::npos)
      throw std::runtime_error("shader printf invocation missing: " +
                               std::to_string(i));
  uint64_t timestamps[4];
  VK_CHECK(vkGetQueryPoolResults(
      device, queries, 0, 2, sizeof(timestamps), timestamps, 16,
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
  for (auto buffer : {&a, &b, &metadata, &output, &copied}) {
    buffer->transfer(false);
    buffer->verify_guards();
  }
  uint64_t unavailable_copy;
  std::memcpy(&unavailable_copy, copied.values() + query_index + 10, 8);
  if (copied.values()[query_index + 8] != -9999.0f ||
      copied.values()[query_index + 9] != -9999.0f || unavailable_copy != 0 ||
      timestamps[1] != 1 || timestamps[3] != 1 ||
      timestamps[2] <= timestamps[0] ||
      std::memcmp(copied.values() + query_index, timestamps,
                  sizeof(timestamps)) != 0 ||
      vkGetEventStatus(device, done) != VK_EVENT_SET ||
      vkGetEventStatus(device, gate) != VK_EVENT_RESET)
    throw std::runtime_error("query copy or event mismatch");
  VK_CHECK(vkResetEvent(device, done));
  if (vkGetEventStatus(device, done) != VK_EVENT_RESET)
    throw std::runtime_error("event reset failed");
  VkSubmitInfo chain[2]{};
  for (auto &entry : chain) entry.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  chain[0].signalSemaphoreCount = chain[1].waitSemaphoreCount = 1;
  chain[0].pSignalSemaphores = chain[1].pWaitSemaphores = &binary;
  chain[1].pWaitDstStageMask = &stage;
  for (unsigned round = 0; round < 2; round++) {
    VK_CHECK(vkResetFences(device, 1, &fence));
    if (vkGetFenceStatus(device, fence) != VK_NOT_READY)
      throw std::runtime_error("fence reset failed");
    VK_CHECK(vkQueueSubmit(queue, 2, chain, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
  }

  std::memcpy(&data, metadata.values(), sizeof(data));
  if (data.counter != n || data.wide != uint64_t(n) * ((uint64_t(1) << 32) + 1))
    throw std::runtime_error("global atomic counter mismatch");
  std::vector<bool> seen(n);
  for (unsigned id = 0; id < n; id++) {
    const float *actual = output.values() + id * 21;
    unsigned lid = id % workgroup, first = id - lid % width,
             last = std::min({first + width, id - lid + workgroup, n});
    float v = std::abs(input(0, id)) + 0.25f;
    float expected[20] = {
        input(0, id) + input(1, id),
        scale * (std::exp(v) + std::log(v) + std::sin(v) + std::cos(v)),
        input(0, id) + 1.0f,
        float(id + 7),
        float(lid / width),
        float(lid % width),
        float((workgroup + width - 1) / width),
        float(std::min(workgroup, n - id / workgroup * workgroup)),
        actual[8],
        0,
        1,
        0,
        float(id == first),
        0,
        float(first + 17)};
    for (unsigned j = 0; j < 13; j++)
      if (j % 3 != 0)
        expected[11] +=
            (input((id / workgroup) & 1u, id) + ((j + id) % 17) * 0.125f) *
            0.5f;
    unsigned odd = first;
    while (odd < last && ((odd % workgroup) & 1u) == 0) odd++;
    expected[13] = id == odd;
    for (unsigned round = 0; round < 3; round++) {
      unsigned active = first;
      while (active < last && active % 3 > round) active++;
      if (id == active) expected[15] += 1u << round;
    }
    int byte = int(id % 61) - 30 + int(id % 7) - 3;
    int short_value = ((int(id % 141) - 70) * 3 - 7) / 5;
    expected[16] = byte * 257 + short_value;
    expected[17] = 1;
    for (unsigned j = id / workgroup * workgroup;
         j < std::min(n, id / workgroup * workgroup + workgroup); j++)
      expected[18] += input(0, j);
    if (id < 13 * 17)
      for (unsigned k = 0; k < 19; k++)
        expected[19] += input(0, id / 17 * 19 + k) * input(1, k * 17 + id % 17);
    if (!std::isfinite(actual[8]) || actual[8] < 0 || actual[8] >= n ||
        std::floor(actual[8]) != actual[8] || seen[unsigned(actual[8])])
      throw std::runtime_error("atomic return values are not a permutation");
    seen[unsigned(actual[8])] = true;
    for (unsigned field = 0; field < 20; field++)
      expect(actual[field], expected[field], "compute", id * 21 + field);
    uint32_t checksum = subgroup_checksum(id, width, n);
    if (std::memcmp(actual + 20, &checksum, sizeof(checksum)))
      throw std::runtime_error("subgroup collective mismatch");
    if (data.touched[id] != id + 1 || data.words[id] != 12 ||
        data.bytes[id] != byte || data.shorts[id] != short_value)
      throw std::runtime_error("atomic or packed integer store mismatch");
  }
  for (unsigned i = n; i < n + 3; i++)
    if (uint8_t(data.bytes[i]) != 0xa5 || uint16_t(data.shorts[i]) != 0x5a5a)
      throw std::runtime_error("packed integer padding overwritten");
  for (unsigned kind = 0, base = 0; kind < 3; kind++) {
    for (unsigned i = 0; i < (kind ? 120u : 60u); i++) {
      unsigned channel = i % 4, pixel = i / 4, x = pixel % 5, y = pixel / 5 % 3,
               z = pixel / 15;
      float expected = 0.125f * (channel + 1);
      if ((pixel & 1u) == 0) {
        expected = 0.5f * (channel + 1);
        float sx = float(x) - 0.25f, sy = float(y) + 0.25f,
              sz = kind == 1 ? float(z) - 0.25f : 0;
        int x0 = int(std::floor(sx)), y0 = int(std::floor(sy)),
            z0 = int(std::floor(sz));
        float fx = sx - x0, fy = sy - y0, fz = sz - z0;
        for (int dz = 0; dz < (kind == 1 ? 2 : 1); dz++)
          for (int dy = 0; dy < 2; dy++)
            for (int dx = 0; dx < 2; dx++) {
              unsigned xx = std::clamp(x0 + dx, 0, 4),
                       yy = std::clamp(y0 + dy, 0, 2);
              unsigned zz = kind == 2   ? z
                            : kind == 1 ? std::clamp(z0 + dz, 0, 1)
                                        : 0;
              unsigned source_x = std::min(xx + (kind == 1 ? 1u : 2u), 4u);
              expected +=
                  input(0, ((zz * 3 + yy) * 5 + source_x) * 4 + channel) *
                  (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) *
                  (kind == 1 ? (dz ? fz : 1 - fz) : 1);
            }
      }
      expect(output.values()[math_count + base + i], expected, "image",
             base + i);
      expect(copied.values()[base + i], expected, "image copy", base + i);
    }
    base += kind ? 120 : 60;
  }
  for (unsigned i = 0; i < a.count; i++)
    expect(a.values()[i], input(0, i), "input A", i);
  for (unsigned i = 0; i < b.count; i++)
    expect(b.values()[i], input(1, i), "input B", i);
  for (unsigned i = 0; i < 120; i++) {
    unsigned source = (i / 20) * 20 + std::min(i / 4 % 5 + 1, 4u) * 4 + i % 4;
    expect(copied.values()[image_count + i], input(0, source), "image transfer",
           i);
  }
  for (unsigned i = 0; i < texel_count; i++)
    expect(output.values()[math_count + image_count + i],
           input(0, i) + 0.25f * (i % 4 + 1), "texel", i);
  for (unsigned i = 0; i < 4; i++)
    expect(copied.values()[query_index + 12 + i], output.values()[i],
           "buffer copy", i);
  vkDestroyFence(device, fence, nullptr);
  vkDestroySemaphore(device, timeline, nullptr);
  vkDestroySemaphore(device, binary, nullptr);
  vkDestroyEvent(device, gate, nullptr);
  vkDestroyEvent(device, done, nullptr);
  vkDestroyQueryPool(device, queries, nullptr);
  vkDestroyCommandPool(device, pool, nullptr);
  vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
  for (auto pipeline : pipelines) vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroyPipelineLayout(device, layout, nullptr);
  for (unsigned i = 0; i < 4; i++)
    vkDestroyDescriptorSetLayout(device, layouts[i], nullptr);
  for (auto view : texels) vkDestroyBufferView(device, view, nullptr);
  for (auto sampler : samplers) vkDestroySampler(device, sampler, nullptr);
  vkDestroyImageView(device, source2d, nullptr);
  std::puts(
      "smoke PASS compute, images, descriptors, transfers, synchronization, "
      "queries, printf");
}

static void run_device_lost(VkDevice device, VkQueue queue,
                            const std::string &shader_path) {
  VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  VkPipelineLayout layout;
  VK_CHECK(vkCreatePipelineLayout(device, &li, nullptr, &layout));
  VkPipeline pipeline;
  VK_CHECK(create_pipeline(device, layout, shader_path, &pipeline));
  VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  VkCommandPool pool;
  VK_CHECK(vkCreateCommandPool(device, &ci, nullptr, &pool));
  VkCommandBufferAllocateInfo ai{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = pool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer command;
  VK_CHECK(vkAllocateCommandBuffers(device, &ai, &command));
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = 1;
  VkQueryPool queries;
  VK_CHECK(vkCreateQueryPool(device, &qi, nullptr, &queries));
  auto reset = reinterpret_cast<PFN_vkResetQueryPoolEXT>(
      vkGetDeviceProcAddr(device, "vkResetQueryPoolEXT"));
  reset(device, queries, 0, 1);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  VK_CHECK(vkBeginCommandBuffer(command, &bi));
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  // Each dimension is legal; the native launch rejects this total grid size.
  vkCmdDispatch(command, 65535, 65535, 1);
  vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries,
                      0);
  VK_CHECK(vkEndCommandBuffer(command));
  VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  si.pNext = &type;
  VkSemaphore semaphore;
  VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &semaphore));
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence fence;
  VK_CHECK(vkCreateFence(device, &fi, nullptr, &fence));
  uint64_t one = 1, two = 2;
  VkTimelineSemaphoreSubmitInfo timeline{
      VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
  timeline.waitSemaphoreValueCount = timeline.signalSemaphoreValueCount = 1;
  timeline.pWaitSemaphoreValues = &one;
  timeline.pSignalSemaphoreValues = &two;
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.pNext = &timeline;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command;
  submit.waitSemaphoreCount = submit.signalSemaphoreCount = 1;
  submit.pWaitSemaphores = submit.pSignalSemaphores = &semaphore;
  submit.pWaitDstStageMask = &stage;
  VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
  std::atomic<unsigned> entered{0};
  auto fence_wait = std::async(std::launch::async, [&] {
    ++entered;
    return vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
  });
  auto query_wait = std::async(std::launch::async, [&] {
    uint64_t value;
    ++entered;
    return vkGetQueryPoolResults(
        device, queries, 0, 1, sizeof(value), &value, sizeof(value),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
  });
  auto semaphore_wait = std::async(std::launch::async, [&] {
    VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wi.semaphoreCount = 1;
    wi.pSemaphores = &semaphore;
    wi.pValues = &two;
    auto wait = reinterpret_cast<PFN_vkWaitSemaphoresKHR>(
        vkGetDeviceProcAddr(device, "vkWaitSemaphoresKHR"));
    ++entered;
    return wait(device, &wi, UINT64_MAX);
  });
  while (entered != 3) std::this_thread::yield();
  // Give all three calls time to enter the driver before releasing the queue.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  VkSemaphoreSignalInfo signal_info{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
  signal_info.semaphore = semaphore;
  signal_info.value = 1;
  auto signal = reinterpret_cast<PFN_vkSignalSemaphoreKHR>(
      vkGetDeviceProcAddr(device, "vkSignalSemaphoreKHR"));
  VK_CHECK(signal(device, &signal_info));
  if (fence_wait.wait_for(std::chrono::seconds(5)) !=
          std::future_status::ready ||
      query_wait.wait_for(std::chrono::seconds(5)) !=
          std::future_status::ready ||
      semaphore_wait.wait_for(std::chrono::seconds(5)) !=
          std::future_status::ready) {
    std::fputs("device-lost wait did not terminate\n", stderr);
    // A future destructor would join the stuck thread and hide the failure.
    std::_Exit(1);
  }
  const VkResult fence_result = fence_wait.get(),
                 query_result = query_wait.get(),
                 semaphore_result = semaphore_wait.get();
  vkDestroyFence(device, fence, nullptr);
  vkDestroySemaphore(device, semaphore, nullptr);
  vkDestroyQueryPool(device, queries, nullptr);
  vkDestroyCommandPool(device, pool, nullptr);
  vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroyPipelineLayout(device, layout, nullptr);
  if (fence_result != VK_ERROR_DEVICE_LOST ||
      query_result != VK_ERROR_DEVICE_LOST ||
      semaphore_result != VK_ERROR_DEVICE_LOST)
    throw std::runtime_error("device loss did not terminate all waits");
  std::puts(
      "device-lost PASS fence, query and timeline waits returned device lost");
}

int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  try {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Formosa Vulkan compute regression";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    VkInstance discovery;
    VK_CHECK(vkCreateInstance(&info, nullptr, &discovery));
    uint32_t discovered = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(discovery, &discovered, nullptr));
    vkDestroyInstance(discovery, nullptr);
    VK_CHECK(vkCreateInstance(&info, nullptr, &instance));
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    for (auto candidate : devices) {
      VkPhysicalDeviceProperties properties;
      vkGetPhysicalDeviceProperties(candidate, &properties);
      if (std::strstr(properties.deviceName, "Formosa")) {
        physical = candidate;
        std::printf("device=%s max_workgroup=%u\n", properties.deviceName,
                    properties.limits.maxComputeWorkGroupInvocations);
        break;
      }
    }
    if (!physical) throw std::runtime_error("Formosa Vulkan device not found");
    VkInstance second_instance = VK_NULL_HANDLE;
    VK_CHECK(vkCreateInstance(&info, nullptr, &second_instance));
    uint32_t second_count = 0;
    VK_CHECK(
        vkEnumeratePhysicalDevices(second_instance, &second_count, nullptr));
    vkDestroyInstance(second_instance, nullptr);
    if (second_count != count)
      throw std::runtime_error("multiple instance enumeration mismatch");
    VkPhysicalDeviceSubgroupProperties subgroup{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties2.pNext = &subgroup;
    vkGetPhysicalDeviceProperties2(physical, &properties2);
    std::printf("subgroup_width=%u\n", subgroup.subgroupSize);
    if (!subgroup.subgroupSize)
      throw std::runtime_error("subgroup properties missing");
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
      if (memory_properties.memoryTypes[i].propertyFlags &
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        memory_type = i;
        break;
      }
    if (memory_type == UINT32_MAX)
      throw std::runtime_error("host-visible memory not found");
    float priority = 1;
    VkDeviceQueueCreateInfo queue_info{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceFeatures features{};
    features.shaderFloat64 = VK_TRUE;
    features.shaderInt64 = VK_TRUE;
    features.shaderInt16 = VK_TRUE;
    features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    device_info.pEnabledFeatures = &features;
    VkPhysicalDeviceShaderAtomicInt64Features atomics{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES};
    atomics.shaderBufferInt64Atomics = VK_TRUE;
    atomics.shaderSharedInt64Atomics = VK_TRUE;
    VkPhysicalDeviceShaderClockFeaturesKHR clocks{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR};
    clocks.shaderSubgroupClock = VK_TRUE;
    clocks.shaderDeviceClock = VK_TRUE;
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    timeline.timelineSemaphore = VK_TRUE;
    VkPhysicalDeviceHostQueryResetFeatures reset{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES};
    reset.hostQueryReset = VK_TRUE;
    VkPhysicalDevice8BitStorageFeatures bytes{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES};
    bytes.storageBuffer8BitAccess = VK_TRUE;
    VkPhysicalDevice16BitStorageFeatures shorts{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    shorts.storageBuffer16BitAccess = VK_TRUE;
    VkPhysicalDeviceShaderFloat16Int8Features integers{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    integers.shaderInt8 = VK_TRUE;
    reset.pNext = &bytes;
    bytes.pNext = &shorts;
    shorts.pNext = &integers;
    timeline.pNext = &reset;
    clocks.pNext = &timeline;
    atomics.pNext = &clocks;
    device_info.pNext = &atomics;
    const char *extensions[] = {
        "VK_KHR_shader_atomic_int64", "VK_KHR_shader_clock",
        "VK_KHR_timeline_semaphore",  "VK_KHR_shader_non_semantic_info",
        "VK_EXT_host_query_reset",    "VK_KHR_8bit_storage",
        "VK_KHR_16bit_storage",       "VK_KHR_shader_float16_int8"};
    device_info.enabledExtensionCount =
        sizeof(extensions) / sizeof(extensions[0]);
    device_info.ppEnabledExtensionNames = extensions;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    VK_CHECK(vkCreateDevice(physical, &device_info, nullptr, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);
    const std::string directory = argc > 1 ? argv[1] : ".";
    const std::string mode = argc > 2 ? argv[2] : "smoke";
    const bool device_lost = mode == "device-lost";
    if (device_lost) {
      run_device_lost(device, queue, directory + "/empty.spv");
    } else if (mode == "smoke" || mode == "positive") {
      run_smoke(device, queue, memory_type, directory, subgroup.subgroupSize,
                properties2.properties.limits);
    } else if (mode == "unsupported") {
      VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           1, VK_SHADER_STAGE_COMPUTE_BIT,
                                           nullptr};
      VkDescriptorSetLayoutCreateInfo si{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      si.bindingCount = 1;
      si.pBindings = &binding;
      VkDescriptorSetLayout set_layout;
      VK_CHECK(vkCreateDescriptorSetLayout(device, &si, nullptr, &set_layout));
      VkPipelineLayoutCreateInfo li{
          VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
      li.setLayoutCount = 1;
      li.pSetLayouts = &set_layout;
      VkPipelineLayout layout;
      VK_CHECK(vkCreatePipelineLayout(device, &li, nullptr, &layout));
      VkPipeline pipeline = VK_NULL_HANDLE;
      VkResult result = create_pipeline(
          device, layout, directory + "/unsupported.spv", &pipeline);
      if (result == VK_SUCCESS || pipeline != VK_NULL_HANDLE)
        throw std::runtime_error(
            "unsupported float atomics unexpectedly compiled");
      vkDestroyPipelineLayout(device, layout, nullptr);
      vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
      std::puts("unsupported PASS float atomic shader rejected");
    } else
      throw std::runtime_error("unknown Vulkan test mode");
    if (!device_lost) VK_CHECK(vkDeviceWaitIdle(device));
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    if (device) vkDestroyDevice(device, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    return 1;
  }
}

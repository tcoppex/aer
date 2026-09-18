#include "chunk_grid.h"

#include "aer/core/utils.h"

namespace shader_interop {
#include "shaders/interop.h"
}

// ----------------------------------------------------------------------------

void ChunkGrid::setup(RenderContext const& context, uint3 const& dimension) {
  context_ptr_ = &context;

  reset(dimension);

  buffers_.vertex = context.createBuffer(
    "ChunkGrid::VertexBuffer",
    size_ * kHeuristicChunkVerticesBufferSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    // | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
  );

  buffers_.index = context.createBuffer(
    "ChunkGrid::IndexBuffer",
    size_ * kHeuristicChunkIndicesBufferSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
  );

  buffers_.draw_indirect = context.createBuffer(
    "ChunkGrid::DrawIndirectBuffer",
    size_ * kDrawIndexedIndirectSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT //
    | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
    ,
    VMA_MEMORY_USAGE_AUTO,
    backend::Allocator::kAllocMappedAtCreation
  );
  if (auto &buf = buffers_.draw_indirect; buf.is_mapped()) {
    uint32_t *data = reinterpret_cast<uint32_t*>(buf.mapped_data);
    memset(data, 0u, size_ * kDrawIndexedIndirectSize);
    for (size_t i = 0; i < size_; ++i) {
      data[5u * i + 1u] = 1u;
    }
    context.flushBuffer(buf);
  }

  // buffers_.chunk = context.createBuffer(
  //   "ChunkGrid::ChunkBuffer",
  //   size_ * kChunkStride,
  //     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
  //   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
  //   VMA_MEMORY_USAGE_AUTO,
  //   backend::Allocator::kAllocMappedAtCreation
  // );
  // if (auto &buf = buffers_.chunk; buf.is_mapped()) {
  //   float4 *attributes = reinterpret_cast<float4*>(buf.mapped_data);
  //   for (size_t i = 0; i < chunks_.size(); ++i) {
  //     attributes[i] = float4(chunks_[i].worldspace_coords(), shader_interop::kChunkSize);
  //   }
  //   context.flushBuffer(buf);
  // }
}

// ----------------------------------------------------------------------------

void ChunkGrid::release() {
  LOG_CHECK(context_ptr_ != nullptr);

  context_ptr_->destroyResources(
    buffers_.vertex,
    buffers_.index,
    buffers_.draw_indirect
    // buffers_.chunk
  );
}

// ----------------------------------------------------------------------------

void ChunkGrid::draw(RenderPassEncoder const& pass) const {
  for (auto const& chunk : chunks_) {
    auto const& offset = chunk.offsets();
    pass.bindVertexBuffer(buffers_.vertex, 0u, offset.vertex);
    pass.bindIndexBuffer(buffers_.index, VK_INDEX_TYPE_UINT32, offset.index);
    pass.drawIndexedIndirect(buffers_.draw_indirect, offset.draw_indirect);
  }
}

// ----------------------------------------------------------------------------

void ChunkGrid::reset(uint3 const& dimension) {
  dimension_ = dimension;
  auto [X, Y, Z] = dimension;

  size_ = X * Y * Z;
  chunks_.clear();
  chunks_.reserve(size_);

  auto const startPosition = -0.5f * float3(X, Y, Z);

  size_t index = 0;
  for (uint32_t k = 0; k < Z; ++k) {
    for (uint32_t j = 0; j < Y; ++j) {
      for (uint32_t i = 0; i < X; ++i) {
        auto const coords   = uint3(i, j, k);
        auto const coordsWS = shader_interop::kChunkSize * (startPosition + float3(coords));
        auto const offsets  = Chunk::Offsets{
          .vertex         = static_cast<uint32_t>(index * kHeuristicChunkVerticesBufferSize),
          .index          = static_cast<uint32_t>(index * kHeuristicChunkIndicesBufferSize),
          .draw_indirect  = static_cast<uint32_t>(index * kDrawIndexedIndirectSize),
          // .chunk          = static_cast<uint32_t>(index * kChunkStride),
        };
        chunks_.emplace_back(index++, coords, coordsWS, offsets);
      }
    }
  }

  if (buffers_.allocated()) {
    release();
  }
}

// ----------------------------------------------------------------------------

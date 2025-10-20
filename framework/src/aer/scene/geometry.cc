#include "aer/scene/geometry.h"

#include <cmath>
#include <cassert>
#include <cstring>

#include <algorithm>
#include <array>
#include <numeric>

#include "mikktspace.h"

/* -------------------------------------------------------------------------- */

namespace {

static float constexpr kPi = M_PI;
static float constexpr kTwoPi = 2.0f * kPi;

/* Default interleaved attributes used to construct most geometries internally. */
struct Vertex_t {
  std::array<float, 3> position;
  std::array<float, 3> normal;
  std::array<float, 2> texcoord;
};

/* Attribute used by point list. */
struct Point_t {
  std::array<float, 4> position;
};

}

/* -------------------------------------------------------------------------- */

void Geometry::MakeCube(Geometry &geo, float size) {
  float s = size / 2.0f;

  std::array<decltype(Vertex_t::position), 8u> const positions{
    +s, +s, +s,
    +s, -s, +s,
    +s, -s, -s,
    +s, +s, -s,
    -s, +s, +s,
    -s, -s, +s,
    -s, -s, -s,
    -s, +s, -s,
  };

  std::array<decltype(Vertex_t::normal), 6u> const normals{
     1.0,  0.0,  0.0,
    -1.0,  0.0,  0.0,
     0.0,  1.0,  0.0,
     0.0, -1.0,  0.0,
     0.0,  0.0,  1.0,
     0.0,  0.0, -1.0,
  };

  std::array<decltype(Vertex_t::texcoord), 4u> const texcoords{
    0.0, 1.0,
    1.0, 1.0,
    0.0, 0.0,
    1.0, 0.0,
  };

  std::array<uint8_t[3], 24u>  const vertexAttributesIndices{
    0, 0, 0,  1, 0, 2,  2, 0, 3,  3, 0, 1,
    4, 4, 0,  5, 4, 2,  1, 4, 3,  0, 4, 1,
    7, 1, 0,  6, 1, 2,  5, 1, 3,  4, 1, 1,
    3, 5, 0,  2, 5, 2,  6, 5, 3,  7, 5, 1,
    7, 2, 0,  4, 2, 2,  0, 2, 3,  3, 2, 1,
    5, 3, 0,  6, 3, 2,  2, 3, 3,  1, 3, 1,
  };

  std::array<uint16_t, 36u> trilistIndices{
    0, 1, 2,  0, 2, 3,
    4, 5, 6,  4, 6, 7,
    8, 9, 10,  8, 10, 11,
    12, 13, 14,  12, 14, 15,
    16, 17, 18,  16, 18, 19,
    20, 21, 22,  20, 22, 23,
  };

  // --------

  geo.set_index_format(IndexFormat::U16);
  geo.set_topology(Topology::TriangleList);

  geo.addIndicesData(std::as_bytes(std::span(trilistIndices)));

  geo.addPrimitive({
    .vertexCount = static_cast<uint32_t>(vertexAttributesIndices.size()),
    .indexCount = static_cast<uint32_t>(trilistIndices.size()),
    .bufferOffsets = {
      {AttributeType::Position, 0ul},
      {AttributeType::Normal, 0ul},
      {AttributeType::Texcoord, 0ul}
    }
  });

  geo.attributes_ = {
    {
      AttributeType::Position,
      {
        .format = AttributeFormat::RGB_F32,
        .offset = offsetof(Vertex_t, position),
        .stride = sizeof(Vertex_t),
      }
    },
    {
      AttributeType::Normal,
      {
        .format = AttributeFormat::RGB_F32,
        .offset = offsetof(Vertex_t, normal),
        .stride = sizeof(Vertex_t),
      }
    },
    {
      AttributeType::Texcoord,
      {
        .format = AttributeFormat::RG_F32,
        .offset = offsetof(Vertex_t, texcoord),
        .stride = sizeof(Vertex_t),
      }
    },
  };

  auto& v = geo.vertices_;
  v.clear();
  v.reserve(vertexAttributesIndices.size() * sizeof(Vertex_t));
  for (auto const i : vertexAttributesIndices) {
    auto pos_ptr = reinterpret_cast<std::byte const*>(&positions[i[0]]);
    auto nor_ptr = reinterpret_cast<std::byte const*>(&normals[i[1]]);
    auto uv_ptr = reinterpret_cast<std::byte const*>(&texcoords[i[2]]);
    v.insert(v.end(), pos_ptr, pos_ptr + sizeof(Vertex_t::position));
    v.insert(v.end(), nor_ptr, nor_ptr + sizeof(Vertex_t::normal));
    v.insert(v.end(), uv_ptr, uv_ptr + sizeof(Vertex_t::texcoord));
  }
}

// ----------------------------------------------------------------------------

void Geometry::MakePlane(Geometry &geo, float size, uint32_t resx, uint32_t resy) {
  uint32_t const ncols = (resx + 1u);
  uint32_t const nrows = (resy + 1u);
  uint32_t const vertex_count = nrows * ncols;

  /* Set up the buffer of raw vertices. */
  std::vector<Vertex_t> vertices(vertex_count);
  {
    float const dx = 1.0f / static_cast<float>(ncols - 1u);
    float const dy = 1.0f / static_cast<float>(nrows - 1u);

    uint32_t v_index = 0u;
    for (uint32_t iy = 0u; iy < nrows; ++iy) {
      for (uint32_t ix = 0u; ix < ncols; ++ix, ++v_index) {
        float const uv_x = static_cast<float>(ix) * dx;
        float const uv_y = static_cast<float>(iy) * dy;

        vertices[v_index] = {
          .position = { size * (uv_x - 0.5f), 0.0f, size * (uv_y - 0.5f) },
          .normal = { 0.0f, 1.0f, 0.0f },
          .texcoord = { uv_x, 1.0f - uv_y }
        };
      }
    }
  }

  /* Set up triangle strip indices. */
  uint32_t const index_count = ((resx + 1u) * 2u) * resy + (resy - 1u) * 2u;
  std::vector<uint32_t> indices(index_count);
  {
    uint32_t index = 0u;
    for (uint32_t iy = 0u; iy < resy; ++iy) {
      for (uint32_t ix = 0u; ix < resx + 1u; ++ix) {
        uint32_t const v_index = (ncols * (iy + 1u)) - (ix + 1);
        indices[index++] = v_index + ncols;
        indices[index++] = v_index;
      }
      if (iy < resy - 1u) {
        indices[index] = indices[index - 1u];
        indices[index + 1u] = indices[index] + resx;
        index += 2u;
      }
    }
    assert(index == index_count);
  }

  /// --------------

  geo.set_index_format(IndexFormat::U32);
  geo.set_topology(Topology::TriangleStrip);

  geo.addPrimitive({
    .vertexCount = vertex_count,
    .indexCount = index_count,
    .bufferOffsets = {
      {AttributeType::Position, 0ul},
      {AttributeType::Normal, 0ul},
      {AttributeType::Texcoord, 0ul}
    }
  });

  geo.attributes_ = {
    {
      AttributeType::Position,
      {
        .format = AttributeFormat::RGB_F32,
        .offset = offsetof(Vertex_t, position),
        .stride = sizeof(Vertex_t),
      }
    },
    {
      AttributeType::Normal,
      {
        .format = AttributeFormat::RGB_F32,
        .offset = offsetof(Vertex_t, normal),
        .stride = sizeof(Vertex_t),
      }
    },
    {
      AttributeType::Texcoord,
      {
        .format = AttributeFormat::RG_F32,
        .offset = offsetof(Vertex_t, texcoord),
        .stride = sizeof(Vertex_t),
      }
    },
  };

  geo.addVerticesData(std::as_bytes(std::span(vertices)));
  geo.addIndicesData(std::as_bytes(std::span(indices)));
}

// ----------------------------------------------------------------------------

void Geometry::MakeSphere(Geometry &geo, float radius, uint32_t resx, uint32_t resy) {
  assert( resx > 3u && resy > 2u );
  assert( radius > 0.0f );

  uint32_t const ncols = resx + 1u;
  uint32_t const nrows = resy + 1u;

  uint32_t const vertex_count = 2u + (nrows - 2u) * ncols;
  std::vector<Vertex_t> vertices(vertex_count);

  // Vertices data.
  if (!vertices.empty()) // (disable a warning)
  {
    auto const dx = 1.0f / static_cast<float>(resx);
    auto const dy = 1.0f / static_cast<float>(resy);

    uint32_t vertex_id = 0u;

    vertices[vertex_id++] = {
      .position = { 0.0f, -radius, 0.0f },
      .normal = { 0.0f, -1.0f, 0.0f },
      .texcoord = { 0.0f, 0.0f },
    };

    for (uint32_t j = 1u; j < nrows-1u; ++j) {
      auto const dj = static_cast<float>(j) * dy;

      auto const theta = (dj - 0.5f) * kPi;
      auto const ct = cosf(theta);
      auto const st = sinf(theta);

      for (uint32_t i = 0u; i < ncols; ++i) {
        auto const di = static_cast<float>(i) * dx;

        auto const phi = di * kTwoPi;
        auto const cp = cosf(phi);
        auto const sp = sinf(phi);

        auto nx = ct * cp;
        auto ny = st;
        auto nz = ct * sp;

        // float dlen = 1.0f / sqrtf(nx*nx + ny*ny + nz*nz);
        // nx *= dlen;
        // ny *= dlen;
        // nz *= dlen;

        vertices[vertex_id++] = {
          .position = { radius * nx, radius * ny, radius * nz },
          .normal = { nx, ny, nz },
          .texcoord = { di, dj },
        };
      }
    }

    vertices[vertex_id++] = {
      .position = { 0.0f, +radius, 0.0f },
      .normal = { 0.0f, 1.0f, 0.0f },
      .texcoord = { 1.0f, 1.0f },
    };

    assert(vertex_id == vertex_count);
  }

  // Indices.
  uint32_t const index_count = 2u * (nrows - 3u) + 2u * ncols * (nrows - 1u);
  std::vector<uint32_t> indices(index_count);
  {
    uint32_t index = 0u;

    for (uint32_t i = 0u; i < ncols; ++i) {
      indices[index++] = 0u;
      indices[index++] = i + 1u;
    }

    for (uint32_t j = 1u; j < nrows-2u; ++j) {
      uint32_t base_index = indices[index-1u];

      indices[index++] = base_index;
      indices[index++] = base_index;

      base_index = base_index - ncols + 1;

      for (uint32_t i = 0u; i < ncols; ++i) {
        indices[index++] = base_index + i;
        indices[index++] = base_index + i + ncols;
      }
    }

    for (uint32_t i = 0u; i < ncols; ++i) {
      indices[index++] = vertex_count - 1u - ncols + i;
      indices[index++] = vertex_count - 1u;
    }

    // assert(index == geo.index_count);
  }

  /// --------------

  geo.set_index_format(IndexFormat::U32);
  geo.set_topology(Topology::TriangleStrip);

  geo.addPrimitive({
    .vertexCount = vertex_count,
    .indexCount = index_count,
    .bufferOffsets = {
      {AttributeType::Position, 0ul},
      {AttributeType::Normal, 0ul},
      {AttributeType::Texcoord, 0ul}
    }
  });

  geo.attributes_ = {
    {
      AttributeType::Position,
      {
        .format = AttributeFormat::RGB_F32,
        .offset = offsetof(Vertex_t, position),
        .stride = sizeof(Vertex_t),
      }
    },
    {
      AttributeType::Normal,
      {
        .format = AttributeFormat::RGB_F32,
        .offset = offsetof(Vertex_t, normal),
        .stride = sizeof(Vertex_t),
      }
    },
    {
      AttributeType::Texcoord,
      {
        .format = AttributeFormat::RG_F32,
        .offset = offsetof(Vertex_t, texcoord),
        .stride = sizeof(Vertex_t),
      }
    },
  };

  geo.addVerticesData(std::as_bytes(std::span(vertices)));
  geo.addIndicesData(std::as_bytes(std::span(indices)));
}

// ----------------------------------------------------------------------------

void Geometry::MakeTorus(Geometry &geo, float major_radius, float minor_radius, uint32_t resx, uint32_t resy) {
  uint32_t const ncols = (resx + 1u);
  uint32_t const nrows = (resy + 1u);
  uint32_t const vertex_count = nrows * ncols;

  // The torus is made as a plane rolled around the outer_ring.

  /* Set up the buffer of raw vertices. */
  std::vector<Vertex_t> vertices(vertex_count);
  {
    auto const dx = 1.0f / static_cast<float>(ncols - 1u);
    auto const dy = 1.0f / static_cast<float>(nrows - 1u);

    //auto const cosine_step_theta = cos(dy * kTwoPi);
    //auto const sine_step_theta = sin(dy * kTwoPi);
    //auto const cosine_step_phi = cos(dx * kTwoPi);
    //auto const sine_step_phi = sin(dx * kTwoPi);

    uint32_t v_index = 0u;
    for (uint32_t iy = 0u; iy < nrows; ++iy) {
      auto const uv_y = static_cast<float>(iy) * dy;
      auto const theta = uv_y * kTwoPi;

      auto cosine_theta = cosf(theta);
      auto sine_theta = sinf(theta);

      // Rotate minor ring position (1, 0, 0) on Z-axis.
      auto const b_x = cosine_theta * minor_radius + major_radius;
      auto const b_y = sine_theta * minor_radius;

      for (uint32_t ix = 0u; ix < ncols; ++ix, ++v_index) {
        auto const uv_x = static_cast<float>(ix) * dx;
        auto const phi = uv_x * kTwoPi;

        auto const cosine_phi = cosf(phi);
        auto const sine_phi = sinf(phi);

        // Rotate major ring position on Y-axis.
        auto const a_x = cosine_phi * b_x;
        auto const a_y = b_y;
        auto const a_z = - sine_phi * b_x;

        auto const nx = cosine_phi * cosine_theta;
        auto const ny = sine_theta;
        auto const nz = - sine_phi * cosine_theta;

        vertices[v_index] = {
          .position = { a_x, a_y, a_z },
          .normal = { nx, ny, nz },
          .texcoord = { uv_x, uv_y }
        };
      }
    }
  }

  /* Set up triangle strip indices. */
  uint32_t const index_count = ((resx + 1u) * 2u) * resy + (resy - 1u) * 2u;
  std::vector<uint32_t> indices(index_count);
  {
    uint32_t index = 0u;
    for (uint32_t iy = 0u; iy < resy; ++iy) {
      for (uint32_t ix = 0u; ix < resx + 1u; ++ix) {
        uint32_t const v_index = (ncols * (iy + 1u)) - (ix + 1);
        indices[index++] = v_index;
        indices[index++] = v_index + ncols;
      }
      if (iy < resy - 1u) {
        indices[index] = indices[index - 1u];
        indices[index + 1u] = indices[index] + resx;
        index += 2u;
      }
    }
    assert(index == index_count);
  }

  /// --------------

  geo.set_index_format(IndexFormat::U32);
  geo.set_topology(Topology::TriangleStrip);

  geo.addPrimitive({
    .vertexCount = vertex_count,
    .indexCount = index_count,
    .bufferOffsets = {
      {AttributeType::Position, 0ul},
      {AttributeType::Normal, 0ul},
      {AttributeType::Texcoord, 0ul}
    }
  });

  geo.attributes_ = {
    {
      AttributeType::Position,
      {
        .format = AttributeFormat::RGB_F32,
        .offset = offsetof(Vertex_t, position),
        .stride = sizeof(Vertex_t),
      }
    },
    {
      AttributeType::Normal,
      {
        .format = AttributeFormat::RGB_F32,
        .offset = offsetof(Vertex_t, normal),
        .stride = sizeof(Vertex_t),
      }
    },
    {
      AttributeType::Texcoord,
      {
        .format = AttributeFormat::RG_F32,
        .offset = offsetof(Vertex_t, texcoord),
        .stride = sizeof(Vertex_t),
      }
    },
  };

  geo.addVerticesData(std::as_bytes(std::span(vertices)));
  geo.addIndicesData(std::as_bytes(std::span(indices)));
}

// ----------------------------------------------------------------------------

void Geometry::MakePointListPlane(Geometry &geo, float size, uint32_t resx, uint32_t resy) {
  uint32_t const ncols = resx;
  uint32_t const nrows = resy;
  uint32_t const vertex_count = nrows * ncols;

  std::vector<Point_t> vertices(vertex_count);
  {
    auto const dx = 1.0f / static_cast<float>(ncols - 1u);
    auto const dy = 1.0f / static_cast<float>(nrows - 1u);

    uint32_t v_index = 0u;
    for (uint32_t iy = 0u; iy < nrows; ++iy) {
      auto const uv_y = static_cast<float>(iy) * dy;
      for (uint32_t ix = 0u; ix < ncols; ++ix, ++v_index) {
        auto const uv_x = static_cast<float>(ix) * dx;
        vertices[v_index] = {
          .position = { size * (uv_x - 0.5f), 0.0f, size * (uv_y - 0.5f), 1.0f },
        };
      }
    }
  }

  std::vector<uint32_t> indices(vertex_count);
  std::iota(indices.begin(), indices.end(), 0u);

  /// --------------

  geo.set_index_format(IndexFormat::U32);
  geo.set_topology(Topology::PointList);

  geo.addPrimitive({
    .vertexCount = vertex_count,
    .indexCount = vertex_count,
    .bufferOffsets = {
      {AttributeType::Position, 0ul},
    }
  });

  geo.attributes_ = {
    {
      AttributeType::Position,
      {
        .format = AttributeFormat::RGBA_F32,
        .offset = offsetof(Point_t, position),
        .stride = sizeof(Point_t),
      }
    },
  };

  geo.addVerticesData(std::as_bytes(std::span(vertices)));
  geo.addIndicesData(std::as_bytes(std::span(indices)));
}

// ----------------------------------------------------------------------------

void Geometry::addPrimitive(Primitive const& primitive) {
  index_count_ += primitive.indexCount;
  vertex_count_ += primitive.vertexCount;
  primitives_.push_back(primitive);
}

uint64_t Geometry::addVerticesData(std::span<const std::byte> data) {
  uint64_t const offset = vertices_.size();
  vertices_.insert(vertices_.end(), data.begin(), data.end());
  return offset;
}

uint64_t Geometry::addIndicesData(std::span<const std::byte> data) {
  uint64_t const offset = indices_.size();
  indices_.insert(indices_.cend(), data.begin(), data.end());
  return offset;
}

void Geometry::addAttribute(AttributeType const type, AttributeInfo const& info) {
  attributes_[type] = info;
}

void Geometry::clearIndicesAndVertices() {
  indices_.clear();
  vertices_.clear();
}

bool Geometry::recalculateTangents() {
  if ( !hasAttribute(AttributeType::Position)
    || !hasAttribute(AttributeType::Normal)
    || !hasAttribute(AttributeType::Tangent)
    || !hasAttribute(AttributeType::Texcoord)) {
    return false;
  }

  if (indices_.empty()) {
    return false;
  }

  if (topology_ != Topology::TriangleList) {
    return false;
  }

  struct Helper {
    Geometry *geo{};
    Geometry::Primitive const* prim{};

    uint32_t vertex_index(int32_t iFace, int32_t iVert) const {
      auto const index_id = iFace * 3 + iVert;
      auto const* indices = geo->indices_.data() + prim->indexOffset;
      uint32_t index = 0u;

      if (geo->index_format_ == Geometry::IndexFormat::U16) {
        index = reinterpret_cast<uint16_t const*>(indices)[index_id];
      } else {
        index = reinterpret_cast<uint32_t const*>(indices)[index_id];
      }
      return index;
    };

    float* vertex(Geometry::AttributeType attrib, uint32_t vertex_index) const {
      auto const& attr = geo->attributes_.at(attrib);
      return reinterpret_cast<float*>(
        geo->vertices_.data() + attr.offset + static_cast<uintptr_t>(vertex_index * attr.stride)
      );
    };
  } helper;


  SMikkTSpaceInterface interface{
    .m_getNumFaces = [](SMikkTSpaceContext const* pContext) {
      auto [_, prim] = *static_cast<Helper*>(pContext->m_pUserData);
      return static_cast<int>(prim->indexCount / 3u);
    },

    .m_getNumVerticesOfFace = [](SMikkTSpaceContext const* pContext, int const iFace) {
      return 3;
    },

    .m_getPosition = [](SMikkTSpaceContext const* pContext,
                        float fvPosOut[],
                        int const iFace,
                        int const iVert) {
      Helper *H = static_cast<Helper*>(pContext->m_pUserData);
      auto const vertex_index = H->vertex_index(iFace, iVert);
      float const* pos = H->vertex(Geometry::AttributeType::Position, vertex_index);
      fvPosOut[0] = pos[0];
      fvPosOut[1] = pos[1];
      fvPosOut[2] = pos[2];
    },

    .m_getNormal = [](SMikkTSpaceContext const* pContext,
                      float fvNormOut[],
                      int const iFace,
                      int const iVert) {
      Helper *H = static_cast<Helper*>(pContext->m_pUserData);
      auto const vertex_index = H->vertex_index(iFace, iVert);
      float const *nor = H->vertex(Geometry::AttributeType::Normal, vertex_index);
      fvNormOut[0] = nor[0];
      fvNormOut[1] = nor[1];
      fvNormOut[2] = nor[2];
    },

    .m_getTexCoord = [](SMikkTSpaceContext const* pContext,
                        float fvTexcOut[],
                        const int iFace,
                        const int iVert) {
      Helper *H = static_cast<Helper*>(pContext->m_pUserData);
      auto const vertex_index = H->vertex_index(iFace, iVert);
      float const *uv = H->vertex(Geometry::AttributeType::Texcoord, vertex_index);
      fvTexcOut[0] = uv[0];
      fvTexcOut[1] = uv[1];
    },

    .m_setTSpaceBasic = [](SMikkTSpaceContext const* pContext,
                           float const fvTangent[],
                           float const fSign,
                           int const iFace,
                           int const iVert) {
      Helper *H = static_cast<Helper*>(pContext->m_pUserData);
      auto const vertex_index = H->vertex_index(iFace, iVert);
      float *tangent = H->vertex(Geometry::AttributeType::Tangent, vertex_index);
      tangent[0] = fvTangent[0];
      tangent[1] = fvTangent[1];
      tangent[2] = fvTangent[2];
      tangent[3] = fSign;
    },

    .m_setTSpace = nullptr,
  };

  helper.geo = this;
  for (auto const& prim : primitives_) {
    helper.prim = &prim;
    SMikkTSpaceContext context{&interface, &helper};
    if (genTangSpaceDefault(&context) == 0) {
      return false;
    }
  }

  if constexpr (true) {
    // reindex_after_tangent_generation();
  }

  return true;
}

/* -------------------------------------------------------------------------- */

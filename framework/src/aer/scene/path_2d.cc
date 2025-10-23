#include "aer/scene/path_2d.h"
#include "aer/scene/mesh.h"

#include "mapbox/earcut.hpp"

/* -------------------------------------------------------------------------- */

namespace mapbox {
namespace util {

template <>
struct nth<0, vec3> {
  inline static auto get(vec3 const &t) {
    return t.x;
  };
};
template <>
struct nth<1, vec3> {
  inline static auto get(vec3 const &t) {
    return t.y;
  };
};

} // namespace util
} // namespace mapbox

/* -------------------------------------------------------------------------- */

namespace scene {

bool Path2D::BuildContourMesh(Path2D path, scene::Mesh &mesh) {
  if (!path.triangulate()) {
    return false;
  }

  mesh.addAttribute(
    Geometry::AttributeType::Position,
    Polyline::AttributeInfo()
  );
  mesh.set_topology(Geometry::Topology::LineStrip);

  /* Merge all contours into a single primitive. */
  auto const& polylines = path.polylines();
  for (auto const& polyline : polylines) {
    auto const& vertices = polyline.vertices();
    auto offset = mesh.addVerticesData(
      std::as_bytes(std::span(vertices))
    );

    mesh.addPrimitive({
      .vertexCount = polyline.vertex_count(),
      .indexCount = 0,
      .bufferOffsets = {
        {Geometry::AttributeType::Position, offset},
      }
    });
  }

  return true;
}

// ----------------------------------------------------------------------------

bool Path2D::BuildShapeMesh(
  Path2D path,
  scene::Mesh &mesh,
  float extrusionDepth,
  uint32_t extrusionSampleCount
) {
  if (!path.triangulate()) {
    return false;
  }

  // SHAPE
  //
  // This will create NUM_SUB_GLYPH * (1 + 2) primitives
  // (+2 when extrusion is enabled)
  // Those primitives don't share vertices
  //


  mesh.addAttribute(
    Geometry::AttributeType::Position,
    Polyline::AttributeInfo()
  );
  mesh.set_index_format(Geometry::IndexFormat::U32);
  mesh.set_topology(Geometry::Topology::TriangleList);

  auto const& polylines = path.polylines();
  auto const& index_buffers = path.index_buffers();

  for (size_t contour_id = 0; contour_id < polylines.size(); ++contour_id)
  {
    /* We copy the indices buffer, as opposed to const ref them, as we might
     * update them for the extruded back face. */
    auto indices = index_buffers[contour_id];

    auto const faceIndexCount = static_cast<uint32_t>(indices.size());
    if (faceIndexCount == 0) {
      continue;
    }

    // Add the front face primitive.
    {
      uint32_t vertexCount = 0;
      uint64_t vertexOffset = 0;

      // Merge the main contour and its holes into one primitive.
      for (auto const& polyline : path.contour_subspan(contour_id))
      {
        auto const& vertices = polyline.vertices();

        auto const off = mesh.addVerticesData(
          std::as_bytes(std::span(vertices))
        );
        if (vertexCount == 0) {
          vertexOffset = off;
        }
        vertexCount += polyline.vertex_count();
      }
      auto const indexOffset = mesh.addIndicesData(
        std::as_bytes(std::span(indices))
      );

      mesh.addPrimitive({
        .vertexCount = vertexCount,
        .indexCount = faceIndexCount,
        .indexOffset = indexOffset,
        .bufferOffsets = {
          { Geometry::AttributeType::Position, vertexOffset },
        }
      });
    }

    // ----------------

    // Extrusion.
    if (extrusionDepth > 0.0)
    {
      auto const kExtrusionDepthVector{
        - extrusionDepth * Polyline::kDefaultFrontAxis
      };

      // Back Face.
      {
        uint32_t vertexCount = 0u;
        uint64_t vertexOffset = 0uL;

        // Push front vertices to the back.
        for (auto const& polyline : path.contour_subspan(contour_id))
        {
          auto vertices = polyline.vertices();
          for (auto& v : vertices) {
            v += kExtrusionDepthVector;
          }

          auto const off = mesh.addVerticesData(
            std::as_bytes(std::span(vertices))
          );
          if (vertexCount == 0) {
            vertexOffset = off;
          }
          vertexCount += polyline.vertex_count();
        }

        // Flip indices to have correct winding order.
        for (size_t i = 0; i < indices.size(); i += 3) {
          std::swap(indices[i], indices[i+2]);
        }

        auto const backIndicesOffset = mesh.addIndicesData(
          std::as_bytes(std::span(indices))
        );

        // Add the back face primitive.
        mesh.addPrimitive({
          .vertexCount = vertexCount,
          .indexCount = faceIndexCount,
          .indexOffset = backIndicesOffset,
          .bufferOffsets = {
            { Geometry::AttributeType::Position, vertexOffset },
          }
        });
      }

      // Extruded Shape.
      auto const bandVertexCount = extrusionSampleCount + 2u;
      for (auto const& polyline : polylines) {
        auto const& vertices = polyline.vertices();
        auto const vertexCount = polyline.vertex_count();

        auto const eVertexCount = vertexCount * bandVertexCount;
        auto const eIndexCount = 2u * 3u * vertexCount* (bandVertexCount - 1u);

        std::vector<vec3> eVertices{};
        eVertices.reserve(eVertexCount);

        std::vector<uint32_t> eIndices{};
        eIndices.reserve(eIndexCount);

        uint32_t index = 0;
        for (uint32_t j = 0; j < vertexCount; ++j) {
          auto const& first_vertex = vertices[j];

          // Add band vertices.
          for (uint32_t i = 0; i < bandVertexCount; ++i) {
            auto const t = i / static_cast<float>(bandVertexCount-1u);
            auto const v = first_vertex + t * kExtrusionDepthVector;
            eVertices.push_back(v);

            // Add band indices.
            if (i+1 < bandVertexCount)
            {
              uint32_t const i00 = index;
              uint32_t const i10 = (i00 + bandVertexCount) % eVertexCount;
              uint32_t const i01 = i00 + 1u;
              uint32_t const i11 = i10 + 1u;

              eIndices.insert(eIndices.end(),
                {i00, i10, i01, i10, i11, i01}
              );
            }
            index += 1u;
          }
        }

        auto const extrusionVertexOffset = mesh.addVerticesData(
          std::as_bytes(std::span(eVertices))
        );
        auto const extrusionIndexOffset = mesh.addIndicesData(
          std::as_bytes(std::span(eIndices))
        );
        mesh.addPrimitive({
          .vertexCount = static_cast<uint32_t>(eVertices.size()),
          .indexCount = static_cast<uint32_t>(eIndices.size()),
          .indexOffset = extrusionIndexOffset,
          .bufferOffsets = {
            { Geometry::AttributeType::Position, extrusionVertexOffset },
          }
        });
      }
    }
  }

  return true;
}

/* -------------------------------------------------------------------------- */

void Path2D::moveTo(vec2 const& p) {
  if (polylines_.empty() || !last_polyline().empty()) {
    polylines_.emplace_back(lina::to_vec3(p));
  }
}

// ----------------------------------------------------------------------------

void Path2D::lineTo(
  vec2 const& p,
  uint32_t nsteps
) {
  auto &poly = last_polyline();
  nsteps = (nsteps <= 0) ? 1 : nsteps;
  auto const dstep = 1.0f / static_cast<float>(nsteps);
  auto const last_v = lina::to_vec2(poly.last_vertex());
  for(uint32_t i = 1; i <= nsteps; ++i) {
    poly.addVertex(linalg::lerp(last_v, p, i * dstep));
  }
}

// ----------------------------------------------------------------------------

void Path2D::quadBezierTo(
  vec2 const& cp,
  vec2 const& p,
  uint32_t curve_resolution
) {
  last_polyline().quadBezierTo(cp, p, curve_resolution);
}

// ----------------------------------------------------------------------------

bool Path2D::triangulate() {
  std::vector<bool> is_shapes(polylines_.size(), false);

  index_buffers_.clear();
  index_buffers_.resize(polylines_.size());

  range_sizes_.clear();
  range_sizes_.resize(polylines_.size(), 0ul);

  // Find which polyline is a shape.
  for (size_t i = 0; i < polylines_.size(); ++i) {
    auto const orient = polylines_[i].calculateOrientation2D();
    is_shapes[i] = (orient == Polyline::Orientation::CounterClockWise);
  }

  // Find the size of each {shape + holes} ranges.
  size_t range_size{};
  auto const start_index = static_cast<int32_t>(polylines_.size()) - 1;
  for (int i = start_index; i >= 0; --i) {
    if (is_shapes[i]) {
      range_sizes_[i] = range_size + 1;
      range_size = 0;
    }
    ++range_size;
  }

  // Tessellate the shapes.
  for (size_t i = 0; i < polylines_.size(); ++i) {
    if (is_shapes[i]) {
      index_buffers_[i] = mapbox::earcut(contour_subspan(i));
    }
  }

  return triangulated();
}

/* -------------------------------------------------------------------------- */

} // namespace "scene"

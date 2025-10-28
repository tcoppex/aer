#ifndef AER_SCENE_GEOMETRY_H_
#define AER_SCENE_GEOMETRY_H_

/* -------------------------------------------------------------------------- */

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

// ----------------------------------------------------------------------------

/**
 * Define host side geometry data.
 * Useful for side loading or creating procedural geometries.
 **/
class Geometry {
 public:
  static constexpr float kDefaultSize = 1.0f;
  static constexpr float kDefaultRadius = 0.5f;

 public:
  enum class Topology {
    PointList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    kCount,
    kUnknown
  };

  enum class IndexFormat {
    U8,
    U16,
    U32,
    kCount,
    kUnknown
  };

  enum class AttributeFormat {
    R_F32,
    RG_F32,
    RGB_F32,
    RGBA_F32,
    R_U32,
    RGBA_U32,
    R_U16,
    RGBA_U16,
    kCount,
    kUnknown,
  };

  enum class AttributeType {
    Position,
    Texcoord,
    Normal,
    Tangent,
    Joints,
    Weights,
    kCount,
    kUnknown
  };

  struct AttributeInfo {
    AttributeFormat format{};
    uint32_t offset{};
    uint32_t stride{};
  };

  using AttributeLocationMap = std::map<AttributeType, uint32_t>;
  using AttributeOffsetMap   = std::map<AttributeType, uint64_t>;
  using AttributeInfoMap     = std::map<AttributeType, AttributeInfo>;

  struct Primitive {
    Topology topology{Topology::kUnknown};

    uint32_t vertexCount{};
    uint32_t indexCount{};

    uint64_t indexOffset{};

    /**
     * When all attributes share the same offset their data are interleaved,
     * otherwhise buffers should be bind separately,
     * with offset depending on the number of elements and the format of attributes before them.
     */
    AttributeOffsetMap bufferOffsets{}; //
  };

 public:
  // --- Indexed Triangle List ---

  /* Create a cube with interleaved Position, Normal and UV. */
  static void MakeCube(Geometry &geo, float size = kDefaultSize);

  // --- Indexed Triangle Strip ---

  /* Create a +Y plane with interleaved Position, Normal and UV. */
  static void MakePlane(Geometry &geo, float size = kDefaultSize, uint32_t resx = 1u, uint32_t resy = 1u);

  /* Create a sphere with interleaved Position, Normal and UV. */
  static void MakeSphere(Geometry &geo, float radius, uint32_t resx, uint32_t resy);

  static void MakeSphere(Geometry &geo, float radius = kDefaultRadius, uint32_t resolution = 32u) {
    MakeSphere(geo, radius, resolution, resolution);
  }

  /* Create a torus with interleaved Position, Normal, and UV. */
  static void MakeTorus(Geometry &geo, float major_radius, float minor_radius, uint32_t resx = 32u, uint32_t resy = 24u);

  static void MakeTorus(Geometry &geo, float radius = kDefaultRadius) {
    MakeTorus(geo, 0.8f*radius, 0.2f*radius);
  }

  static void MakeTorus2(Geometry &geo, float inner_radius, float outer_radius, uint32_t resx = 32u, uint32_t resy = 24u) {
    float const minor_radius = (outer_radius - inner_radius) / 2.0f;
    MakeTorus(geo, inner_radius + minor_radius, minor_radius, resx, resy);
  }

  // --- Indexed Point List ---

  /* Create a plane of points with float4 positions and an index buffer. */
  static void MakePointListPlane(Geometry &geo, float size = kDefaultSize, uint32_t resx = 1u, uint32_t resy = 1u);

 public:
  Geometry() = default;
  ~Geometry() = default;

  void reset() {
    *this = {};
  }

  /* --- Getters --- */

  [[nodiscard]]
  Topology topology() const noexcept {
    return topology_;
  }

  [[nodiscard]]
  IndexFormat index_format() const noexcept {
    return index_format_;
  }

  [[nodiscard]]
  uint32_t index_count() const noexcept {
    return index_count_;
  }

  [[nodiscard]]
  uint32_t vertex_count() const noexcept {
    return vertex_count_;
  }

  [[nodiscard]]
  AttributeFormat attribute_format(AttributeType const attrib_type) const {
    return attributes_.at(attrib_type).format;
  }

  [[nodiscard]]
  uint32_t attribute_offset(AttributeType const attrib_type) const {
    return attributes_.at(attrib_type).offset;
  }

  [[nodiscard]]
  uint32_t attribute_stride(AttributeType const attrib_type = AttributeType::Position) const {
    return attributes_.at(attrib_type).stride;
  }

  [[nodiscard]]
  uint64_t indices_bytesize() const noexcept {
    return indices_.size();
  }

  [[nodiscard]]
  uint64_t vertices_bytesize() const noexcept {
    return vertices_.size();
  }

  [[nodiscard]]
  std::vector<std::byte> const& indices() const noexcept {
    return indices_;
  }

  [[nodiscard]]
  std::vector<std::byte> const& vertices() const noexcept {
    return vertices_;
  }

  [[nodiscard]]
  Primitive const& primitive(uint32_t const primitive_index = 0u) const {
    return primitives_.at(primitive_index);
  }

  [[nodiscard]]
  uint32_t primitive_count() const noexcept {
    return static_cast<uint32_t>(primitives_.size());
  }

  /* --- Setters --- */

  void set_attributes(AttributeInfoMap const attributes) noexcept {
    attributes_ = attributes;
  }

  void set_topology(Topology const topology) noexcept {
    topology_ = topology;
  }

  void set_index_format(IndexFormat const format) noexcept {
    index_format_ = format;
  }

  /* --- Utils --- */

  [[nodiscard]]
  bool hasAttribute(AttributeType const type) const noexcept {
    return attributes_.contains(type);
  }

  void addAttribute(AttributeType const type, AttributeInfo const& info);

  void addPrimitive(Primitive const& primitive);

  /* Return the current bytesize of the vertex attributes buffer. */
  uint64_t addVerticesData(std::span<const std::byte> data);

  /* Return the current bytesize of the indices buffer. */
  uint64_t addIndicesData(std::span<const std::byte> data);

  /* Release host data. */
  void clearIndicesAndVertices();

  bool recalculateTangents();

 protected:
  AttributeInfoMap attributes_{};
  std::vector<Primitive> primitives_{};

 private:
  Topology topology_{};
  IndexFormat index_format_{};
  uint32_t index_count_{};
  uint32_t vertex_count_{};

  std::vector<std::byte> indices_{};
  std::vector<std::byte> vertices_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_GEOMETRY_H_

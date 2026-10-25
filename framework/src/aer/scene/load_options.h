#ifndef AER_SCENE_LOAD_OPTIONS_H_
#define AER_SCENE_LOAD_OPTIONS_H_

namespace scene {

/* -------------------------------------------------------------------------- */

// to put in scene/vertex_layout_desc.h for customization.
// struct VertexLayoutDesc {
//   uint32_t stride{};
//   struct Attr {
//     uint32_t offset{};
//     uint32_t components{};
//   };
//   std::map<Geometry::AttributeType, Attr> attributes{};   // absent = not stored
// };

/* -------------------------------------------------------------------------- */

struct LoadOptions {
  bool keep_raw_attributes = false;   // if false use VertexInternal_t
  bool keep_indexing_format = false;  // if false use uint32_t

  //std::optional<VertexLayoutDesc> vertex_layout_desc{};

  // (for instanciation)
  // int scene_index = -1;
};

/* -------------------------------------------------------------------------- */

}

#endif // AER_SCENE_LOAD_OPTIONS_H_
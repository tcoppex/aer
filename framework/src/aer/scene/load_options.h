#ifndef AER_SCENE_LOAD_OPTIONS_H_
#define AER_SCENE_LOAD_OPTIONS_H_

namespace scene {

/* -------------------------------------------------------------------------- */

struct LoadOptions {
  bool keep_raw_attributes = false;   // if false use VertexInternal_t
  bool keep_indexing_format = false;  // if false use uint32_t

  // (for restructuration, instead of using VertexInternal_t)
  // AttributeInfoMap attributes_info{};

  // (for instanciation)
  // int scene_index = -1;
};

/* -------------------------------------------------------------------------- */

}

#endif // AER_SCENE_LOAD_OPTIONS_H_
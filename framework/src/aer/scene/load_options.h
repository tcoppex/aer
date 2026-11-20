#ifndef AER_SCENE_LOAD_OPTIONS_H_
#define AER_SCENE_LOAD_OPTIONS_H_

namespace scene {

/* -------------------------------------------------------------------------- */

struct LoadOptions {
  bool keep_raw_attributes = false;     // if false use VertexInternal_t
  bool keep_indexing_format = false;    // if false use uint32_t
  bool load_animations = true;          // bake the animations driving skins into clips
  float animation_framerate = 30.0f;    // sampling rate of the baked clips (samples per second)

  // (for restructuration, instead of using VertexInternal_t)
  // AttributeInfoMap attributes_info{};

  // (for instanciation)
  // int scene_index = -1;
};

/* -------------------------------------------------------------------------- */

}

#endif // AER_SCENE_LOAD_OPTIONS_H_
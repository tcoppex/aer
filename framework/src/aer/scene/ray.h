#ifndef AER_SCENE_RAY_H_
#define AER_SCENE_RAY_H_

#include "aer/core/common.h"

namespace scene {

struct Ray {
  vec3 origin{};
  vec3 direction{};
};

}

#endif // AER_SCENE_RAY_H_
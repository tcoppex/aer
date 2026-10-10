#ifndef AER_CORE_VIEW_CONTROLLER_INTERFACE_H_
#define AER_CORE_VIEW_CONTROLLER_INTERFACE_H_

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

class ViewControllerInterface {
 public:
  virtual ~ViewControllerInterface() = default;

  /* Handle event inputs, return true when the view matrices has changed. */
  virtual bool update(float dt) { return false; }

  /* Retrieve the new view matrix. */
  virtual void calculateViewMatrix(mat4 *m, uint32_t view_id = 0u) = 0;

  /* Number of view supported by the controller, should be max 2. */
  virtual uint32_t view_count() const noexcept { return 1u; }

  virtual vec3 target() const = 0; //
};


/* -------------------------------------------------------------------------- */

#endif // AER_CORE_VIEW_CONTROLLER_INTERFACE_H_

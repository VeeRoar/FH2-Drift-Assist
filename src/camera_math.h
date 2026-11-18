#ifndef CAMERA_MATH_H
#define CAMERA_MATH_H
/* Move the orbit to the front axle without changing the straight-line view.
   P' = P + distance * (carForward - cameraForward).
   Both the eye and its look target translate together; native collision and
   view construction still run after the chase update. */
int chase_pivot_shift(const float forward[3],const float look[3],float distance,float shift[3]);
#endif

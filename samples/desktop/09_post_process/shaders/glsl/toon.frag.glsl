#version 460

// ----------------------------------------------------------------------------

layout (set = 0, binding = 0)
uniform sampler2D inChannels[];

layout (location = 0) in vec2 vTexcoord;

layout (location = 0) out vec4 fragColor;

// ----------------------------------------------------------------------------

vec3 grayscale(vec3 color) {
  return vec3(dot(color, vec3(0.299, 0.587, 0.114)));
}

void main() {
  vec4 color = texture(inChannels[0], vTexcoord);

  float edge_nor = texture(inChannels[1], vTexcoord).r;
  float edge_obj = texture(inChannels[2], vTexcoord).r;

  color.xyz = mix(grayscale(color.xyz), vec3(0.88, 0.88, 0.77), 0.8);
  vec3 edge_color = (vec3(1) - color.xyz * 0.8);

  color.xyz = mix(color.xyz, edge_color, edge_nor);
  color.xyz = mix(color.xyz, edge_color, edge_obj);

  fragColor = color;
}

// ----------------------------------------------------------------------------
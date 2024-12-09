void node_bsdf_game(
    vec4 color,
    float metallic,
    float specular,
    float roughness,
    float alpha,
    vec3 N,
    float use_multi_scatter,
    float use_diffuse,
    out Closure result)
{
  N = normalize(N);
  vec3 V = view_position;
  
  float roughness_squared = max(1e-8, roughness * roughness);
  vec3 diffuse_color = mix(color.rgb, vec3(0.0), metallic);
  vec3 spec_color = mix(vec3(0.08 * specular), color.rgb, metallic);
  
  result = CLOSURE_DEFAULT;
  
  if (use_diffuse > 0.0) {
    result.diffuse = diffuse_color;
    result.weight *= (1.0 - metallic);
  }
  
  float NV = max(dot(N, V), 1e-5);
  result.specular = spec_color;
  result.roughness = roughness_squared;
  result.use_multi_scatter = (use_multi_scatter > 0.0);
  
  result.radiance = mix(result.radiance, result.radiance * alpha, alpha);
}

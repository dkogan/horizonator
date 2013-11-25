/* -*- c -*- */

#version 110

varying float red, green;
void main(void)
{
  gl_FragColor = vec4(red, green ,0.0, 0.0);
}

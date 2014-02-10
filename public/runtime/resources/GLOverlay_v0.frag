// Created using http://en.wikipedia.org/wiki/HLSL2GLSL from our
// D3D9 source in gameoverlayrenderer\Shaders\D3D9Overlay.ps

//
// Translator library functions
//

float xlat_lib_saturate( float x) {
  return clamp( x, 0.0, 1.0);
}

vec2 xlat_lib_saturate( vec2 x) {
  return clamp( x, 0.0, 1.0);
}

vec3 xlat_lib_saturate( vec3 x) {
  return clamp( x, 0.0, 1.0);
}

vec4 xlat_lib_saturate( vec4 x) {
  return clamp( x, 0.0, 1.0);
}

mat2 xlat_lib_saturate(mat2 m) {
  return mat2( clamp(m[0], 0.0, 1.0), clamp(m[1], 0.0, 1.0));
}

mat3 xlat_lib_saturate(mat3 m) {
  return mat3( clamp(m[0], 0.0, 1.0), clamp(m[1], 0.0, 1.0), clamp(m[2], 0.0, 1.0));
}

mat4 xlat_lib_saturate(mat4 m) {
  return mat4( clamp(m[0], 0.0, 1.0), clamp(m[1], 0.0, 1.0), clamp(m[2], 0.0, 1.0), clamp(m[3], 0.0, 1.0));
}


//
// Structure definitions
//

struct RGB_SAMPLE_COORDS {
    vec2 xy_red;
    vec2 xy_green;
    vec2 xy_blue;
};

struct PS_OUTPUT {
    vec4 color;
};

struct Fullscreen_Quad_Vert {
    vec4 position;
    vec2 texcoord;
    vec4 color;
};


//
// Global variable definitions
//

uniform vec4 P0;
uniform float c_rg_to_rb_ratio;
uniform vec4 coef;
uniform sampler2D filterTex;
uniform mat4 invProj;
uniform sampler2D mainTex;
uniform mat4 rot;
uniform vec4 uvOffset;
uniform vec4 kernel[2];

//
// Function declarations
//

RGB_SAMPLE_COORDS undistort_coords_from_texture( in vec2 norm01_coord );
vec4 sample_chromatic( in vec2 uv );
vec4 background( in vec3 ray );
vec4 raycast( in vec2 uv );
PS_OUTPUT PSAA( in Fullscreen_Quad_Vert xlat_var_input );

//
// Function definitions
//

RGB_SAMPLE_COORDS undistort_coords_from_texture( in vec2 norm01_coord ) {
    vec4 distort_samp;
    RGB_SAMPLE_COORDS samp;

    distort_samp = texture2D( filterTex, norm01_coord);
    samp.xy_red = vec2( distort_samp.x , distort_samp.y );
    samp.xy_blue = vec2( distort_samp.z , distort_samp.w );
    samp.xy_green = (samp.xy_red + (c_rg_to_rb_ratio * (samp.xy_blue - samp.xy_red)));
    return samp;
}


vec4 sample_chromatic( in vec2 uv ) {
    RGB_SAMPLE_COORDS samp;
    vec4 redsamp;
    vec4 greensamp;
    vec4 bluesamp;
    vec4 color;
    float threshold;

    samp = undistort_coords_from_texture( uv);
    redsamp = raycast( samp.xy_red);
    greensamp = raycast( samp.xy_green);
    bluesamp = raycast( samp.xy_blue);
    color.x  = redsamp.x ;
    color.y  = greensamp.y ;
    color.z  = bluesamp.z ;
    color.w  = greensamp.w ;
    threshold = xlat_lib_saturate( (dot( vec2( lessThan( samp.xy_green, vec2( 0.0100000, 0.0100000)) ), vec2( 1.00000, 1.00000)) + dot( vec2( greaterThan( samp.xy_green, vec2( 0.990000, 0.990000)) ), vec2( 1.00000, 1.00000))) );
    return mix( color, vec4( 0.000000, 0.000000, 0.000000, color.w ), vec4( threshold));
}


vec4 background( in vec3 ray ) {

    return vec4( 0.000000, 0.000000, 0.000000, 0.000000);
}


vec4 raycast( in vec2 uv ) {
    vec4 ndc;
    vec4 V;
    float a;
    float b;
    float c;
    float det;
    float t;
    vec3 P;
    vec2 threshold;
    vec2 edge;
    float bg;

    ndc = vec4( ((2.00000 * uv.x ) - 1.00000), (1.00000 - (2.00000 * uv.y )), 0.000000, 1.00000);
    ndc = ( invProj * ndc );
    ndc /= ndc.w ;
    ndc.w  = 0.000000;
    V = normalize( ( rot * ndc ) );
    a = dot( V.xz , V.xz );
    b = (2.00000 * dot( V.xz , P0.xz ));
    c = (dot( P0.xz , P0.xz ) - P0.w );
    det = ((b * b) - ((4.00000 * a) * c));
    if ( (det <= 0.000000) ){
        return background( V.xyz );
    }
    t = ((sqrt( det ) - b) / (2.00000 * a));
    if ( (t <= 0.000000) ){
        return background( V.xyz );
    }
    P = (P0.xyz  + (V.xyz  * t));
    P.x  = (atan( P.x , ( -P.z  )) / coef.x );
    P.y  *= coef.y ;
    uv.x  = (P.x  + 0.500000);
    uv.y  = (0.500000 - P.y );
    uv += uvOffset.xy ;
    uv *= uvOffset.zw ;
    threshold = vec2( 0.0100000, (0.0100000 * coef.z ));
    edge = xlat_lib_saturate( (((abs( P.xy  ) + threshold) - 0.500000) / threshold) );
    bg = dot( edge, edge);
    return mix( texture2D( mainTex, uv), background( V.xyz ), vec4( bg));
}


PS_OUTPUT PSAA( in Fullscreen_Quad_Vert xlat_var_input ) {
    PS_OUTPUT o;

    vec4 c1 = sample_chromatic(xlat_var_input.texcoord.xy + kernel[0].xy);
    vec4 c2 = sample_chromatic(xlat_var_input.texcoord.xy + kernel[0].zw);
    vec4 c3 = sample_chromatic(xlat_var_input.texcoord.xy + kernel[1].xy);
    vec4 c4 = sample_chromatic(xlat_var_input.texcoord.xy + kernel[1].zw);

    o.color = (c1 + c2 + c3 + c4) / 4.0f;
    o.color *= xlat_var_input.color;
    return o;
}


//
// User varying
//
varying vec4 xlat_varying_SV_POSITION;

//
// Translator's entry point
//
void main() {
    PS_OUTPUT xlat_retVal;
    Fullscreen_Quad_Vert xlat_temp_xlat_var_input;
    xlat_temp_xlat_var_input.position = vec4( xlat_varying_SV_POSITION);
    xlat_temp_xlat_var_input.texcoord = vec2( gl_TexCoord[0]);
    xlat_temp_xlat_var_input.color = vec4( gl_Color);

    xlat_retVal = PSAA( xlat_temp_xlat_var_input);

    gl_FragData[0] = vec4( xlat_retVal.color);
}


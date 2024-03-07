

#define PI 3.1415926538

//=================================================================================================
struct SpotLight
{
  float3 Position;
  float AngularAttenuationX;

  float3 Direction;
  float AngularAttenuationY;

  float3 Intensity;
  float Range;
};
//=================================================================================================
struct LightConstants
{
  SpotLight Lights[MaxSpotLights];
  float4x4 ShadowMatrices[MaxSpotLights];
};
//=================================================================================================
// Encoding/Decoding SRGB:
// sRGB to Linear
// Assuming using sRGB typed textures this should not be needed.
float toLinear1 (float c)
{
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
//=================================================================================================
// Linear to sRGB.
// Assuing using sRGB typed textures this should not be needed.
float toSrgb1 (float c)
{
    return (c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055);
}
//=================================================================================================
float3 toSrgb(float3 c)
{
    return float3( ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b) );
}
//=================================================================================================
float3 decodeSrgb( float3 c )
{
    float3 result;
    if ( c.r <= 0.04045) {
        result.r = c.r / 12.92;
    } else {
        result.r = pow( ( c.r + 0.055 ) / 1.055, 2.4 );
    }

    if ( c.g <= 0.04045) {
        result.g = c.g / 12.92;
    } else {
        result.g = pow( ( c.g + 0.055 ) / 1.055, 2.4 );
    }

    if ( c.b <= 0.04045) {
        result.b = c.b / 12.92;
    } else {
        result.b = pow( ( c.b + 0.055 ) / 1.055, 2.4 );
    }

    return clamp( result, 0.0, 1.0 );
}
//=================================================================================================
float3 encodeSrgb (float3 c)
{
    float3 result;
    if ( c.r <= 0.0031308) {
        result.r = c.r * 12.92;
    } else {
        result.r = 1.055 * pow( c.r, 1.0 / 2.4 ) - 0.055;
    }

    if ( c.g <= 0.0031308) {
        result.g = c.g * 12.92;
    } else {
        result.g = 1.055 * pow( c.g, 1.0 / 2.4 ) - 0.055;
    }

    if ( c.b <= 0.0031308) {
        result.b = c.b * 12.92;
    } else {
        result.b = 1.055 * pow( c.b, 1.0 / 2.4 ) - 0.055;
    }

    return clamp( result, 0.0, 1.0 );
}
//=================================================================================================
float luminance (float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}
//=================================================================================================
// https://spec.oneapi.io/oneipl/0.6/concepts/color-models.html
float3 rgbToYcocg (float3 c)
{
    // Y = R/4 + G/2 + B/4
    // Co = R/2 - B/2
    // Cg = -R/4 + G/2 - B/4
    return float3(c.x/4.0 + c.y/2.0 + c.z/4.0,
                c.x/2.0 - c.z/2.0,
                -c.x/4.0 + c.y/2.0 - c.z/4.0 );
}
//=================================================================================================
// https://spec.oneapi.io/oneipl/0.6/concepts/color-models.html
float3 YcocgToRgb (float3 c)
{
    // R = Y + Co - Cg
    // G = Y + Cg
    // B = Y - Co - Cg
    return clamp(float3(c.x + c.y - c.z,
                      c.x + c.z,
                      c.x - c.y - c.z), float3(0), float3(1));
}
//=================================================================================================
// Noise helper functions:
float remapNoiseTri (float v)
{
    v = v * 2.0 - 1.0;
    return sign(v) * (1.0 - sqrt(1.0 - abs(v)));
}
//=================================================================================================
// Takes 2 noises in space [0..1] and remaps them in [-1..1]
float triangularNoise (float noise0, float noise1)
{
    return noise0 + noise1 - 1.0f;
}
//=================================================================================================
float interleavedGradientNoise (vec2 pixel, int frame)
{
    pixel += (float(frame) * 5.588238f);
    return frac(52.9829189f * frac(0.06711056f*float(pixel.x) + 0.00583715f*float(pixel.y)));
}
//=================================================================================================
// Custom filtering
// https://gist.github.com/Fewes/59d2c831672040452aa77da6eaab2234
float4 tricubicFiltering (uint textureIndex, float3 uvw, float3 textureSize)
{
    // Shift the coordinate from [0,1] to [-0.5, textureSize-0.5]
    float3 coordGrid = uvw * textureSize - 0.5;
    float3 index = floor(coordGrid);
    float3 fraction = coordGrid - index;
    float3 oneFrac = 1.0 - fraction;

    float3 w0 = 1.0 / 6.0 * oneFrac * oneFrac * oneFrac;
    float3 w1 = 2.0 / 3.0 - 0.5 * fraction * fraction * (2.0 - fraction);
    float3 w2 = 2.0 / 3.0 - 0.5 * oneFrac * oneFrac * (2.0 - oneFrac);
    float3 w3 = 1.0 / 6.0 * fraction * fraction * fraction;

    float3 g0 = w0 + w1;
    float3 g1 = w2 + w3;
    float3 mult = 1.0 / textureSize;
    float3 h0 = mult * ((w1 / g0) - 0.5 + index); //h0 = w1/g0 - 1, move from [-0.5, textureSize-0.5] to [0,1]
    float3 h1 = mult * ((w3 / g1) + 1.5 + index); //h1 = w3/g1 + 1, move from [-0.5, textureSize-0.5] to [0,1]

    // Fetch the eight linear interpolations
    // Weighting and fetching is interleaved for performance and stability reasons
    Texture3D noiseTexture = Tex3DTable[NonUniformResourceIndex(textureIndex)];

    float4 tex000 = noiseTexture.SampleLevel(LinearClampSampler, h0, 0);
    float4 tex100 = noiseTexture.SampleLevel(LinearClampSampler, float3(h1.x, h0.y, h0.z), 0);
    tex000 = lerp(tex100, tex000, g0.x); // Weigh along the x-direction

    float4 tex010 = noiseTexture.SampleLevel(LinearClampSampler, float3(h0.x, h1.y, h0.z), 0);
    float4 tex110 = noiseTexture.SampleLevel(LinearClampSampler, float3(h1.x, h1.y, h0.z), 0);
    // float4 tex010 = noiseTexture[float4(h0.x, h1.y, h0.z, 0)];
    // float4 tex110 = noiseTexture[float4(h1.x, h1.y, h0.z, 0)];
    tex010 = lerp(tex110, tex010, g0.x); // Weigh along the x-direction
    tex000 = lerp(tex010, tex000, g0.y); // Weigh along the y-direction

    float4 tex001 = noiseTexture.SampleLevel(LinearClampSampler, float3(h0.x, h0.y, h1.z), 0);
    float4 tex101 = noiseTexture.SampleLevel(LinearClampSampler, float3(h1.x, h0.y, h1.z), 0);
    // float4 tex001 = noiseTexture[float4(h0.x, h0.y, h1.z, 0)];
    // float4 tex101 = noiseTexture[float4(h1.x, h0.y, h1.z, 0)];
    tex001 = lerp(tex101, tex001, g0.x); // Weigh along the x-direction

    float4 tex011 = noiseTexture.SampleLevel(LinearClampSampler, float3(h0.x, h1.y, h1.z), 0);
    float4 tex111 = noiseTexture.SampleLevel(LinearClampSampler, float3(h1.x, h1.y, h1.z), 0);
    // float4 tex011 = noiseTexture[float4(h0.x, h1.y, h1.z, 0)];
    // float4 tex111 = noiseTexture[float4(h1, 0)];
    tex011 = lerp(tex111, tex011, g0.x); // Weigh along the x-direction
    tex001 = lerp(tex011, tex001, g0.y); // Weigh along the y-direction

    return mix(tex001, tex000, g0.z); // Weigh along the z-direction
}
//=================================================================================================


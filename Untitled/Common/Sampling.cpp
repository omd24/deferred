//=================================================================================================
//
//  from MJP's DX12 Sample Framework
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

#include "Sampling.hpp"

#define RadicalInverse_(base)                 \
  {                                           \
    const float radical = 1.0f / float(base); \
    uint64_t value = 0;                         \
    float factor = 1.0f;                      \
    while (sampleIdx)                         \
    {                                         \
      uint64_t next = sampleIdx / base;         \
      uint64_t digit = sampleIdx - next * base; \
      value = value * base + digit;           \
      factor *= radical;                      \
      sampleIdx = next;                       \
    }                                         \
    inverse = float(value) * factor;          \
  }

static const float OneMinusEpsilon = 0.9999999403953552f;

// Random impl
void Random::SetSeed(uint32_t seed) { engine.seed(seed); }

void Random::SeedWithRandomValue()
{
  std::random_device device;
  engine.seed(device());
}

uint32_t Random::RandomUint() { return engine(); }

float Random::RandomFloat()
{
  // return distribution(engine);
  return (RandomUint() & 0xFFFFFF) / float(1 << 24);
}

glm::vec2 Random::RandomFloat2() { return glm::vec2(RandomFloat(), RandomFloat()); }

float RadicalInverseFast(uint64_t baseIdx, uint64_t sampleIdx)
{
  assert(baseIdx < 64);

  float inverse = 0.0f;

  switch (baseIdx)
  {
  case 0:
    RadicalInverse_(2);
    break;
  case 1:
    RadicalInverse_(3);
    break;
  case 2:
    RadicalInverse_(5);
    break;
  case 3:
    RadicalInverse_(7);
    break;
  case 4:
    RadicalInverse_(11);
    break;
  case 5:
    RadicalInverse_(13);
    break;
  case 6:
    RadicalInverse_(17);
    break;
  case 7:
    RadicalInverse_(19);
    break;
  case 8:
    RadicalInverse_(23);
    break;
  case 9:
    RadicalInverse_(29);
    break;
  case 10:
    RadicalInverse_(31);
    break;
  case 11:
    RadicalInverse_(37);
    break;
  case 12:
    RadicalInverse_(41);
    break;
  case 13:
    RadicalInverse_(43);
    break;
  case 14:
    RadicalInverse_(47);
    break;
  case 15:
    RadicalInverse_(53);
    break;
  case 16:
    RadicalInverse_(59);
    break;
  case 17:
    RadicalInverse_(61);
    break;
  case 18:
    RadicalInverse_(67);
    break;
  case 19:
    RadicalInverse_(71);
    break;
  case 20:
    RadicalInverse_(73);
    break;
  case 21:
    RadicalInverse_(79);
    break;
  case 22:
    RadicalInverse_(83);
    break;
  case 23:
    RadicalInverse_(89);
    break;
  case 24:
    RadicalInverse_(97);
    break;
  case 25:
    RadicalInverse_(101);
    break;
  case 26:
    RadicalInverse_(103);
    break;
  case 27:
    RadicalInverse_(107);
    break;
  case 28:
    RadicalInverse_(109);
    break;
  case 29:
    RadicalInverse_(113);
    break;
  case 30:
    RadicalInverse_(127);
    break;
  case 31:
    RadicalInverse_(131);
    break;
  case 32:
    RadicalInverse_(137);
    break;
  case 33:
    RadicalInverse_(139);
    break;
  case 34:
    RadicalInverse_(149);
    break;
  case 35:
    RadicalInverse_(151);
    break;
  case 36:
    RadicalInverse_(157);
    break;
  case 37:
    RadicalInverse_(163);
    break;
  case 38:
    RadicalInverse_(167);
    break;
  case 39:
    RadicalInverse_(173);
    break;
  case 40:
    RadicalInverse_(179);
    break;
  case 41:
    RadicalInverse_(181);
    break;
  case 42:
    RadicalInverse_(191);
    break;
  case 43:
    RadicalInverse_(193);
    break;
  case 44:
    RadicalInverse_(197);
    break;
  case 45:
    RadicalInverse_(199);
    break;
  case 46:
    RadicalInverse_(211);
    break;
  case 47:
    RadicalInverse_(223);
    break;
  case 48:
    RadicalInverse_(227);
    break;
  case 49:
    RadicalInverse_(229);
    break;
  case 50:
    RadicalInverse_(233);
    break;
  case 51:
    RadicalInverse_(239);
    break;
  case 52:
    RadicalInverse_(241);
    break;
  case 53:
    RadicalInverse_(251);
    break;
  case 54:
    RadicalInverse_(257);
    break;
  case 55:
    RadicalInverse_(263);
    break;
  case 56:
    RadicalInverse_(269);
    break;
  case 57:
    RadicalInverse_(271);
    break;
  case 58:
    RadicalInverse_(277);
    break;
  case 59:
    RadicalInverse_(281);
    break;
  case 60:
    RadicalInverse_(283);
    break;
  case 61:
    RadicalInverse_(293);
    break;
  case 62:
    RadicalInverse_(307);
    break;
  case 63:
    RadicalInverse_(311);
    break;
  }
  return std::min(inverse, OneMinusEpsilon);
}

// Maps a value inside the square [0,1]x[0,1] to a value in a disk of radius 1 using concentric
// squares. This mapping preserves area, bi continuity, and minimizes deformation. Based off the
// algorithm "A Low Distortion Map Between Disk and Square" by Peter Shirley and Kenneth Chiu. Also
// includes polygon morphing modification from "CryEngine3 Graphics Gems" by Tiago Sousa
glm::vec2 SquareToConcentricDiskMapping(float x, float y, float numSides, float polygonAmount)
{
  float phi, r;

  // -- (a,b) is now on [-1,1]�2
  float a = 2.0f * x - 1.0f;
  float b = 2.0f * y - 1.0f;

  if (a > -b) // region 1 or 2
  {
    if (a > b) // region 1, also |a| > |b|
    {
      r = a;
      phi = (Pi / 4.0f) * (b / a);
    }
    else // region 2, also |b| > |a|
    {
      r = b;
      phi = (Pi / 4.0f) * (2.0f - (a / b));
    }
  }
  else // region 3 or 4
  {
    if (a < b) // region 3, also |a| >= |b|, a != 0
    {
      r = -a;
      phi = (Pi / 4.0f) * (4.0f + (b / a));
    }
    else // region 4, |b| >= |a|, but a==0 and b==0 could occur.
    {
      r = -b;
      if (b != 0)
        phi = (Pi / 4.0f) * (6.0f - (a / b));
      else
        phi = 0;
    }
  }

  const float N = numSides;
  float polyModifier =
      std::cos(Pi / N) / std::cos(phi - (Pi2 / N) * std::floor((N * phi + Pi) / Pi2));
  r *= lerp(1.0f, polyModifier, polygonAmount);

  glm::vec2 result;
  result.x = r * std::cos(phi);
  result.y = r * std::sin(phi);

  return result;
}

// Maps a value inside the square [0,1]x[0,1] to a value in a disk of radius 1 using concentric
// squares. This mapping preserves area, bi continuity, and minimizes deformation. Based off the
// algorithm "A Low Distortion Map Between Disk and Square" by Peter Shirley and Kenneth Chiu.
glm::vec2 SquareToConcentricDiskMapping(float x, float y)
{
  float phi = 0.0f;
  float r = 0.0f;

  // -- (a,b) is now on [-1,1]�2
  float a = 2.0f * x - 1.0f;
  float b = 2.0f * y - 1.0f;

  if (a > -b) // region 1 or 2
  {
    if (a > b) // region 1, also |a| > |b|
    {
      r = a;
      phi = (Pi / 4.0f) * (b / a);
    }
    else // region 2, also |b| > |a|
    {
      r = b;
      phi = (Pi / 4.0f) * (2.0f - (a / b));
    }
  }
  else // region 3 or 4
  {
    if (a < b) // region 3, also |a| >= |b|, a != 0
    {
      r = -a;
      phi = (Pi / 4.0f) * (4.0f + (b / a));
    }
    else // region 4, |b| >= |a|, but a==0 and b==0 could occur.
    {
      r = -b;
      if (b != 0)
        phi = (Pi / 4.0f) * (6.0f - (a / b));
      else
        phi = 0;
    }
  }

  glm::vec2 result;
  result.x = r * std::cos(phi);
  result.y = r * std::sin(phi);
  return result;
}

// Returns a random direction for sampling a GGX distribution.
// Does everything in world space.
glm::vec3 SampleDirectionGGX(
    const glm::vec3& v,
    const glm::vec3& n,
    float roughness,
    const glm::mat3& tangentToWorld,
    float u1,
    float u2)
{
  float theta = std::atan2(roughness * std::sqrt(u1), std::sqrt(1 - u1));
  float phi = 2 * Pi * u2;

  glm::vec3 h;
  h.x = std::sin(theta) * std::cos(phi);
  h.y = std::sin(theta) * std::sin(phi);
  h.z = std::cos(theta);

  h = glm::normalize(_transformVec3Mat4(h, tangentToWorld));

  float hDotV = std::abs(glm::dot(h, v));
  glm::vec3 sampleDir = 2.0f * hDotV * h - v;
  return glm::normalize(sampleDir);
}

// Returns a point inside of a unit sphere
glm::vec3 SampleSphere(float x1, float x2, float x3, float u1)
{
  glm::vec3 xyz = glm::vec3(x1, x2, x3) * 2.0f - 1.0f;
  float scale = std::pow(u1, 1.0f / 3.0f) / glm::length(xyz);
  return xyz * scale;
}

// Returns a random direction on the unit sphere
glm::vec3 SampleDirectionSphere(float u1, float u2)
{
  float z = u1 * 2.0f - 1.0f;
  float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  float phi = 2 * Pi * u2;
  float x = r * std::cos(phi);
  float y = r * std::sin(phi);

  return glm::vec3(x, y, z);
}

// Returns a random direction on the hemisphere around z = 1
glm::vec3 SampleDirectionHemisphere(float u1, float u2)
{
  float z = u1;
  float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  float phi = 2 * Pi * u2;
  float x = r * std::cos(phi);
  float y = r * std::sin(phi);

  return glm::vec3(x, y, z);
}

// Returns a random cosine-weighted direction on the hemisphere around z = 1
glm::vec3 SampleDirectionCosineHemisphere(float u1, float u2)
{
  glm::vec2 uv = SquareToConcentricDiskMapping(u1, u2);
  float u = uv.x;
  float v = uv.y;

  // Project samples on the disk to the hemisphere to get a
  // cosine weighted distribution
  glm::vec3 dir;
  float r = u * u + v * v;
  dir.x = u;
  dir.y = v;
  dir.z = std::sqrt(std::max(0.0f, 1.0f - r));

  return dir;
}

// Returns a random direction from within a cone with angle == theta
glm::vec3 SampleDirectionCone(float u1, float u2, float cosThetaMax)
{
  float cosTheta = (1.0f - u1) + u1 * cosThetaMax;
  float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
  float phi = u2 * 2.0f * Pi;
  return glm::vec3(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
}

// Returns a direction that samples a rectangular area light
glm::vec3 SampleDirectionRectangularLight(
    float u1,
    float u2,
    const glm::vec3& sourcePos,
    const glm::vec2& lightSize,
    const glm::vec3& lightPos,
    const Quaternion lightOrientation,
    float& distanceToLight)
{
  float x = u1 - 0.5f;
  float y = u2 - 0.5f;

  glm::mat3 lightBasis = lightOrientation.ToMat3();
  glm::vec3 lightBasisX = lightBasis[0];
  glm::vec3 lightBasisY = lightBasis[1];
  glm::vec3 lightBasisZ = lightBasis[3];

  // Pick random sample point
  glm::vec3 samplePos = lightPos + lightBasisX * x * lightSize.x + lightBasisY * y * lightSize.y;

  glm::vec3 sampleDir = samplePos - sourcePos;
  distanceToLight = glm::length(sampleDir);
  if (distanceToLight > 0.0f)
    sampleDir /= distanceToLight;

  return sampleDir;
}

// Returns the PDF for a particular GGX sample
float SampleDirectionGGX_PDF(
    const glm::vec3& n, const glm::vec3& h, const glm::vec3& v, float roughness)
{
  float nDotH = saturate(glm::dot(n, h));
  float hDotV = saturate(glm::dot(h, v));
  float m2 = roughness * roughness;
  float d = m2 / (Pi * square(nDotH * nDotH * (m2 - 1) + 1));
  float pM = d * nDotH;
  return pM / (4 * hDotV);
}

// Returns the (constant) PDF of sampling uniform directions on the unit sphere
float SampleDirectionSphere_PDF() { return 1.0f / (Pi * 4.0f); }

// Returns the (constant) PDF of sampling uniform directions on a unit hemisphere
float SampleDirectionHemisphere_PDF() { return 1.0f / (Pi * 2.0f); }

// Returns the PDF of of a single sample on a cosine-weighted hemisphere
float SampleDirectionCosineHemisphere_PDF(float cosTheta) { return cosTheta / Pi; }

// Returns the PDF of of a single sample on a cosine-weighted hemisphere
float SampleDirectionCosineHemisphere_PDF(const glm::vec3& normal, const glm::vec3& sampleDir)
{
  return saturate(glm::dot(normal, sampleDir)) / Pi;
}

// Returns the PDF of of a single uniform sample within a cone
float SampleDirectionCone_PDF(float cosThetaMax)
{
  return 1.0f / (2.0f * Pi * (1.0f - cosThetaMax));
}

// Returns the PDF of of a single sample on a rectangular area light
float SampleDirectionRectangularLight_PDF(
    const glm::vec2& lightSize,
    const glm::vec3& sampleDir,
    Quaternion lightOrientation,
    float distanceToLight)
{
  glm::vec3 lightBasisZ = _transformVec3Mat4(glm::vec3(0.0f, 0.0f, -1.0f), lightOrientation.ToMat3());
  float areaNDotL = saturate(glm::dot(sampleDir, lightBasisZ));
  return (distanceToLight * distanceToLight) / (areaNDotL * lightSize.x * lightSize.y);
}

// Computes a radical inverse with base 2 using crazy bit-twiddling from "Hacker's Delight"
float RadicalInverseBase2(uint32_t bits)
{
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

// Returns a single 2D point in a Hammersley sequence of length "numSamples", using base 1 and base
// 2
glm::vec2 Hammersley2D(uint64_t sampleIdx, uint64_t numSamples)
{
  return glm::vec2(float(sampleIdx) / float(numSamples), RadicalInverseBase2(uint32_t(sampleIdx)));
}

static uint32_t CMJPermute(uint32_t i, uint32_t l, uint32_t p)
{
  uint32_t w = l - 1;
  w |= w >> 1;
  w |= w >> 2;
  w |= w >> 4;
  w |= w >> 8;
  w |= w >> 16;
  do
  {
    i ^= p;
    i *= 0xe170893d;
    i ^= p >> 16;
    i ^= (i & w) >> 4;
    i ^= p >> 8;
    i *= 0x0929eb3f;
    i ^= p >> 23;
    i ^= (i & w) >> 1;
    i *= 1 | p >> 27;
    i *= 0x6935fa69;
    i ^= (i & w) >> 11;
    i *= 0x74dcb303;
    i ^= (i & w) >> 2;
    i *= 0x9e501cc3;
    i ^= (i & w) >> 2;
    i *= 0xc860a3df;
    i &= w;
    i ^= i >> 5;
  } while (i >= l);
  return (i + p) % l;
}

static float CMJRandFloat(uint32_t i, uint32_t p)
{
  i ^= p;
  i ^= i >> 17;
  i ^= i >> 10;
  i *= 0xb36534e5;
  i ^= i >> 12;
  i ^= i >> 21;
  i *= 0x93fc4795;
  i ^= 0xdf6e307f;
  i ^= i >> 17;
  i *= 1 | p >> 18;
  return i * (1.0f / 4294967808.0f);
}

// Returns a 2D sample from a particular pattern using correlated multi-jittered sampling [Kensler
// 2013]
glm::vec2 SampleCMJ2D(int32_t sampleIdx, int32_t numSamplesX, int32_t numSamplesY, int32_t pattern)
{
  int32_t N = numSamplesX * numSamplesY;
  sampleIdx = CMJPermute(sampleIdx, N, pattern * 0x51633e2d);
  int32_t sx = CMJPermute(sampleIdx % numSamplesX, numSamplesX, pattern * 0x68bc21eb);
  int32_t sy = CMJPermute(sampleIdx / numSamplesX, numSamplesY, pattern * 0x02e5be93);
  float jx = CMJRandFloat(sampleIdx, pattern * 0x967a889b);
  float jy = CMJRandFloat(sampleIdx, pattern * 0x368cc8b7);
  return glm::vec2((sx + (sy + jx) / numSamplesY) / numSamplesX, (sampleIdx + jy) / N);
}

void GenerateRandomSamples2D(glm::vec2* samples, uint64_t numSamples, Random& randomGenerator)
{
  for (uint64_t i = 0; i < numSamples; ++i)
    samples[i] = randomGenerator.RandomFloat2();
}

void GenerateStratifiedSamples2D(
    glm::vec2* samples, uint64_t numSamplesX, uint64_t numSamplesY, Random& randomGenerator)
{
  const glm::vec2 delta = glm::vec2(1.0f / numSamplesX, 1.0f / numSamplesY);
  uint64_t sampleIdx = 0;
  for (uint64_t y = 0; y < numSamplesY; ++y)
  {
    for (uint64_t x = 0; x < numSamplesX; ++x)
    {
      glm::vec2& currSample = samples[sampleIdx];
      currSample = glm::vec2(float(x), float(y)) + randomGenerator.RandomFloat2();
      currSample *= delta;
      currSample = clamp(currSample, 0.0f, OneMinusEpsilon);

      ++sampleIdx;
    }
  }
}

void GenerateGridSamples2D(glm::vec2* samples, uint64_t numSamplesX, uint64_t numSamplesY)
{
  const glm::vec2 delta = glm::vec2(1.0f / numSamplesX, 1.0f / numSamplesY);
  uint64_t sampleIdx = 0;
  for (uint64_t y = 0; y < numSamplesY; ++y)
  {
    for (uint64_t x = 0; x < numSamplesX; ++x)
    {
      glm::vec2& currSample = samples[sampleIdx];
      currSample = glm::vec2(float(x), float(y));
      currSample *= delta;

      ++sampleIdx;
    }
  }
}

// Generates hammersley using base 1 and 2
void GenerateHammersleySamples2D(glm::vec2* samples, uint64_t numSamples)
{
  for (uint64_t i = 0; i < numSamples; ++i)
    samples[i] = Hammersley2D(i, numSamples);
}

// Generates hammersley using arbitrary bases
void GenerateHammersleySamples2D(glm::vec2* samples, uint64_t numSamples, uint64_t dimIdx)
{
  if (dimIdx == 0)
  {
    GenerateHammersleySamples2D(samples, numSamples);
  }
  else
  {
    uint64_t baseIdx0 = dimIdx * 2 - 1;
    uint64_t baseIdx1 = baseIdx0 + 1;
    for (uint64_t i = 0; i < numSamples; ++i)
      samples[i] = glm::vec2(RadicalInverseFast(baseIdx0, i), RadicalInverseFast(baseIdx1, i));
  }
}

void GenerateLatinHypercubeSamples2D(glm::vec2* samples, uint64_t numSamples, Random& rng)
{
  // Generate LHS samples along diagonal
  const glm::vec2 delta = glm::vec2(1.0f / numSamples, 1.0f / numSamples);
  for (uint64_t i = 0; i < numSamples; ++i)
  {
    glm::vec2 currSample = glm::vec2(float(i)) + rng.RandomFloat2();
    currSample *= delta;
    samples[i] = clamp(currSample, 0.0f, OneMinusEpsilon);
  }

  // Permute LHS samples in each dimension
  float* samples1D = reinterpret_cast<float*>(samples);
  const uint64_t numDims = 2;
  for (uint64_t i = 0; i < numDims; ++i)
  {
    for (uint64_t j = 0; j < numSamples; ++j)
    {
      uint64_t other = j + (rng.RandomUint() % (numSamples - j));
      swap(samples1D[numDims * j + i], samples1D[numDims * other + i]);
    }
  }
}

void GenerateCMJSamples2D(glm::vec2* samples, uint64_t numSamplesX, uint64_t numSamplesY, uint32_t pattern)
{
  const uint64_t numSamples = numSamplesX * numSamplesY;
  for (uint64_t i = 0; i < numSamples; ++i)
    samples[i] = SampleCMJ2D(int32_t(i), int32_t(numSamplesX), int32_t(numSamplesY), int32_t(pattern));
}

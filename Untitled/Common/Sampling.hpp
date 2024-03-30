//=================================================================================================
//
//  from MJP's DX12 Sample Framework
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

#include "Utility.hpp"
#include "Quaternion.hpp"
#include <random>

// Random number generation
class Random
{

public:
  void SetSeed(uint32_t seed);
  void SeedWithRandomValue();

  uint32_t RandomUint();
  float RandomFloat();
  glm::vec2 RandomFloat2();

private:
  std::mt19937 engine;
  std::uniform_real_distribution<float> distribution;
};

// Shape sampling functions
glm::vec2 SquareToConcentricDiskMapping(float x, float y, float numSides, float polygonAmount);
glm::vec2 SquareToConcentricDiskMapping(float x, float y);
glm::vec3 SampleDirectionGGX(
    const glm::vec3& v,
    const glm::vec3& n,
    float roughness,
    const glm::mat3& tangentToWorld,
    float u1,
    float u2);
glm::vec3 SampleSphere(float x1, float x2, float x3, float u1);
glm::vec3 SampleDirectionSphere(float u1, float u2);
glm::vec3 SampleDirectionHemisphere(float u1, float u2);
glm::vec3 SampleDirectionCosineHemisphere(float u1, float u2);
glm::vec3 SampleDirectionCone(float u1, float u2, float cosThetaMax);
glm::vec3 SampleDirectionRectangularLight(
    float u1,
    float u2,
    const glm::vec3& sourcePos,
    const glm::vec2& lightSize,
    const glm::vec3& lightPos,
    const Quaternion lightOrientation,
    float& distanceToLight);

// PDF functions
float SampleDirectionGGX_PDF(const glm::vec3& n, const glm::vec3& h, const glm::vec3& v, float roughness);
float SampleDirectionSphere_PDF();
float SampleDirectionHemisphere_PDF();
float SampleDirectionCosineHemisphere_PDF(float cosTheta);
float SampleDirectionCosineHemisphere_PDF(const glm::vec3& normal, const glm::vec3& sampleDir);
float SampleDirectionCone_PDF(float cosThetaMax);
float SampleDirectionRectangularLight_PDF(
    const glm::vec2& lightSize,
    const glm::vec3& sampleDir,
    const Quaternion lightOrientation,
    float distanceToLight);

// Random sample generation
glm::vec2 Hammersley2D(uint64_t sampleIdx, uint64_t numSamples);
glm::vec2 SampleCMJ2D(int32_t sampleIdx, int32_t numSamplesX, int32_t numSamplesY, int32_t pattern);

// Full random sample set generation
void GenerateRandomSamples2D(glm::vec2* samples, uint64_t numSamples, Random& randomGenerator);
void GenerateStratifiedSamples2D(
    glm::vec2* samples, uint64_t numSamplesX, uint64_t numSamplesY, Random& randomGenerator);
void GenerateGridSamples2D(glm::vec2* samples, uint64_t numSamplesX, uint64_t numSamplesY);
void GenerateHammersleySamples2D(glm::vec2* samples, uint64_t numSamples);
void GenerateHammersleySamples2D(glm::vec2* samples, uint64_t numSamples, uint64_t dimIdx);
void GenerateLatinHypercubeSamples2D(glm::vec2* samples, uint64_t numSamples, Random& rng);
void GenerateCMJSamples2D(glm::vec2* samples, uint64_t numSamplesX, uint64_t numSamplesY, uint32_t pattern);

// Helpers
float RadicalInverseBase2(uint32_t bits);
float RadicalInverseFast(uint64_t baseIndex, uint64_t index);

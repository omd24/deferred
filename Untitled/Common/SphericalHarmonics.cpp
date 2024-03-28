//=================================================================================================
//
//  from MJP's DX12 Sample Framework
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

#include "SphericalHarmonics.hpp"
#include "Model.hpp"

SH9 ProjectOntoSH9(const glm::vec3& dir)
{
  SH9 sh;

  // Band 0
  sh.Coefficients[0] = 0.282095f;

  // Band 1
  sh.Coefficients[1] = 0.488603f * dir.y;
  sh.Coefficients[2] = 0.488603f * dir.z;
  sh.Coefficients[3] = 0.488603f * dir.x;

  // Band 2
  sh.Coefficients[4] = 1.092548f * dir.x * dir.y;
  sh.Coefficients[5] = 1.092548f * dir.y * dir.z;
  sh.Coefficients[6] = 0.315392f * (3.0f * dir.z * dir.z - 1.0f);
  sh.Coefficients[7] = 1.092548f * dir.x * dir.z;
  sh.Coefficients[8] = 0.546274f * (dir.x * dir.x - dir.y * dir.y);

  return sh;
}

SH9Color ProjectOntoSH9Color(const glm::vec3& dir, const glm::vec3& color)
{
  SH9 sh = ProjectOntoSH9(dir);
  SH9Color shColor;
  for (uint64_t i = 0; i < 9; ++i)
    shColor.Coefficients[i] = color * sh.Coefficients[i];
  return shColor;
}

glm::vec3 EvalSH9Irradiance(const glm::vec3& dir, const SH9Color& sh)
{
  SH9 dirSH = ProjectOntoSH9(dir);
  dirSH.Coefficients[0] *= CosineA0;
  dirSH.Coefficients[1] *= CosineA1;
  dirSH.Coefficients[2] *= CosineA1;
  dirSH.Coefficients[3] *= CosineA1;
  dirSH.Coefficients[4] *= CosineA2;
  dirSH.Coefficients[5] *= CosineA2;
  dirSH.Coefficients[6] *= CosineA2;
  dirSH.Coefficients[7] *= CosineA2;
  dirSH.Coefficients[8] *= CosineA2;

  glm::vec3 result;
  for (uint64_t i = 0; i < 9; ++i)
    result += dirSH.Coefficients[i] * sh.Coefficients[i];

  return result;
}

H4 ProjectOntoH4(const glm::vec3& dir)
{
  H4 result;

  result[0] = (1.0f / std::sqrt(2.0f * 3.14159f));

  // Band 1
  result[1] = std::sqrt(1.5f / 3.14159f) * dir.y;
  result[2] = std::sqrt(1.5f / 3.14159f) * (2 * dir.z - 1.0f);
  result[3] = std::sqrt(1.5f / 3.14159f) * dir.x;

  return result;
}

float EvalH4(const H4& h, const glm::vec3& dir)
{
  H4 b = ProjectOntoH4(dir);
  return H4::Dot(h, b);
}

H4 ConvertToH4(const SH9& sh)
{
  const float rt2 = sqrt(2.0f);
  const float rt32 = sqrt(3.0f / 2.0f);
  const float rt52 = sqrt(5.0f / 2.0f);
  const float rt152 = sqrt(15.0f / 2.0f);
  const float convMatrix[4][9] = {
      {1.0f / rt2, 0, 0.5f * rt32, 0, 0, 0, 0, 0, 0},
      {0, 1.0f / rt2, 0, 0, 0, (3.0f / 8.0f) * rt52, 0, 0, 0},
      {0, 0, 1.0f / (2.0f * rt2), 0, 0, 0, 0.25f * rt152, 0, 0},
      {0, 0, 0, 1.0f / rt2, 0, 0, 0, (3.0f / 8.0f) * rt52, 0}};

  H4 hBasis;

  for (uint64_t row = 0; row < 4; ++row)
  {
    hBasis.Coefficients[row] = 0.0f;

    for (uint64_t col = 0; col < 9; ++col)
      hBasis.Coefficients[row] += convMatrix[row][col] * sh.Coefficients[col];
  }

  return hBasis;
}

SH9Color ProjectCubemapToSH(const Texture& texture)
{
  assert(texture.Cubemap);

  TextureData<glm::vec4> textureData;
  getTextureData(texture, textureData);
  assert(textureData.NumSlices == 6);
  const uint32_t width = textureData.Width;
  const uint32_t height = textureData.Height;

  SH9Color result;
  float weightSum = 0.0f;
  for (uint32_t face = 0; face < 6; ++face)
  {
    for (uint32_t y = 0; y < height; ++y)
    {
      for (uint32_t x = 0; x < width; ++x)
      {
        const uint32_t idx = face * (width * height) + y * (width) + x;
        glm::vec3 sample = glm::vec3(0);
        sample.x = textureData.Texels[idx].r;
        sample.y = textureData.Texels[idx].g;
        sample.z = textureData.Texels[idx].b;

        float u = (x + 0.5f) / width;
        float v = (y + 0.5f) / height;

        // Account for cubemap texel distribution
        u = u * 2.0f - 1.0f;
        v = v * 2.0f - 1.0f;
        const float temp = 1.0f + u * u + v * v;
        const float weight = 4.0f / (sqrt(temp) * temp);

        glm::vec3 dir = mapXYSToDirection(x, y, face, width, height);
        SH9Color sh9Color = ProjectOntoSH9Color(dir, sample);

        for (uint64_t i = 0; i < 9; ++i)
          sh9Color.Coefficients[i] *= weight;

        result += sh9Color;
        weightSum += weight;
      }
    }
  }

  float m = (4.0f * 3.14159f) / weightSum;
  for (uint64_t i = 0; i < 9; ++i)
    result.Coefficients[i] *= m;

  return result;
}

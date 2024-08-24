//=================================================================================================
//
//  from MJP's DX12 Sample Framework
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

#include "AnalyticalSkyModel.hpp"

#include "HosekSky/ArHosekSkyModel.h"
#include "Model.hpp"
#include "../Common/Spectrum.hpp"
#include "Sampling.hpp"
#include "pix3.h"
#include "d3dx12.h"
#include "../Common/Half.hpp"

static const uint64_t NumIndices = 36;
static const uint64_t NumVertices = 8;

// Actual physical size of the sun, expressed as an angular radius (in radians)
static const float PhysicalSunSize = degToRad(0.27f);
static const float CosPhysicalSunSize = std::cos(PhysicalSunSize);

glm::vec3 perpendicular(const glm::vec3& vec)
{
  assert(glm::length(vec) >= 0.00001f);

  glm::vec3 perp;

  float x = std::abs(vec.x);
  float y = std::abs(vec.y);
  float z = std::abs(vec.z);
  float minVal = std::min(x, y);
  minVal = std::min(minVal, z);

  if (minVal == x)
    perp = glm::cross(vec, glm::vec3(1.0f, 0.0f, 0.0f));
  else if (minVal == y)
    perp = glm::cross(vec, glm::vec3(0.0f, 1.0f, 0.0f));
  else
    perp = glm::cross(vec, glm::vec3(0.0f, 0.0f, 1.0f));

  return glm::normalize(perp);
}

static float AngleBetween(const glm::vec3& dir0, const glm::vec3& dir1)
{
  return std::acos(std::max(glm::dot(dir0, dir1), 0.00001f));
}

// Returns the result of performing a irradiance integral over the portion
// of the hemisphere covered by a region with angular radius = theta
static float IrradianceIntegral(float theta)
{
  float sinTheta = std::sin(theta);
  return Pi * sinTheta * sinTheta;
}

void SkyCache::Init(
    const glm::vec3& sunDirection_,
    float sunSize,
    const glm::vec3& groundAlbedo_,
    float turbidity,
    bool createCubemap)
{
  glm::vec3 sunDirection = sunDirection_;
  glm::vec3 groundAlbedo = groundAlbedo_;
  sunDirection.y = saturate(sunDirection.y);
  sunDirection = glm::normalize(sunDirection);
  turbidity = _clamp(turbidity, 1.0f, 32.0f);
  groundAlbedo = saturate(groundAlbedo);
  sunSize = std::max(sunSize, 0.01f);

  // Do nothing if we're already up-to-date
  if (Initialized() && sunDirection == SunDirection && groundAlbedo == Albedo &&
      turbidity == Turbidity && SunSize == sunSize)
    return;

  Shutdown();

  sunDirection.y = saturate(sunDirection.y);
  sunDirection = glm::normalize(sunDirection);
  turbidity = _clamp(turbidity, 1.0f, 32.0f);
  groundAlbedo = saturate(groundAlbedo);

  float thetaS = AngleBetween(sunDirection, glm::vec3(0, 1, 0));
  float elevation = Pi_2 - thetaS;
  StateR = arhosek_rgb_skymodelstate_alloc_init(turbidity, groundAlbedo.x, elevation);
  StateG = arhosek_rgb_skymodelstate_alloc_init(turbidity, groundAlbedo.y, elevation);
  StateB = arhosek_rgb_skymodelstate_alloc_init(turbidity, groundAlbedo.z, elevation);

  Albedo = groundAlbedo;
  Elevation = elevation;
  SunDirection = sunDirection;
  Turbidity = turbidity;
  SunSize = sunSize;

  // Compute the irradiance of the sun for a surface perpendicular to the sun using monte carlo
  // integration. Note that the solar radiance function provided by the authors of this sky model
  // only works using spectral rendering, so we sample a range of wavelengths and then convert to
  // RGB.
  SampledSpectrum groundAlbedoSpectrum =
      SampledSpectrum::FromRGB(Albedo, SpectrumType::Reflectance);
  SampledSpectrum solarRadiance;

  // Init the Hosek solar radiance model for all wavelengths
  ArHosekSkyModelState* skyStates[NumSpectralSamples] = {};
  for (int32_t i = 0; i < NumSpectralSamples; ++i)
    skyStates[i] = arhosekskymodelstate_alloc_init(thetaS, turbidity, groundAlbedoSpectrum[i]);

  SunIrradiance = glm::vec3(0.0f);

  // Uniformly sample the solid area of the solar disc.
  // Note that we use the *actual* sun size here and not the passed in the sun direction, so that
  // we always end up with the appropriate intensity. This allows changing the size of the sun
  // as it appears in the skydome without actually changing the sun intensity.
  glm::vec3 sunDirX = perpendicular(sunDirection);
  glm::vec3 sunDirY = glm::cross(sunDirection, sunDirX);
  glm::mat3 sunOrientation = glm::mat3(sunDirX, sunDirY, sunDirection);
  sunOrientation = glm::transpose(sunOrientation);

  const uint64_t NumSamples = 8;
  for (uint64_t x = 0; x < NumSamples; ++x)
  {
    for (uint64_t y = 0; y < NumSamples; ++y)
    {
      float u1 = (x + 0.5f) / NumSamples;
      float u2 = (y + 0.5f) / NumSamples;
      glm::vec3 sampleDir = SampleDirectionCone(u1, u2, CosPhysicalSunSize);
      sampleDir = sampleDir * sunOrientation;

      float sampleThetaS = AngleBetween(sampleDir, glm::vec3(0, 1, 0));
      float sampleGamma = AngleBetween(sampleDir, sunDirection);

      for (int32_t i = 0; i < NumSpectralSamples; ++i)
      {
        float wavelength =
            lerp(float(SampledLambdaStart), float(SampledLambdaEnd), i / float(NumSpectralSamples));
        solarRadiance[i] = float(
            arhosekskymodel_solar_radiance(skyStates[i], sampleThetaS, sampleGamma, wavelength));
      }

      glm::vec3 sampleRadiance = solarRadiance.ToRGB();

      // Pre-scale by our FP16 scaling factor, so that we can use the irradiance value
      // and have the resulting lighting still fit comfortably in an FP16 render target
      sampleRadiance *= FP16Scale;

      SunIrradiance += sampleRadiance * saturate(glm::dot(sampleDir, sunDirection));
    }
  }

  // Apply the monte carlo factor of 1 / (PDF * N)
  float pdf = SampleDirectionCone_PDF(CosPhysicalSunSize);
  SunIrradiance *= (1.0f / NumSamples) * (1.0f / NumSamples) * (1.0f / pdf);

  // Account for luminous efficiency and coordinate system scaling
  SunIrradiance *= 683.0f * 100.0f;

  // Clean up
  for (uint64_t i = 0; i < NumSpectralSamples; ++i)
  {
    arhosekskymodelstate_free(skyStates[i]);
    skyStates[i] = nullptr;
  }

  // Compute a uniform solar radiance value such that integrating this radiance over a disc with
  // the provided angular radius
  SunRadiance = SunIrradiance / IrradianceIntegral(degToRad(SunSize));

  if (createCubemap)
  {
    // Make a pre-computed cubemap with the sky radiance values, minus the sun.
    // For this we again pre-scale by our FP16 scale factor so that we can use an FP16 format.
    const uint64_t CubeMapRes = 128;
    std::vector<Half4> texels;
    texels.resize(CubeMapRes * CubeMapRes * 6);

    // We'll also project the sky onto SH coefficients for use during rendering
    sh = SH9Color();
    float weightSum = 0.0f;

    for (uint64_t s = 0; s < 6; ++s)
    {
      for (uint64_t y = 0; y < CubeMapRes; ++y)
      {
        for (uint64_t x = 0; x < CubeMapRes; ++x)
        {
          glm::vec3 dir = mapXYSToDirection(x, y, s, CubeMapRes, CubeMapRes);
          glm::vec3 radiance = Sample(dir);

          uint64_t idx = (s * CubeMapRes * CubeMapRes) + (y * CubeMapRes) + x;
          texels[idx] = Half4(glm::vec4(radiance, 1.0f));

          float u = (x + 0.5f) / CubeMapRes;
          float v = (y + 0.5f) / CubeMapRes;

          // Account for cubemap texel distribution
          u = u * 2.0f - 1.0f;
          v = v * 2.0f - 1.0f;
          const float temp = 1.0f + u * u + v * v;
          const float weight = 4.0f / (std::sqrt(temp) * temp);

          SH9Color result;
          for (uint64_t i = 0; i < 9; ++i)
            result.Coefficients[i] = ProjectOntoSH9Color(dir, radiance).Coefficients[i] * weight;
          sh += result;
          weightSum += weight;
        }
      }
    }

    for (uint64_t i = 0; i < 9; ++i)
      sh.Coefficients[i] *= (4.0f * 3.14159f) / weightSum;

    create2DTexture(
        CubeMap, CubeMapRes, CubeMapRes, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, true, texels.data());
  }
}

void SkyCache::Shutdown()
{
  if (StateR != nullptr)
  {
    arhosekskymodelstate_free(StateR);
    StateR = nullptr;
  }

  if (StateG != nullptr)
  {
    arhosekskymodelstate_free(StateG);
    StateG = nullptr;
  }

  if (StateB != nullptr)
  {
    arhosekskymodelstate_free(StateB);
    StateB = nullptr;
  }

  CubeMap.Shutdown();
  Turbidity = 0.0f;
  Albedo = glm::vec3(0.0f);
  Elevation = 0.0f;
  SunDirection = glm::vec3(0.0f);
  SunRadiance = glm::vec3(0.0f);
  SunIrradiance = glm::vec3(0.0f);
  sh = SH9Color();
}

SkyCache::~SkyCache() { assert(Initialized() == false); }

glm::vec3 SkyCache::Sample(glm::vec3 sampleDir) const
{
  assert(StateR != nullptr);

  float gamma = AngleBetween(sampleDir, SunDirection);
  float theta = AngleBetween(sampleDir, glm::vec3(0, 1, 0));

  glm::vec3 radiance;

  radiance.x = float(arhosek_tristim_skymodel_radiance(StateR, theta, gamma, 0));
  radiance.y = float(arhosek_tristim_skymodel_radiance(StateG, theta, gamma, 1));
  radiance.z = float(arhosek_tristim_skymodel_radiance(StateB, theta, gamma, 2));

  // Multiply by standard luminous efficacy of 683 lm/W to bring us in line with the photometric
  // units used during rendering
  radiance *= 683.0f;

  return radiance * FP16Scale;
}

// == Skybox ======================================================================================

enum RootParams : uint32_t
{
  RootParam_StandardDescriptors = 0,
  RootParam_VSCBuffer,
  RootParam_PSCBuffer,

  NumRootParams
};

Skybox::Skybox() {}

Skybox::~Skybox() {}

void Skybox::Initialize()
{
  // Load the shaders
#if defined(_DEBUG)
  UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  UINT compileFlags = 0;
#endif
  // Unbounded size descriptor tables
  compileFlags |= D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
  WCHAR assetsPath[512];
  getAssetsPath(assetsPath, _countof(assetsPath));
  std::wstring shaderPath = assetsPath;
  shaderPath += L"Shaders\\AnalyticalSkyModel.hlsl";

  compileShader(
    "skybox vs",
    shaderPath.c_str(),
    0,
    nullptr,
    compileFlags,
    ShaderType::Vertex,
    "SkyboxVS",
    vertexShader);

  compileShader(
    "skybox ps",
    shaderPath.c_str(),
    0,
    nullptr,
    compileFlags,
    ShaderType::Pixel,
    "SkyboxPS",
    pixelShader);

  {
    // Make a root signature
    D3D12_ROOT_PARAMETER1 rootParameters[NumRootParams] = {};
    rootParameters[RootParam_StandardDescriptors].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[RootParam_StandardDescriptors].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[RootParam_StandardDescriptors].DescriptorTable.pDescriptorRanges =
        StandardDescriptorRanges();
    rootParameters[RootParam_StandardDescriptors].DescriptorTable.NumDescriptorRanges =
        NumStandardDescriptorRanges;

    rootParameters[RootParam_VSCBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[RootParam_VSCBuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[RootParam_VSCBuffer].Descriptor.RegisterSpace = 0;
    rootParameters[RootParam_VSCBuffer].Descriptor.ShaderRegister = 0;

    rootParameters[RootParam_PSCBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[RootParam_PSCBuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[RootParam_PSCBuffer].Descriptor.RegisterSpace = 0;
    rootParameters[RootParam_PSCBuffer].Descriptor.ShaderRegister = 0;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0] = GetStaticSamplerState(SamplerState::LinearClamp);

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    CreateRootSignature(&rootSignature, rootSignatureDesc);
  }

  // Create and initialize the vertex and index buffers
  glm::vec3 verts[NumVertices] = {
      glm::vec3(-1.0f, 1.0f, 1.0f),
      glm::vec3(1.0f, 1.0f, 1.0f),
      glm::vec3(1.0f, -1.0f, 1.0f),
      glm::vec3(-1.0f, -1.0f, 1.0f),
      glm::vec3(1.0f, 1.0f, -1.0f),
      glm::vec3(-1.0f, 1.0f, -1.0f),
      glm::vec3(-1.0f, -1.0f, -1.0f),
      glm::vec3(1.0f, -1.0f, -1.0f),
  };

  StructuredBufferInit vbInit;
  vbInit.Stride = sizeof(glm::vec3);
  vbInit.NumElements = uint32_t(NumVertices);
  vbInit.InitData = verts;
  vbInit.Name = L"Skybox Vertex Buffer";
  vertexBuffer.init(vbInit);

  uint16_t indices[NumIndices] = {
      0, 1, 2, 2, 3, 0, // Front
      1, 4, 7, 7, 2, 1, // Right
      4, 5, 6, 6, 7, 4, // Back
      5, 0, 3, 3, 6, 5, // Left
      5, 4, 1, 1, 0, 5, // Top
      3, 2, 7, 7, 6, 3  // Bottom
  };

  FormattedBufferInit ibInit;
  ibInit.Format = DXGI_FORMAT_R16_UINT;
  ibInit.NumElements = uint32_t(NumIndices);
  ibInit.InitData = indices;
  ibInit.Name = L"SpriteRenderer Index Buffer";
  indexBuffer.init(ibInit);
}

void Skybox::Shutdown()
{
  DestroyPSOs();

  vertexBuffer.deinit();
  indexBuffer.deinit();
  rootSignature->Release();
}

void Skybox::CreatePSOs(DXGI_FORMAT rtFormat, DXGI_FORMAT depthFormat, uint32_t numMSAASamples)
{
  D3D12_INPUT_ELEMENT_DESC inputElements[] = {
      {"POSITION",
       0,
       DXGI_FORMAT_R32G32B32_FLOAT,
       0,
       D3D12_APPEND_ALIGNED_ELEMENT,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
       0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.GetInterfacePtr());
  psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.GetInterfacePtr());
  psoDesc.RasterizerState = GetRasterizerState(RasterizerState::NoCull);
  psoDesc.BlendState = GetBlendState(BlendState::Disabled);
  psoDesc.DepthStencilState = GetDepthState(DepthState::Enabled);
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtFormat;
  psoDesc.DSVFormat = depthFormat;
  psoDesc.SampleDesc.Count = numMSAASamples;
  psoDesc.SampleDesc.Quality = numMSAASamples > 1 ? StandardMSAAPattern : 0;
  psoDesc.InputLayout.pInputElementDescs = inputElements;
  psoDesc.InputLayout.NumElements = arrayCount32(inputElements);
  D3D_EXEC_CHECKED(g_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));
}

void Skybox::DestroyPSOs() { pipelineState->Release(); }

void Skybox::RenderCommon(
    ID3D12GraphicsCommandList* cmdList,
    const Texture* environmentMap,
    const glm::mat4& view,
    const glm::mat4& projection,
    glm::vec3 scale)
{
  // Set pipeline state
  cmdList->SetPipelineState(pipelineState);
  cmdList->SetGraphicsRootSignature(rootSignature);
  cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  BindStandardDescriptorTable(cmdList, RootParam_StandardDescriptors, CmdListMode::Graphics);

  // Bind the constant buffers
  vsConstants.View = view;
  vsConstants.Projection = projection;
  BindTempConstantBuffer(cmdList, vsConstants, RootParam_VSCBuffer, CmdListMode::Graphics);

  psConstants.Scale = scale;
  psConstants.EnvMapIdx = environmentMap->SRV;
  BindTempConstantBuffer(cmdList, psConstants, RootParam_PSCBuffer, CmdListMode::Graphics);

  // Set vertex/index buffers
  D3D12_VERTEX_BUFFER_VIEW vbView = vertexBuffer.vbView();
  cmdList->IASetVertexBuffers(0, 1, &vbView);

  D3D12_INDEX_BUFFER_VIEW ibView = indexBuffer.IBView();
  cmdList->IASetIndexBuffer(&ibView);

  // Draw
  cmdList->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
}

void Skybox::RenderEnvironmentMap(
    ID3D12GraphicsCommandList* cmdList,
    const glm::mat4& view,
    const glm::mat4& projection,
    const Texture* environmentMap,
    glm::vec3 scale)
{
  PIXBeginEvent(cmdList, 0, "Skybox Render Environment Map");

  psConstants.CosSunAngularRadius = 0.0f;
  RenderCommon(cmdList, environmentMap, view, projection, scale);

  PIXEndEvent(cmdList);
}

void Skybox::RenderSky(
    ID3D12GraphicsCommandList* cmdList,
    const glm::mat4& view,
    const glm::mat4& projection,
    const SkyCache& skyCache,
    bool enableSun,
    const glm::vec3& scale)
{
  PIXBeginEvent(cmdList, 0, "Skybox Render Sky");

  assert(skyCache.Initialized());

  // Set the pixel shader constants
  psConstants.SunDirection = skyCache.SunDirection;
  psConstants.Scale = scale;
  if (enableSun)
  {
    glm::vec3 sunColor = skyCache.SunRadiance;
    float maxComponent = std::max(sunColor.x, std::max(sunColor.y, sunColor.z));
    if (maxComponent > FP16Max)
      sunColor *= (FP16Max / maxComponent);
    psConstants.SunColor = glm::clamp(sunColor, glm::vec3(0.0f), glm::vec3(FP16Max));
    psConstants.CosSunAngularRadius = std::cos(degToRad(skyCache.SunSize));
  }
  else
  {
    psConstants.SunColor = glm::vec3(0.0f);
    psConstants.CosSunAngularRadius = 0.0f;
  }

  RenderCommon(cmdList, &skyCache.CubeMap, view, projection, scale);

  PIXEndEvent(cmdList);
}

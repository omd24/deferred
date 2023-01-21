#include "Model.hpp"

// DirectX Tex
#include "..\\Externals\\DirectXTex July 2017\\Include\\DirectXTex.h"
#ifdef _DEBUG
#  pragma comment(lib, "..\\Externals\\DirectXTex July 2017\\Lib 2017\\Debug\\DirectXTex.lib")
#else
#  pragma comment(lib, "..\\Externals\\DirectXTex July 2017\\Lib 2017\\Release\\DirectXTex.lib")
#endif

// Assimp
#include "..\\Externals\\Assimp-3.1.1\\include\\Importer.hpp"
#include "..\\Externals\\Assimp-3.1.1\\include\\scene.h"
#include "..\\Externals\\Assimp-3.1.1\\include\\postprocess.h"
#pragma comment(lib, "..\\Externals\\Assimp-3.1.1\\lib\\assimp.lib")

static const wchar_t* DefaultTextures[] = {
    L"..\\Content\\Textures\\Default.dds",          // Albedo
    L"..\\Content\\Textures\\DefaultNormalMap.dds", // Normal
    L"..\\Content\\Textures\\DefaultRoughness.dds", // Roughness
    L"..\\Content\\Textures\\DefaultBlack.dds",     // Metallic
};
static_assert(arrayCount32(DefaultTextures) == uint32_t(MaterialTextures::Count));

//---------------------------------------------------------------------------//
// Helpers
//---------------------------------------------------------------------------//

static constexpr float maxFloat = std::numeric_limits<float>::max();

// Scale factor used for storing physical light units in fp16 floats (equal to 2^-10).
// https://www.reedbeta.com/blog/artist-friendly-hdr-with-exposure-values/#fitting-into-half-float
// We basically shift input light values by -10EV (i.e., 2^-10) here and reapply the scale factor
// during exposure/tone-mapping to get back the real values.
const float FP16Scale = 0.0009765625f;

static glm::vec3 convertVector(const aiVector3D& vec) { return glm::vec3(vec.x, vec.y, vec.z); }

static glm::vec3 convertColor(const aiColor3D& clr) { return glm::vec3(clr.r, clr.g, clr.b); }

static glm::mat4 convertMatrix(const aiMatrix4x4& mat)
{
  glm::mat4 ret = glm::mat4(
      glm::vec4(mat.a1, mat.a2, mat.a3, mat.a4),
      glm::vec4(mat.b1, mat.b2, mat.b3, mat.b4),
      glm::vec4(mat.c1, mat.c2, mat.c3, mat.c4),
      glm::vec4(mat.d1, mat.d2, mat.d3, mat.d4));

  return glm::transpose(ret);
}
static glm::mat3 convertMatrix4To3(const aiMatrix4x4& mat)
{
  glm::mat3 ret = glm::mat3(
      glm::vec3(mat.a1, mat.a2, mat.a3),
      glm::vec3(mat.b1, mat.b2, mat.b3),
      glm::vec3(mat.c1, mat.c2, mat.c3));

  return glm::transpose(ret);
}
void loadTexture(ID3D12Device* dev, Texture& texture, const wchar_t* filePath, bool forceSRGB)
{
  g_Device = dev;

  texture.Shutdown();
  if (fileExists(filePath) == false)
    throw std::exception("Texture file does not exist");

  DirectX::ScratchImage image;

  const std::wstring extension = getFileExtension(filePath);
  if (extension == L"DDS" || extension == L"dds")
  {
    DirectX::LoadFromDDSFile(filePath, DirectX::DDS_FLAGS_NONE, nullptr, image);
  }
  else if (extension == L"TGA" || extension == L"tga")
  {
    DirectX::ScratchImage tempImage;
    D3D_EXEC_CHECKED(DirectX::LoadFromTGAFile(filePath, nullptr, tempImage));
    DirectX::GenerateMipMaps(
        *tempImage.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 0, image, false);
  }
  else
  {
    DirectX::ScratchImage tempImage;
    D3D_EXEC_CHECKED(
        DirectX::LoadFromWICFile(filePath, DirectX::WIC_FLAGS_NONE, nullptr, tempImage));
    DirectX::GenerateMipMaps(
        *tempImage.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 0, image, false);
  }

  const DirectX::TexMetadata& metaData = image.GetMetadata();
  DXGI_FORMAT format = metaData.format;
  if (forceSRGB)
    format = DirectX::MakeSRGB(format);

  const bool is3D = metaData.dimension == DirectX::TEX_DIMENSION_TEXTURE3D;

  D3D12_RESOURCE_DESC textureDesc = {};
  textureDesc.MipLevels = uint16_t(metaData.mipLevels);
  textureDesc.Format = format;
  textureDesc.Width = uint32_t(metaData.width);
  textureDesc.Height = uint32_t(metaData.height);
  textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
  textureDesc.DepthOrArraySize = is3D ? uint16_t(metaData.depth) : uint16_t(metaData.arraySize);
  textureDesc.SampleDesc.Count = 1;
  textureDesc.SampleDesc.Quality = 0;
  textureDesc.Dimension =
      is3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  textureDesc.Alignment = 0;

  ID3D12Device* device = g_Device;
  (device->CreateCommittedResource(
      GetDefaultHeapProps(),
      D3D12_HEAP_FLAG_NONE,
      &textureDesc,
      D3D12_RESOURCE_STATE_COMMON,
      nullptr,
      IID_PPV_ARGS(&texture.Resource)));
  texture.Resource->SetName(filePath);

  PersistentDescriptorAlloc srvAlloc = SRVDescriptorHeap.AllocatePersistent();
  texture.SRV = srvAlloc.Index;

  const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDescPtr = nullptr;
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  if (metaData.IsCubemap())
  {
    assert(metaData.arraySize == 6);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = uint32_t(metaData.mipLevels);
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    srvDescPtr = &srvDesc;
  }

  for (uint32_t i = 0; i < SRVDescriptorHeap.NumHeaps; ++i)
    device->CreateShaderResourceView(texture.Resource, srvDescPtr, srvAlloc.Handles[i]);

  const uint64_t numSubResources = metaData.mipLevels * metaData.arraySize;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT* layouts = (D3D12_PLACED_SUBRESOURCE_FOOTPRINT*)_alloca(
      sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) * numSubResources);
  uint32_t* numRows = (uint32_t*)_alloca(sizeof(uint32_t) * numSubResources);
  uint64_t* rowSizes = (uint64_t*)_alloca(sizeof(uint64_t) * numSubResources);

  uint64_t textureMemSize = 0;
  device->GetCopyableFootprints(
      &textureDesc, 0, uint32_t(numSubResources), 0, layouts, numRows, rowSizes, &textureMemSize);

  // Get a GPU upload buffer
  UploadContext uploadContext = resourceUploadBegin(textureMemSize);
  uint8_t* uploadMem = reinterpret_cast<uint8_t*>(uploadContext.CpuAddress);

  for (uint64_t arrayIdx = 0; arrayIdx < metaData.arraySize; ++arrayIdx)
  {

    for (uint64_t mipIdx = 0; mipIdx < metaData.mipLevels; ++mipIdx)
    {
      const uint64_t subResourceIdx = mipIdx + (arrayIdx * metaData.mipLevels);

      const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& subResourceLayout = layouts[subResourceIdx];
      const uint64_t subResourceHeight = numRows[subResourceIdx];
      const uint64_t subResourcePitch = subResourceLayout.Footprint.RowPitch;
      const uint64_t subResourceDepth = subResourceLayout.Footprint.Depth;
      uint8_t* dstSubResourceMem = reinterpret_cast<uint8_t*>(uploadMem) + subResourceLayout.Offset;

      for (uint64_t z = 0; z < subResourceDepth; ++z)
      {
        const DirectX::Image* subImage = image.GetImage(mipIdx, arrayIdx, z);
        assert(subImage != nullptr);
        const uint8_t* srcSubResourceMem = subImage->pixels;

        for (uint64_t y = 0; y < subResourceHeight; ++y)
        {
          memcpy(
              dstSubResourceMem, srcSubResourceMem, std::min(subResourcePitch, subImage->rowPitch));
          dstSubResourceMem += subResourcePitch;
          srcSubResourceMem += subImage->rowPitch;
        }
      }
    }
  }

  for (uint64_t subResourceIdx = 0; subResourceIdx < numSubResources; ++subResourceIdx)
  {
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture.Resource;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = uint32_t(subResourceIdx);
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadContext.Resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = layouts[subResourceIdx];
    src.PlacedFootprint.Offset += uploadContext.ResourceOffset;
    uploadContext.CmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  }

  resourceUploadEnd(uploadContext);

  texture.Width = uint32_t(metaData.width);
  texture.Height = uint32_t(metaData.height);
  texture.Depth = uint32_t(metaData.depth);
  texture.NumMips = uint32_t(metaData.mipLevels);
  texture.ArraySize = uint32_t(metaData.arraySize);
  texture.Format = metaData.format;
  texture.Cubemap = metaData.IsCubemap() ? 1 : 0;
}
void loadMaterialResources(
    ID3D12Device* dev,
    std::vector<MeshMaterial>& materials,
    const std::wstring& directory,
    bool forceSRGB,
    std::vector<MaterialTexture*>& materialTextures)
{
  const uint64_t numMaterials = materials.size();
  for (uint64_t matIdx = 0; matIdx < numMaterials; ++matIdx)
  {
    MeshMaterial& material = materials[matIdx];
    for (uint64_t texType = 0; texType < uint64_t(MaterialTextures::Count); ++texType)
    {
      material.Textures[texType] = nullptr;

      std::wstring path = directory + material.TextureNames[texType];
      if (material.TextureNames[texType].length() == 0 || fileExists(path.c_str()) == false)
        path = DefaultTextures[texType];

      const uint64_t numLoaded = materialTextures.size();
      for (uint64_t i = 0; i < numLoaded; ++i)
      {
        if (materialTextures[i]->Name == path)
        {
          material.Textures[texType] = &materialTextures[i]->Texture;
          material.TextureIndices[texType] = uint32_t(i);
          break;
        }
      }

      if (material.Textures[texType] == nullptr)
      {
        MaterialTexture* newMatTexture = new MaterialTexture();
        newMatTexture->Name = path;
        bool useSRGB = forceSRGB && texType == uint64_t(MaterialTextures::Albedo);
        loadTexture(dev, newMatTexture->Texture, path.c_str(), useSRGB ? true : false);
        materialTextures.push_back(newMatTexture);
        uint64_t idx = materialTextures.size() - 1;

        material.Textures[texType] = &newMatTexture->Texture;
        material.TextureIndices[texType] = uint32_t(idx);
      }
    }
  }
}

//---------------------------------------------------------------------------//
// Mesh
//---------------------------------------------------------------------------//
// Init from loaded files
void Mesh::InitFromAssimpMesh(
    const aiMesh& assimpMesh, float sceneScale, MeshVertex* dstVertices, uint16_t* dstIndices)
{
  numVertices = assimpMesh.mNumVertices;
  numIndices = assimpMesh.mNumFaces * 3;

  indexType = IndexType::Index16Bit;
  if (numVertices > 0xFFFF)
  {
    assert(false && "32-bit indices not currently supported");
    // indexSize = 4;
    // indexType = IndexType::Index32Bit;
  }

  if (assimpMesh.HasPositions())
  {
    // Compute the AABB of the mesh, and copy the positions
    aabbMin = glm::vec3(maxFloat);
    aabbMax = glm::vec3(-maxFloat);

    for (uint64_t i = 0; i < numVertices; ++i)
    {
      glm::vec3 position = convertVector(assimpMesh.mVertices[i]) * sceneScale;
      aabbMin.x = std::min(aabbMin.x, position.x);
      aabbMin.y = std::min(aabbMin.y, position.y);
      aabbMin.z = std::min(aabbMin.z, position.z);

      aabbMax.x = std::max(aabbMax.x, position.x);
      aabbMax.y = std::max(aabbMax.y, position.y);
      aabbMax.z = std::max(aabbMax.z, position.z);

      dstVertices[i].Position = position;
    }
  }

  if (assimpMesh.HasNormals())
  {
    for (uint64_t i = 0; i < numVertices; ++i)
      dstVertices[i].Normal = convertVector(assimpMesh.mNormals[i]);
  }

  if (assimpMesh.HasTextureCoords(0))
  {
    for (uint64_t i = 0; i < numVertices; ++i)
    {
      glm::vec3 UVW = convertVector(assimpMesh.mTextureCoords[0][i]);
      dstVertices[i].UV = glm::vec2(UVW.x, UVW.y);
    }
  }

  if (assimpMesh.HasTangentsAndBitangents())
  {
    for (uint64_t i = 0; i < numVertices; ++i)
    {
      dstVertices[i].Tangent = convertVector(assimpMesh.mTangents[i]);
      dstVertices[i].Bitangent = convertVector(assimpMesh.mBitangents[i]) * -1.0f;
    }
  }

  // Copy the index data
  const uint64_t numTriangles = assimpMesh.mNumFaces;
  for (uint64_t triIdx = 0; triIdx < numTriangles; ++triIdx)
  {
    dstIndices[triIdx * 3 + 0] = uint16_t(assimpMesh.mFaces[triIdx].mIndices[0]);
    dstIndices[triIdx * 3 + 1] = uint16_t(assimpMesh.mFaces[triIdx].mIndices[1]);
    dstIndices[triIdx * 3 + 2] = uint16_t(assimpMesh.mFaces[triIdx].mIndices[2]);
  }

  meshParts.resize(1);
  MeshPart& part = meshParts[0];
  part.IndexStart = 0;
  part.IndexCount = numIndices;
  part.VertexStart = 0;
  part.VertexCount = numVertices;
  part.MaterialIdx = assimpMesh.mMaterialIndex;
}
static const uint64_t NumBoxVerts = 24;
static const uint64_t NumBoxIndices = 36;

// Procedural generation
void Mesh::InitBox(
    const glm::vec3& dimensions,
    const glm::vec3& position,
    const glm::quat& orientation,
    uint32_t materialIdx,
    MeshVertex* dstVertices,
    uint16_t* dstIndices)
{
  uint64_t vIdx = 0;

  // Top
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, 1.0f, 1.0f),
      glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec2(0.0f, 0.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, -1.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, 1.0f, 1.0f),
      glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec2(1.0f, 0.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, -1.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, 1.0f, -1.0f),
      glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec2(1.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, -1.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, 1.0f, -1.0f),
      glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec2(0.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, -1.0f));

  // Bottom
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, -1.0f, -1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f),
      glm::vec2(0.0f, 0.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, -1.0f, -1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f),
      glm::vec2(1.0f, 0.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, -1.0f, 1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f),
      glm::vec2(1.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, -1.0f, 1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f),
      glm::vec2(0.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f));

  // Front
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, 1.0f, -1.0f),
      glm::vec3(0.0f, 0.0f, -1.0f),
      glm::vec2(0.0f, 0.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, 1.0f, -1.0f),
      glm::vec3(0.0f, 0.0f, -1.0f),
      glm::vec2(1.0f, 0.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, -1.0f, -1.0f),
      glm::vec3(0.0f, 0.0f, -1.0f),
      glm::vec2(1.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, -1.0f, -1.0f),
      glm::vec3(0.0f, 0.0f, -1.0f),
      glm::vec2(0.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));

  // Back
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, 1.0f, 1.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec2(0.0f, 0.0f),
      glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, 1.0f, 1.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec2(1.0f, 0.0f),
      glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, -1.0f, 1.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec2(1.0f, 1.0f),
      glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, -1.0f, 1.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec2(0.0f, 1.0f),
      glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));

  // Left
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, 1.0f, 1.0f),
      glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec2(0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, -1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, 1.0f, -1.0f),
      glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec2(1.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, -1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, -1.0f, -1.0f),
      glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec2(1.0f, 1.0f),
      glm::vec3(0.0f, 0.0f, -1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, -1.0f, 1.0f),
      glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec2(0.0f, 1.0f),
      glm::vec3(0.0f, 0.0f, -1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));

  // Right
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, 1.0f, -1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec2(0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, 1.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec2(1.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, -1.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec2(1.0f, 1.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, -1.0f, -1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec2(0.0f, 1.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f));

  for (uint64_t i = 0; i < NumBoxVerts; ++i)
    dstVertices[i].Transform(position, dimensions * 0.5f, orientation);

  uint64_t iIdx = 0;

  // Top
  dstIndices[iIdx++] = 0;
  dstIndices[iIdx++] = 1;
  dstIndices[iIdx++] = 2;
  dstIndices[iIdx++] = 2;
  dstIndices[iIdx++] = 3;
  dstIndices[iIdx++] = 0;

  // Bottom
  dstIndices[iIdx++] = 4 + 0;
  dstIndices[iIdx++] = 4 + 1;
  dstIndices[iIdx++] = 4 + 2;
  dstIndices[iIdx++] = 4 + 2;
  dstIndices[iIdx++] = 4 + 3;
  dstIndices[iIdx++] = 4 + 0;

  // Front
  dstIndices[iIdx++] = 8 + 0;
  dstIndices[iIdx++] = 8 + 1;
  dstIndices[iIdx++] = 8 + 2;
  dstIndices[iIdx++] = 8 + 2;
  dstIndices[iIdx++] = 8 + 3;
  dstIndices[iIdx++] = 8 + 0;

  // Back
  dstIndices[iIdx++] = 12 + 0;
  dstIndices[iIdx++] = 12 + 1;
  dstIndices[iIdx++] = 12 + 2;
  dstIndices[iIdx++] = 12 + 2;
  dstIndices[iIdx++] = 12 + 3;
  dstIndices[iIdx++] = 12 + 0;

  // Left
  dstIndices[iIdx++] = 16 + 0;
  dstIndices[iIdx++] = 16 + 1;
  dstIndices[iIdx++] = 16 + 2;
  dstIndices[iIdx++] = 16 + 2;
  dstIndices[iIdx++] = 16 + 3;
  dstIndices[iIdx++] = 16 + 0;

  // Right
  dstIndices[iIdx++] = 20 + 0;
  dstIndices[iIdx++] = 20 + 1;
  dstIndices[iIdx++] = 20 + 2;
  dstIndices[iIdx++] = 20 + 2;
  dstIndices[iIdx++] = 20 + 3;
  dstIndices[iIdx++] = 20 + 0;

  const uint32_t indexSize = 2;
  UNREFERENCED_PARAMETER(indexSize);
  indexType = IndexType::Index16Bit;

  numVertices = uint32_t(NumBoxVerts);
  numIndices = uint32_t(NumBoxIndices);

  meshParts.resize(1);
  MeshPart& part = meshParts[0];
  part.IndexStart = 0;
  part.IndexCount = numIndices;
  part.VertexStart = 0;
  part.VertexCount = numVertices;
  part.MaterialIdx = materialIdx;
}

const uint64_t NumPlaneVerts = 4;
const uint64_t NumPlaneIndices = 6;

void Mesh::InitPlane(
    const glm::vec2& dimensions,
    const glm::vec3& position,
    const glm::quat& orientation,
    uint32_t materialIdx,
    MeshVertex* dstVertices,
    uint16_t* dstIndices)
{
  uint64_t vIdx = 0;

  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec2(0.0f, 0.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, -1.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec2(1.0f, 0.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, -1.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(1.0f, 0.0f, -1.0f),
      glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec2(1.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, -1.0f));
  dstVertices[vIdx++] = MeshVertex(
      glm::vec3(-1.0f, 0.0f, -1.0f),
      glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec2(0.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, -1.0f));

  for (uint64_t i = 0; i < NumPlaneVerts; ++i)
    dstVertices[i].Transform(
        position, glm::vec3(dimensions.x, 1.0f, dimensions.y) * 0.5f, orientation);

  uint64_t iIdx = 0;
  dstIndices[iIdx++] = 0;
  dstIndices[iIdx++] = 1;
  dstIndices[iIdx++] = 2;
  dstIndices[iIdx++] = 2;
  dstIndices[iIdx++] = 3;
  dstIndices[iIdx++] = 0;

  indexType = IndexType::Index16Bit;

  numVertices = uint32_t(NumPlaneVerts);
  numIndices = uint32_t(NumPlaneIndices);

  meshParts.resize(1);
  MeshPart& part = meshParts[0];
  part.IndexStart = 0;
  part.IndexCount = numIndices;
  part.VertexStart = 0;
  part.VertexCount = numVertices;
  part.MaterialIdx = materialIdx;
}

void Mesh::InitCommon(
    const MeshVertex* p_Vertices,
    const uint16_t* p_Indices,
    uint64_t p_VbAddress,
    uint64_t p_IbAddress,
    uint64_t p_VtxOffset,
    uint64_t p_IdxOffset)
{
  assert(meshParts.size() > 0);

  vertices = p_Vertices;
  indices = p_Indices;
  vtxOffset = uint32_t(p_VtxOffset);
  idxOffset = uint32_t(p_IdxOffset);

  vbView.BufferLocation = p_VbAddress;
  vbView.SizeInBytes = sizeof(MeshVertex) * numVertices;
  vbView.StrideInBytes = sizeof(MeshVertex);

  ibView.Format = IndexBufferFormat();
  ibView.SizeInBytes = IndexSize() * numIndices;
  ibView.BufferLocation = p_IbAddress;
}

void Mesh::Shutdown()
{
  numVertices = 0;
  numIndices = 0;
  meshParts.clear();
  vertices = nullptr;
  indices = nullptr;
}

//---------------------------------------------------------------------------//
// Model
//---------------------------------------------------------------------------//
// A note from Matt Pettineo:
// For some reason the roughness maps aren't coming through in the SHININESS
// channel after Assimp import Loading from file formats

static const wchar_t* SponzaRoughnessMaps[] = {
    L"Sponza_Thorn_roughness.png",
    L"VasePlant_roughness.png",
    L"VaseRound_roughness.png",
    L"Background_Roughness.png",
    L"Sponza_Bricks_a_Roughness.png",
    L"Sponza_Arch_roughness.png",
    L"Sponza_Ceiling_roughness.png",
    L"Sponza_Column_a_roughness.png",
    L"Sponza_Floor_roughness.png",
    L"Sponza_Column_c_roughness.png",
    L"Sponza_Details_roughness.png",
    L"Sponza_Column_b_roughness.png",
    nullptr,
    L"Sponza_FlagPole_roughness.png",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    L"ChainTexture_Roughness.png",
    L"VaseHanging_roughness.png",
    L"Vase_roughness.png",
    L"Lion_Roughness.png",
    L"Sponza_Roof_roughness.png"};

void Model::CreateWithAssimp(ID3D12Device* dev, const ModelLoadSettings& settings)
{

  const wchar_t* filePath = settings.FilePath;
  assert(filePath != nullptr);
  if (fileExists(filePath) == false)
    throw std::exception("Model file does not exist");

  writeLog("Loading scene '%ls' with Assimp...", filePath);

  std::string fileNameAnsi = WideStrToStr(filePath);

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(fileNameAnsi, 0);

  if (scene == nullptr)
    throw std::exception("Failed to load scene");

  if (scene->mNumMeshes == 0)
    throw std::exception("Scene has no meshes");

  if (scene->mNumMaterials == 0)
    throw std::exception("Scene has no materials");

  fileDirectory = getDirectoryFromFilePath(filePath);
  forceSRGB = settings.ForceSRGB;

  // Grab the lights before we process the scene
  spotLights.resize(scene->mNumLights);
  pointLights.resize(scene->mNumLights);

  uint64_t numSpotLights = 0;
  uint64_t numPointLights = 0;
  for (uint64_t i = 0; i < scene->mNumLights; ++i)
  {
    const aiLight& srcLight = *scene->mLights[i];
    if (srcLight.mType == aiLightSource_SPOT)
    {
      // Assimp seems to mess up when importing spot light transforms for FBX
      std::string translationName =
          MakeString("%s_$AssimpFbx$_Translation", srcLight.mName.C_Str());
      const aiNode* translationNode = scene->mRootNode->FindNode(translationName.c_str());
      if (translationNode == nullptr)
        continue;

      std::string rotationName = MakeString("%s_$AssimpFbx$_Rotation", srcLight.mName.C_Str());
      const aiNode* rotationNode = translationNode->FindNode(rotationName.c_str());
      if (rotationNode == nullptr)
        continue;

      ModelSpotLight& dstLight = spotLights[numSpotLights++];

      glm::mat4x4 translation = (convertMatrix(translationNode->mTransformation));
      dstLight.Position = translation[3] * settings.SceneScale;
      dstLight.Position.z *= -1.0f;
      dstLight.Intensity = convertColor(srcLight.mColorDiffuse) * FP16Scale;
      dstLight.AngularAttenuation.x = srcLight.mAngleInnerCone;
      dstLight.AngularAttenuation.y = srcLight.mAngleOuterCone;

      glm::mat3x3 rot = glm::transpose(convertMatrix4To3(rotationNode->mTransformation));
      glm::mat3x3 rotation = glm::mat3x3(rot[0], rot[1], rot[2]);
      dstLight.Orientation = glm::normalize((glm::quat(rotation)));
      dstLight.Direction = glm::normalize(glm::vec3(rotation[2].x, rotation[2].y, rotation[2].z));
    }
    else if (srcLight.mType == aiLightSource_POINT)
    {
      PointLight& dstLight = pointLights[numPointLights++];
      dstLight.Position = convertVector(srcLight.mPosition);
      dstLight.Intensity = convertColor(srcLight.mColorDiffuse);
    }
  }

  spotLights.resize(numSpotLights);
  pointLights.resize(numPointLights);

  // Post-process the scene
  uint32_t flags = aiProcess_CalcTangentSpace | aiProcess_Triangulate |
                   aiProcess_JoinIdenticalVertices | aiProcess_MakeLeftHanded |
                   aiProcess_RemoveRedundantMaterials | aiProcess_FlipUVs |
                   aiProcess_FlipWindingOrder;

  if (settings.MergeMeshes)
    flags |= aiProcess_PreTransformVertices | aiProcess_OptimizeMeshes;

  scene = importer.ApplyPostProcessing(flags);

  // Load the materials
  const uint64_t numMaterials = scene->mNumMaterials;
  meshMaterials.resize(numMaterials);
  for (uint64_t i = 0; i < numMaterials; ++i)
  {
    MeshMaterial& material = meshMaterials[i];
    const aiMaterial& mat = *scene->mMaterials[i];

    aiString diffuseTexPath;
    aiString normalMapPath;
    aiString roughnessMapPath;
    aiString metallicMapPath;
    if (mat.GetTexture(aiTextureType_DIFFUSE, 0, &diffuseTexPath) == aiReturn_SUCCESS)
      material.TextureNames[uint64_t(MaterialTextures::Albedo)] =
          getFileName(strToWideStr(diffuseTexPath.C_Str()).c_str());

    if (mat.GetTexture(aiTextureType_NORMALS, 0, &normalMapPath) == aiReturn_SUCCESS ||
        mat.GetTexture(aiTextureType_HEIGHT, 0, &normalMapPath) == aiReturn_SUCCESS)
      material.TextureNames[uint64_t(MaterialTextures::Normal)] =
          getFileName(strToWideStr(normalMapPath.C_Str()).c_str());

    /*if(mat.GetTexture(aiTextureType_SHININESS, 0, &roughnessMapPath) ==
       aiReturn_SUCCESS)
        material.TextureNames[uint64_t(MaterialTextures::Roughness)] =
       GetFileName(AnsiToWString(roughnessMapPath.C_Str()).c_str());*/

    // For some reason the roughness maps aren't coming through in the SHININESS
    // channel after Assimp import. :(
    if (SponzaRoughnessMaps[i] != nullptr)
      material.TextureNames[uint64_t(MaterialTextures::Roughness)] =
          getFileName(SponzaRoughnessMaps[i]);

    if (mat.GetTexture(aiTextureType_AMBIENT, 0, &metallicMapPath) == aiReturn_SUCCESS)
      material.TextureNames[uint64_t(MaterialTextures::Metallic)] =
          getFileName(strToWideStr(metallicMapPath.C_Str()).c_str());
  }

  loadMaterialResources(dev, meshMaterials, fileDirectory, settings.ForceSRGB, materialTextures);

  aabbMin = glm::vec3(maxFloat);
  aabbMax = glm::vec3(-maxFloat);

  // Initialize the meshes
  const uint64_t numMeshes = scene->mNumMeshes;
  uint64_t numVertices = 0;
  uint64_t numIndices = 0;
  for (uint64_t i = 0; i < numMeshes; ++i)
  {
    const aiMesh& assimpMesh = *scene->mMeshes[i];

    numVertices += assimpMesh.mNumVertices;
    numIndices += assimpMesh.mNumFaces * 3;
  }

  vertices.resize(numVertices);
  indices.resize(numIndices);

  meshes.resize(numMeshes);
  uint64_t vtxOffset = 0;
  uint64_t idxOffset = 0;
  for (uint64_t i = 0; i < numMeshes; ++i)
  {
    meshes[i].InitFromAssimpMesh(
        *scene->mMeshes[i], settings.SceneScale, &vertices[vtxOffset], &indices[idxOffset]);

    aabbMin.x = std::min(aabbMin.x, meshes[i].AABBMin().x);
    aabbMin.y = std::min(aabbMin.y, meshes[i].AABBMin().y);
    aabbMin.z = std::min(aabbMin.z, meshes[i].AABBMin().z);

    aabbMax.x = std::max(aabbMax.x, meshes[i].AABBMax().x);
    aabbMax.y = std::max(aabbMax.y, meshes[i].AABBMax().y);
    aabbMax.z = std::max(aabbMax.z, meshes[i].AABBMax().z);

    vtxOffset += meshes[i].NumVertices();
    idxOffset += meshes[i].NumIndices();
  }

  CreateBuffers();

  writeLog("Finished loading scene '%ls'", filePath);
}

void Model::CreateFromMeshData(ID3D12Device* dev, const wchar_t* filePath)
{
  if (fileExists(filePath) == false)
    throw std::exception("Model file does not exist");

  fileDirectory = getDirectoryFromFilePath(filePath);

  CreateBuffers();

  loadMaterialResources(dev, meshMaterials, fileDirectory, forceSRGB, materialTextures);
}

// Procedural generation
void Model::GenerateBoxScene(
    ID3D12Device* dev,
    const glm::vec3& dimensions,
    const glm::vec3& position,
    const glm::quat& orientation,
    const wchar_t* colorMap,
    const wchar_t* normalMap)
{
  meshMaterials.resize(1);
  MeshMaterial& material = meshMaterials[0];
  material.TextureNames[uint64_t(MaterialTextures::Albedo)] = colorMap;
  material.TextureNames[uint64_t(MaterialTextures::Normal)] = normalMap;
  fileDirectory = L"..\\Content\\Textures\\";
  loadMaterialResources(dev, meshMaterials, L"..\\Content\\Textures\\", false, materialTextures);

  vertices.resize(NumBoxVerts);
  indices.resize(NumBoxIndices);

  meshes.resize(1);
  meshes[0].InitBox(dimensions, position, orientation, 0, vertices.data(), indices.data());

  CreateBuffers();
}
void Model::GenerateBoxTestScene(ID3D12Device* dev)
{
  meshMaterials.resize(1);
  MeshMaterial& material = meshMaterials[0];
  material.TextureNames[uint64_t(MaterialTextures::Albedo)] = L"White.png";
  material.TextureNames[uint64_t(MaterialTextures::Normal)] = L"Hex.png";
  fileDirectory = L"..\\Content\\Textures\\";
  loadMaterialResources(dev, meshMaterials, L"..\\Content\\Textures\\", false, materialTextures);

  vertices.resize(NumBoxVerts * 2);
  indices.resize(NumBoxIndices * 2);

  meshes.resize(2);
  meshes[0].InitBox(
      glm::vec3(2.0f),
      glm::vec3(0.0f, 1.5f, 0.0f),
      glm::quat(0, 0, 0, 1),
      0,
      vertices.data(),
      indices.data());
  meshes[1].InitBox(
      glm::vec3(10.0f, 0.25f, 10.0f),
      glm::vec3(0.0f),
      glm::quat(0, 0, 0, 1),
      0,
      &vertices[NumBoxVerts],
      &indices[NumBoxIndices]);

  CreateBuffers();
}
void Model::GeneratePlaneScene(
    ID3D12Device* dev,
    const glm::vec2& dimensions,
    const glm::vec3& position,
    const glm::quat& orientation,
    const wchar_t* colorMap,
    const wchar_t* normalMap)
{
  meshMaterials.resize(1);
  MeshMaterial& material = meshMaterials[0];
  material.TextureNames[uint64_t(MaterialTextures::Albedo)] = colorMap;
  material.TextureNames[uint64_t(MaterialTextures::Normal)] = normalMap;
  fileDirectory = L"..\\Content\\Textures\\";
  loadMaterialResources(dev, meshMaterials, L"..\\Content\\Textures\\", false, materialTextures);

  vertices.resize(NumPlaneVerts);
  indices.resize(NumPlaneIndices);

  meshes.resize(1);
  meshes[0].InitPlane(dimensions, position, orientation, 0, vertices.data(), indices.data());

  CreateBuffers();
}

void Model::Shutdown()
{
  for (uint64_t i = 0; i < meshes.size(); ++i)
    meshes[i].Shutdown();
  meshes.clear();
  meshMaterials.clear();
  for (uint64_t i = 0; i < materialTextures.size(); ++i)
  {
    materialTextures[i]->Texture.Shutdown();
    delete materialTextures[i];
    materialTextures[i] = nullptr;
  }
  materialTextures.clear();
  fileDirectory = L"";
  forceSRGB = false;

  vertexBuffer.deinit();
  indexBuffer.deinit();
  vertices.clear();
  indices.clear();
}

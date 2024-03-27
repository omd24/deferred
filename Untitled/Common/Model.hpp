#pragma once

#pragma once

#include "D3D12Wrapper.hpp"

#include <assert.h>
#include <string>
#include <vector>

struct aiMesh;

struct MeshVertex
{
  glm::vec3 Position;
  glm::vec3 Normal;
  glm::vec2 UV;
  glm::vec3 Tangent;
  glm::vec3 Bitangent;

  MeshVertex() {}

  MeshVertex(
      const glm::vec3& p,
      const glm::vec3& n,
      const glm::vec2& uv,
      const glm::vec3& t,
      const glm::vec3& b)
  {
    Position = p;
    Normal = n;
    UV = uv;
    Tangent = t;
    Bitangent = b;
  }

  void Transform(const glm::vec3& p, const glm::vec3& s, const glm::quat& q)
  {
    Position *= s;
    Position = glm::rotate(q, Position);
    Position += p;

    Normal = glm::rotate(q, Normal);
    Tangent = glm::rotate(q, Tangent);
    Bitangent = glm::rotate(q, Bitangent);
  }
};

enum class MaterialTextures
{
  Albedo = 0,
  Normal,
  Roughness,
  Metallic,

  Count
};

struct MeshMaterial
{
  std::wstring TextureNames[uint64_t(MaterialTextures::Count)];
  const Texture* Textures[uint64_t(MaterialTextures::Count)] = {};
  uint32_t TextureIndices[uint64_t(MaterialTextures::Count)] = {};

  uint32_t Texture(MaterialTextures texType) const
  {
    assert(uint64_t(texType) < uint64_t(MaterialTextures::Count));
    assert(Textures[uint64_t(texType)] != nullptr);
    return Textures[uint64_t(texType)]->SRV;
  }
};

struct MeshPart
{
  uint32_t VertexStart;
  uint32_t VertexCount;
  uint32_t IndexStart;
  uint32_t IndexCount;
  uint32_t MaterialIdx;

  MeshPart() : VertexStart(0), VertexCount(0), IndexStart(0), IndexCount(0), MaterialIdx(0) {}
};

enum class IndexType
{
  Index16Bit = 0,
  Index32Bit = 1
};

enum class InputElementType : uint64_t
{
  Position = 0,
  Normal,
  Tangent,
  Bitangent,
  UV,

  NumTypes,
};

static const InputElementType StandardInputElementTypes[5] = {
    InputElementType::Position,
    InputElementType::Normal,
    InputElementType::UV,
    InputElementType::Tangent,
    InputElementType::Bitangent,
};

static const D3D12_INPUT_ELEMENT_DESC StandardInputElements[5] = {
    {"POSITION",
     0,
     DXGI_FORMAT_R32G32B32_FLOAT,
     0,
     0,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     0},
    {"NORMAL",
     0,
     DXGI_FORMAT_R32G32B32_FLOAT,
     0,
     12,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     0},
    {"UV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TANGENT",
     0,
     DXGI_FORMAT_R32G32B32_FLOAT,
     0,
     32,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     0},
    {"BITANGENT",
     0,
     DXGI_FORMAT_R32G32B32_FLOAT,
     0,
     44,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     0},
};

struct MaterialTexture
{
  std::wstring Name;
  Texture Texture;
};

struct ModelSpotLight
{
  glm::vec3 Position;
  glm::vec3 Intensity;
  glm::vec3 Direction;
  glm::quat Orientation;
  glm::vec2 AngularAttenuation;
};

struct PointLight
{
  glm::vec3 Position;
  glm::vec3 Intensity;
};

class Mesh
{
  friend class Model;

public:
  ~Mesh() { assert(numVertices == 0); }

  // Init from loaded files
  void InitFromAssimpMesh(
      const aiMesh& assimpMesh, float sceneScale, MeshVertex* dstVertices, uint16_t* dstIndices);

  // Procedural generation
  void InitBox(
      const glm::vec3& dimensions,
      const glm::vec3& position,
      const glm::quat& orientation,
      uint32_t materialIdx,
      MeshVertex* dstVertices,
      uint16_t* dstIndices);

  void InitPlane(
      const glm::vec2& dimensions,
      const glm::vec3& position,
      const glm::quat& orientation,
      uint32_t materialIdx,
      MeshVertex* dstVertices,
      uint16_t* dstIndices);

  void InitCommon(
      const MeshVertex* vertices,
      const uint16_t* indices,
      uint64_t vbAddress,
      uint64_t ibAddress,
      uint64_t vtxOffset,
      uint64_t idxOffset);

  void Shutdown();

  // Accessors
  const std::vector<MeshPart>& MeshParts() const { return meshParts; }
  uint64_t NumMeshParts() const { return meshParts.size(); }

  uint32_t NumVertices() const { return numVertices; }
  uint32_t NumIndices() const { return numIndices; }
  uint32_t VertexOffset() const { return vtxOffset; }
  uint32_t IndexOffset() const { return idxOffset; }

  IndexType IndexBufferType() const { return IndexType::Index16Bit; }
  DXGI_FORMAT IndexBufferFormat() const { return DXGI_FORMAT_R16_UINT; }
  uint32_t IndexSize() const { return 2; }

  const MeshVertex* Vertices() const { return vertices; }
  const uint16_t* Indices() const { return indices; }

  const D3D12_VERTEX_BUFFER_VIEW* VBView() const { return &vbView; }
  const D3D12_INDEX_BUFFER_VIEW* IBView() const { return &ibView; }

  const glm::vec3& AABBMin() const { return aabbMin; }
  const glm::vec3& AABBMax() const { return aabbMax; }

  static const char* InputElementTypeString(InputElementType elemType)
  {
    static const char* ElemStrings[] = {
        "POSITION",
        "NORMAL",
        "TANGENT",
        "BITANGENT",
        "UV",
    };

    static_assert(arrayCount32(ElemStrings) == uint32_t(InputElementType::NumTypes));
    assert(uint64_t(elemType) < uint64_t(InputElementType::NumTypes));

    return ElemStrings[uint64_t(elemType)];
  }

protected:
  std::vector<MeshPart> meshParts;

  uint32_t numVertices = 0;
  uint32_t numIndices = 0;
  uint32_t vtxOffset = 0;
  uint32_t idxOffset = 0;

  IndexType indexType = IndexType::Index16Bit;

  const MeshVertex* vertices = nullptr;
  const uint16_t* indices = nullptr;

  D3D12_VERTEX_BUFFER_VIEW vbView = {};
  D3D12_INDEX_BUFFER_VIEW ibView = {};

  glm::vec3 aabbMin;
  glm::vec3 aabbMax;
};

struct ModelLoadSettings
{
  const wchar_t* FilePath = nullptr;
  float SceneScale = 1.0f;
  bool ForceSRGB = false;
  bool MergeMeshes = true;
};

class Model
{
public:
  ~Model() { assert(meshes.size() == 0); }

  // Loading from file formats
  void CreateWithAssimp(ID3D12Device* dev, const ModelLoadSettings& settings);

  void CreateFromMeshData(ID3D12Device* dev, const wchar_t* filePath);

  // Procedural generation
  void GenerateBoxScene(
      ID3D12Device* dev,
      const glm::vec3& dimensions = glm::vec3(1.0f, 1.0f, 1.0f),
      const glm::vec3& position = glm::vec3(),
      const glm::quat& orientation = glm::identity<glm::quat>(),
      const wchar_t* colorMap = L"",
      const wchar_t* normalMap = L"");
  void GenerateBoxTestScene(ID3D12Device* dev);
  void GeneratePlaneScene(
      ID3D12Device* dev,
      const glm::vec2& dimensions = glm::vec2(1.0f, 1.0f),
      const glm::vec3& position = glm::vec3(),
      const glm::quat& orientation = glm::quat(0.0, 0.0, 0.0, 1.0),
      const wchar_t* colorMap = L"",
      const wchar_t* normalMap = L"");

  void Shutdown();

  // Accessors
  const std::vector<Mesh>& Meshes() const { return meshes; }
  uint64_t NumMeshes() const { return meshes.size(); }

  const glm::vec3& AABBMin() const { return aabbMin; }
  const glm::vec3& AABBMax() const { return aabbMax; }

  const std::vector<MeshMaterial>& Materials() const { return meshMaterials; }
  const std::vector<MaterialTexture*>& MaterialTextures() const { return materialTextures; }

  const std::vector<ModelSpotLight>& SpotLights() const { return spotLights; }
  const std::vector<PointLight>& PointLights() const { return pointLights; }

  const StructuredBuffer& VertexBuffer() const { return vertexBuffer; }
  const FormattedBuffer& IndexBuffer() const { return indexBuffer; }

  const std::wstring& FileDirectory() const { return fileDirectory; }

  static const D3D12_INPUT_ELEMENT_DESC* InputElements() { return StandardInputElements; }
  static const InputElementType* InputElementTypes() { return StandardInputElementTypes; }
  static uint64_t NumInputElements()
  {
    return static_cast<uint64_t>(arrayCount32(StandardInputElements));
  }

protected:
  void CreateBuffers()
  {
    assert(meshes.size() > 0);

    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(MeshVertex);
    sbInit.NumElements = vertices.size();

    sbInit.InitData = vertices.data();
    vertexBuffer.init(sbInit);

    FormattedBufferInit fbInit;
    fbInit.Format = DXGI_FORMAT_R16_UINT;
    fbInit.NumElements = indices.size();
    fbInit.InitData = indices.data();
    indexBuffer.init(fbInit);

    uint64_t vtxOffset = 0;
    uint64_t idxOffset = 0;
    const uint64_t numMeshes = meshes.size();
    for (uint64_t i = 0; i < numMeshes; ++i)
    {
      uint64_t vbOffset = vtxOffset * sizeof(MeshVertex);
      uint64_t ibOffset = idxOffset * sizeof(uint16_t);
      meshes[i].InitCommon(
          &vertices[vtxOffset],
          &indices[idxOffset],
          vertexBuffer.m_GpuAddress + vbOffset,
          indexBuffer.GPUAddress + ibOffset,
          vtxOffset,
          idxOffset);

      vtxOffset += meshes[i].NumVertices();
      idxOffset += meshes[i].NumIndices();
    }
  }

  std::vector<Mesh> meshes;
  std::vector<MeshMaterial> meshMaterials;
  std::vector<ModelSpotLight> spotLights;
  std::vector<PointLight> pointLights;
  std::wstring fileDirectory;
  bool forceSRGB = false;
  glm::vec3 aabbMin;
  glm::vec3 aabbMax;

  StructuredBuffer vertexBuffer;
  FormattedBuffer indexBuffer;
  std::vector<MeshVertex> vertices;
  std::vector<uint16_t> indices;

  std::vector<MaterialTexture*> materialTextures;
};

void loadTexture(
    ID3D12Device* dev, Texture& texture, const wchar_t* filePath, bool forceSRGB = false);

void makeConeGeometry(
    uint64_t divisions,
    StructuredBuffer& vtxBuffer,
    FormattedBuffer& idxBuffer,
    std::vector<glm::vec3>& positions);
void makeConeGeometry(uint64_t divisions, StructuredBuffer& vtxBuffer, FormattedBuffer& idxBuffer);

// Some additional texture helpers:
template <typename T> struct TextureData
{
  std::vector<T> Texels;
  uint32 Width = 0;
  uint32 Height = 0;
  uint32 NumSlices = 0;

  void init(uint32 width, uint32 height, uint32 numSlices)
  {
    Width = width;
    Height = height;
    NumSlices = numSlices;
    Texels.resize(width * height * numSlices);
  }
};
// Decode a texture and copies it to the CPU
void getTextureData(const Texture& texture, TextureData<glm::vec4>& textureData);
glm::vec3 mapXYSToDirection(uint64_t x, uint64_t y, uint64_t s, uint64_t width, uint64_t height);

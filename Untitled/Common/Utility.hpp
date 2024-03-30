#pragma once

/******************************************************************************
 * \common utilities for a demo
 * \
 ******************************************************************************/

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

//---------------------------------------------------------------------------//
// Disable warnings:
//---------------------------------------------------------------------------//
//
#pragma warning(disable : 28182) // pointer might be null
#pragma warning(disable : 6387)  // pointer might be 0
#pragma warning(disable : 6001)  // uninitialized memory
#pragma warning(disable : 26439) // noexcept function
#pragma warning(disable : 26812) // unscoped enum
#pragma warning(disable : 26495) // not initializing struct members

//---------------------------------------------------------------------------//
// Includes:
//---------------------------------------------------------------------------//
//
//#ifndef NOMINMAX
//#define NOMINMAX
//#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <comdef.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include "DXCompiler/dxcapi.use.h"
#include <wrl.h>
#include <locale>
#include <codecvt>
#include <shellapi.h>
#include <vector>
#include <array>
#include <deque>
#include <thread>
#include <mutex>

#ifndef GLM_FORCE_RADIANS
#  define GLM_FORCE_RADIANS
#endif

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

//---------------------------------------------------------------------------//
// Smart COM ptr definitions:
//---------------------------------------------------------------------------//
//
#define MAKE_SMART_COM_PTR(_a) _COM_SMARTPTR_TYPEDEF(_a, __uuidof(_a))
MAKE_SMART_COM_PTR(ID3D12Device5);
MAKE_SMART_COM_PTR(ID3D12Device);
MAKE_SMART_COM_PTR(ID3D12GraphicsCommandList4);
MAKE_SMART_COM_PTR(ID3D12GraphicsCommandList);
MAKE_SMART_COM_PTR(ID3D12CommandQueue);
MAKE_SMART_COM_PTR(IDXGISwapChain1);
MAKE_SMART_COM_PTR(IDXGISwapChain3);
MAKE_SMART_COM_PTR(IDXGISwapChain4);
MAKE_SMART_COM_PTR(IDXGIFactory4);
MAKE_SMART_COM_PTR(IDXGIFactory6);
MAKE_SMART_COM_PTR(IDXGIAdapter1);
MAKE_SMART_COM_PTR(IDXGIAdapter);
MAKE_SMART_COM_PTR(ID3D12Fence);
MAKE_SMART_COM_PTR(ID3D12CommandAllocator);
MAKE_SMART_COM_PTR(ID3D12Resource);
MAKE_SMART_COM_PTR(ID3D12DescriptorHeap);
MAKE_SMART_COM_PTR(ID3D12Debug);
MAKE_SMART_COM_PTR(ID3D12StateObject);
MAKE_SMART_COM_PTR(ID3D12PipelineState);
MAKE_SMART_COM_PTR(ID3D12RootSignature);
MAKE_SMART_COM_PTR(ID3DBlob);
MAKE_SMART_COM_PTR(IDxcBlobEncoding);

//---------------------------------------------------------------------------//
// Common alias-declaration:
//---------------------------------------------------------------------------//
//
using I32 = int32_t;
using I64 = int64_t;
using U32 = uint32_t;
using U64 = uint64_t;

// Constants
const float Pi = 3.141592654f;
const float Pi2 = 6.283185307f;
const float Pi_2 = 1.570796327f;
const float Pi_4 = 0.7853981635f;
const float InvPi = 0.318309886f;
const float InvPi2 = 0.159154943f;

// Max value that we can store in an fp16 buffer (actually a little less so that we have room for
// error, real max is 65504)
const float FP16Max = 65000.0f;

// Scale factor used for storing physical light units in fp16 floats (equal to 2^-10).
// https://www.reedbeta.com/blog/artist-friendly-hdr-with-exposure-values/#fitting-into-half-float
// We basically shift input light values by -10EV (i.e., 2^-10) here and reapply the scale factor
// during exposure/tone-mapping to get back the real values.
const float FP16Scale = 0.0009765625f;

//---------------------------------------------------------------------------//
// Helper macros:
//---------------------------------------------------------------------------//
//
//---------------------------------------------------------------------------//
#define WIN32_MSG_BOX(x)                         \
  {                                              \
    MessageBoxA(g_WinHandle, x, "Error", MB_OK); \
  }

#define D3D_SAFE_RELEASE(p)  \
  {                          \
    if (p.GetInterfacePtr()) \
      (p).Release();         \
  }

#define D3D_ASSERT_HRESULT(x)                       \
  {                                                 \
    assert(SUCCEEDED(x) && "HRESULT != SUCCEEDED"); \
  }

#define D3D_EXEC_CHECKED(x) \
  {                         \
    HRESULT hr_ = x;        \
    if (FAILED(hr_))        \
    {                       \
      traceHr(#x, hr_);     \
      DEBUG_BREAK(0);       \
    }                       \
  }

#define DEBUG_BREAK(expr) \
  if (!(expr))            \
  {                       \
    __debugbreak();       \
  }

// TODO: use unique name generator or redesign!
#define DEFER(name_) auto name_ = DeferHelper{} + [&]()

// Naming helpers for COM ptrs:
// The indexed variant will include the index in the name of the object.
#define D3D_NAME_OBJECT(x) setName((x).GetInterfacePtr(), L#x)
#define D3D_NAME_OBJECT_INDEXED(x, n) setNameIndexed((x)[n].GetInterfacePtr(), L#x, n)

// For aligning to float4 boundaries
#define Float4Align __declspec(align(16))

//---------------------------------------------------------------------------//
// Global variables
//---------------------------------------------------------------------------//
struct DemoInfo;
struct CallBackRegistery;

inline DemoInfo* g_DemoInfo = nullptr;
inline HWND g_WinHandle = nullptr;
// inline CallBackRegistery* g_CallbackReg = nullptr;

//---------------------------------------------------------------------------//
// Helper functions:
//---------------------------------------------------------------------------//
// Aligns p_Val to the next multiple of p_Alignment
template <typename T> inline T alignUp(T p_Val, T p_Alignment)
{
  return (p_Val + p_Alignment - (T)1) & ~(p_Alignment - (T)1);
}
//---------------------------------------------------------------------------//
// Aligns p_Val to the previous multiple of p_Alignment
template <typename T> inline T alignDown(T p_Val, T p_Alignment)
{
  return p_Val & ~(p_Alignment - (T)1);
}
//---------------------------------------------------------------------------//
inline UINT calculateConstantBufferByteSize(UINT p_ByteSize)
{
  // Constant buffer size is required to be aligned:
  return alignUp<UINT>(p_ByteSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
}
//---------------------------------------------------------------------------//
template <typename T> inline T divideRoundingUp(T p_Value1, T p_Value2)
{
  return (p_Value1 + p_Value2 - (T)1) / p_Value2;
}
//---------------------------------------------------------------------------//
template <typename T> inline T lerp(const T& p_Begin, const T& p_End, float p_InterpolationValue)
{
  return (T)(p_Begin * (1 - p_InterpolationValue) + p_End * p_InterpolationValue);
}
//---------------------------------------------------------------------------//
template <typename T> inline T _clamp(T p_Value, T p_Min, T p_Max)
{
  assert(p_Max > p_Min);

  if (p_Value < p_Min)
  {
    return p_Min;
  }
  else if (p_Value > p_Max)
  {
    return p_Max;
  }
  return p_Value;
}
//---------------------------------------------------------------------------//
inline glm::vec3 _clamp(glm::vec3 p_Value, glm::vec3 p_Min, glm::vec3 p_Max)
{
  assert(p_Max.x > p_Min.x && p_Max.y > p_Min.y && p_Max.z > p_Min.z);

  glm::vec3 ret = p_Value;
  for (unsigned i = 0; i < 3; ++i)
  {
    if (p_Value[i] < p_Min[i])
    {
      ret[i] = p_Min[i];
    }
    else if (p_Value[i] > p_Max[i])
    {
      ret[i] = p_Max[i];
    }
  }
  return ret;
}
// Clamps a value to [0, 1]
inline glm::vec3 saturate(glm::vec3 p_Value)
{
  return _clamp(p_Value, glm::vec3(0.0f), glm::vec3(1.0f));
}

// Clamps a value to [0, 1]
template <typename T> T saturate(T p_Value) { return _clamp<T>(p_Value, T(0.0f), T(1.0f)); }
//---------------------------------------------------------------------------//
template <typename T> void swap(T& a, T& b)
{
  T tmp = a;
  a = b;
  b = tmp;
}
//---------------------------------------------------------------------------//
// Returns x * x
template <typename T> T square(T x) { return x * x; }
//---------------------------------------------------------------------------//
inline float degToRad(float deg) { return deg * (1.0f / 180.0f) * 3.14159265359f; }
//---------------------------------------------------------------------------//
inline float radToDeg(float rad) { return rad * (1.0f / 3.14159265359f) * 180.0f; }
// 
// Converts a string to a wide-string
inline std::wstring strToWideStr(const std::string& p_Str)
{
  std::wstring_convert<std::codecvt_utf8<WCHAR>> cvt;
  std::wstring wStr = cvt.from_bytes(p_Str);
  return wStr;
}
//---------------------------------------------------------------------------//
// Converts a wide-string to a string
inline std::string WideStrToStr(const std::wstring& p_WideStr)
{
  std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
  std::string str = cvt.to_bytes(p_WideStr);
  return str;
}
//---------------------------------------------------------------------------//
inline std::wstring MakeString(const wchar_t* format, ...)
{
  wchar_t buffer[1024] = {0};
  va_list args;
  va_start(args, format);
  vswprintf_s(buffer, 1024, format, args);
  return std::wstring(buffer);
}
//---------------------------------------------------------------------------//
inline std::string MakeString(const char* format, ...)
{
  char buffer[1024] = {0};
  va_list args;
  va_start(args, format);
  vsprintf_s(buffer, 1024, format, args);
  return std::string(buffer);
}
//---------------------------------------------------------------------------//
// Alternative methods for string conversion
template <typename CharSource, typename CharDest>
inline size_t StringConvert(const CharSource* p_Src, CharDest* p_Dst, int p_DstSize);
template <> inline size_t StringConvert(const wchar_t* p_Src, char* p_Dst, int p_DstSize)
{
  size_t converted = 0;
  wcstombs_s(&converted, p_Dst, p_DstSize, p_Src, p_DstSize);
  return converted;
}
template <> inline size_t StringConvert(const char* p_Src, wchar_t* p_Dst, int p_DstSize)
{
  size_t converted = 0;
  mbstowcs_s(&converted, p_Dst, p_DstSize, p_Src, p_DstSize);
  return converted;
}
//---------------------------------------------------------------------------//
// Converts a blob to a string
template <typename BlobType> std::string convertBlobToString(BlobType* p_Blob)
{
  std::vector<char> infoLog(p_Blob->GetBufferSize() + 1);
  memcpy(infoLog.data(), p_Blob->GetBufferPointer(), p_Blob->GetBufferSize());
  infoLog[p_Blob->GetBufferSize()] = 0;
  return std::string(infoLog.data());
}
//---------------------------------------------------------------------------//
// Counts the elements of an array:
template <typename T, size_t N> constexpr size_t arrayCount(T (&)[N]) { return N; }
//---------------------------------------------------------------------------//
template <typename T, U32 N> constexpr U32 arrayCount32(T (&)[N]) { return N; }
//---------------------------------------------------------------------------//
template <typename T, U32 N> constexpr void setArrayToZero(T (&p_Array)[N])
{
  ::memset(p_Array, 0, N * sizeof(p_Array[0]));
}
//---------------------------------------------------------------------------//
// Traces an error and convert the msg to a human-readable string
inline void traceHr(const std::string& p_Msg, HRESULT p_Hr)
{
  char hrMsg[512];
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, p_Hr, 0, hrMsg, arrayCount32(hrMsg), nullptr);
  std::string errMsg = p_Msg + ".\nError! " + hrMsg;
  WIN32_MSG_BOX(errMsg.c_str());
}
//---------------------------------------------------------------------------//
inline void getAssetsPath(_Out_writes_(p_PathSize) WCHAR* p_Path, UINT p_PathSize)
{
  DEBUG_BREAK(p_Path);

  DWORD size = GetModuleFileName(nullptr, p_Path, p_PathSize);
  DEBUG_BREAK(0 != size || size != p_PathSize);

  WCHAR* lastSlash = wcsrchr(p_Path, L'\\');
  if (lastSlash)
  {
    *(lastSlash + 1) = L'\0';
  }
}
inline void getShadersPath(_Out_writes_(p_PathSize) WCHAR* p_Path, UINT p_PathSize)
{
  getAssetsPath(p_Path, p_PathSize);
  wcscat_s(p_Path, 512, L"Shaders\\");
}
//---------------------------------------------------------------------------//
inline HRESULT readDataFromFile(LPCWSTR p_Filename, byte** p_Data, UINT* p_Size)
{
  using namespace Microsoft::WRL;

#if WINVER >= _WIN32_WINNT_WIN8
  CREATEFILE2_EXTENDED_PARAMETERS extendedParams = {};
  extendedParams.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
  extendedParams.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
  extendedParams.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
  extendedParams.dwSecurityQosFlags = SECURITY_ANONYMOUS;
  extendedParams.lpSecurityAttributes = nullptr;
  extendedParams.hTemplateFile = nullptr;

  Wrappers::FileHandle file(
      CreateFile2(p_Filename, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &extendedParams));
#else
  Wrappers::FileHandle file(CreateFile(
      filename,
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | SECURITY_SQOS_PRESENT |
          SECURITY_ANONYMOUS,
      nullptr));
#endif
  if (file.Get() == INVALID_HANDLE_VALUE)
  {
    throw std::exception();
  }

  FILE_STANDARD_INFO fileInfo = {};
  DEBUG_BREAK(
      !GetFileInformationByHandleEx(file.Get(), FileStandardInfo, &fileInfo, sizeof(fileInfo)));

  DEBUG_BREAK(fileInfo.EndOfFile.HighPart != 0);

  *p_Data = reinterpret_cast<byte*>(malloc(fileInfo.EndOfFile.LowPart));
  *p_Size = fileInfo.EndOfFile.LowPart;

  DEBUG_BREAK(!ReadFile(file.Get(), *p_Data, fileInfo.EndOfFile.LowPart, nullptr, nullptr));

  return S_OK;
}
//---------------------------------------------------------------------------//
inline HRESULT readDataFromDDSFile(LPCWSTR p_Filename, byte** p_Data, UINT* p_Offset, UINT* p_Size)
{
  D3D_ASSERT_HRESULT(readDataFromFile(p_Filename, p_Data, p_Size));

  // DDS files always start with the same magic number.
  static const UINT DDS_MAGIC = 0x20534444;
  UINT magicNumber = *reinterpret_cast<const UINT*>(*p_Data);
  if (magicNumber != DDS_MAGIC)
  {
    return E_FAIL;
  }

  struct DDS_PIXELFORMAT
  {
    UINT size;
    UINT flags;
    UINT fourCC;
    UINT rgbBitCount;
    UINT rBitMask;
    UINT gBitMask;
    UINT bBitMask;
    UINT aBitMask;
  };

  struct DDS_HEADER
  {
    UINT size;
    UINT flags;
    UINT height;
    UINT width;
    UINT pitchOrLinearSize;
    UINT depth;
    UINT mipMapCount;
    UINT reserved1[11];
    DDS_PIXELFORMAT ddsPixelFormat;
    UINT caps;
    UINT caps2;
    UINT caps3;
    UINT caps4;
    UINT reserved2;
  };

  auto ddsHeader = reinterpret_cast<const DDS_HEADER*>(*p_Data + sizeof(UINT));
  if (ddsHeader->size != sizeof(DDS_HEADER) ||
      ddsHeader->ddsPixelFormat.size != sizeof(DDS_PIXELFORMAT))
  {
    return E_FAIL;
  }

  const ptrdiff_t ddsDataOffset = sizeof(UINT) + sizeof(DDS_HEADER);
  *p_Offset = ddsDataOffset;
  *p_Size = *p_Size - ddsDataOffset;

  return S_OK;
}
//---------------------------------------------------------------------------//
// Assigns a name to the object to aid with debugging.
#if defined(_DEBUG) || defined(DBG)
inline void setName(ID3D12Object* p_Object, LPCWSTR p_Name) { p_Object->SetName(p_Name); }
inline void setNameIndexed(ID3D12Object* pObject, LPCWSTR name, UINT index)
{
  WCHAR fullName[50];
  if (swprintf_s(fullName, L"%s[%u]", name, index) > 0)
  {
    pObject->SetName(fullName);
  }
}
#else
inline void setName(ID3D12Object*, LPCWSTR) {}
inline void setNameIndexed(ID3D12Object*, LPCWSTR, UINT) {}
#endif
//---------------------------------------------------------------------------//
enum class ShaderType : uint8_t
{
  Vertex = 0,
  Pixel,
  Compute,

  NumTypes
};
//---------------------------------------------------------------------------//
static const char* ShaderTypeStringsFXC[] = {

    "vs_5_1",
    "ps_5_1",
    "cs_5_1",
};
//---------------------------------------------------------------------------//
#ifdef D3D_COMPILE_STANDARD_FILE_INCLUDE
inline ID3DBlobPtr compileShaderFXC_old(
    const std::wstring& p_Filename,
    const D3D_SHADER_MACRO* p_Defines,
    const std::string& p_Entrypoint,
    const std::string& p_Target)
{
  UINT compileFlags = 0;
#  if defined(_DEBUG) || defined(DBG)
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#  endif

  HRESULT hr;
  ID3DBlobPtr byteCode = nullptr;
  ID3DBlobPtr errors;
  hr = D3DCompileFromFile(
      p_Filename.c_str(),
      p_Defines,
      D3D_COMPILE_STANDARD_FILE_INCLUDE,
      p_Entrypoint.c_str(),
      p_Target.c_str(),
      compileFlags,
      0,
      &byteCode,
      &errors);

  if (errors != nullptr)
  {
    OutputDebugStringA((char*)errors->GetBufferPointer());
  }
  D3D_ASSERT_HRESULT(hr);

  return byteCode;
}
#endif
//---------------------------------------------------------------------------//
// Compile shaders using FXC (updated version of the above function)
bool compileShaderFXC (
  const char* p_DbgName,
  const wchar_t* p_ShaderPath,
  const D3D_SHADER_MACRO* p_Defines,
  unsigned int p_CompileFlags,
  ShaderType p_ShaderType,
  const char * p_EntryPoint,
  ID3DBlobPtr & p_OutShader);
//---------------------------------------------------------------------------//
// Compile shader wrapper (to switch between dxc and fxc)
bool compileShader(
    const char* p_DbgName,
    const wchar_t* p_ShaderPath,
    const D3D_SHADER_MACRO* p_Defines,
    unsigned int p_CompileFlags,
    ShaderType p_ShaderType,
    const char* p_EntryPoint,
    ID3DBlobPtr& p_OutShader);
//---------------------------------------------------------------------------//
// Resets all elements in a ComPtr array
template <typename T> inline void resetComPtrArray(T* p_ComPtrArray)
{
  for (auto& i : *p_ComPtrArray)
  {
    i = nullptr;
  }
}
//---------------------------------------------------------------------------//
// Resets all elements in a unique_ptr array
template <typename T> inline void resetUniquePtrArray(T* p_UniquePtrArray)
{
  for (auto& i : *p_UniquePtrArray)
  {
    i = nullptr;
  }
}
//---------------------------------------------------------------------------//
inline void getHardwareAdapter(
    _In_ IDXGIFactory1* p_Factory,
    _Outptr_result_maybenull_ IDXGIAdapter1** p_Adapter,
    bool p_RequestHighPerformanceAdapter = false)
{
  *p_Adapter = nullptr;

  IDXGIAdapter1Ptr adapter;

  IDXGIFactory6Ptr factory6;
  if (SUCCEEDED(p_Factory->QueryInterface(IID_PPV_ARGS(&factory6))))
  {
    for (UINT adapterIndex = 0; SUCCEEDED(factory6->EnumAdapterByGpuPreference(
             adapterIndex,
             p_RequestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                                                     : DXGI_GPU_PREFERENCE_UNSPECIFIED,
             IID_PPV_ARGS(&adapter)));
         ++adapterIndex)
    {
      DXGI_ADAPTER_DESC1 desc;
      adapter->GetDesc1(&desc);

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      {
        // Don't select the Basic Render Driver adapter.
        // If you want a software adapter, pass in "/warp" on the command line.
        continue;
      }

      // Check to see whether the adapter supports Direct3D 12, but don't create
      // the actual device yet.
      if (SUCCEEDED(D3D12CreateDevice(
              adapter.GetInterfacePtr(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
      {
        break;
      }
    }
  }

  if (adapter.GetInterfacePtr() == nullptr)
  {
    for (UINT adapterIndex = 0; SUCCEEDED(p_Factory->EnumAdapters1(adapterIndex, &adapter));
         ++adapterIndex)
    {
      DXGI_ADAPTER_DESC1 desc;
      adapter->GetDesc1(&desc);

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      {
        // Don't select the Basic Render Driver adapter.
        // If you want a software adapter, pass in "/warp" on the command line.
        continue;
      }

      // Check to see whether the adapter supports Direct3D 12, but don't create
      // the actual device yet.
      if (SUCCEEDED(D3D12CreateDevice(
              adapter.GetInterfacePtr(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
      {
        break;
      }
    }
  }

  *p_Adapter = adapter.Detach();
}

//---------------------------------------------------------------------------//
// Defer helpers:
//---------------------------------------------------------------------------//
template <typename F> struct Deferrer
{
  Deferrer(F&& p_Func) : m_Func(std::move(p_Func)) {}
  ~Deferrer() noexcept
  {
    if (!m_Canceled)
      m_Func();
  }
  void cancel() noexcept { m_Canceled = true; }

private:
  bool m_Canceled = false;
  F m_Func;
};
//---------------------------------------------------------------------------//
template <typename F> Deferrer<F> makeDeferrer(F&& p_Func)
{
  return Deferrer<F>(std::move(p_Func));
}
struct DeferHelper
{
  template <typename F> friend Deferrer<F> operator+(DeferHelper const&, F&& p_Func)
  {
    return makeDeferrer(std::move(p_Func));
  }
};
//---------------------------------------------------------------------------//
// Microsoft Exception Helper:
//---------------------------------------------------------------------------//
inline std::string HrToString(HRESULT hr)
{
  char s_str[64] = {};
  sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
  return std::string(s_str);
}
class HrException : public std::runtime_error
{
public:
  HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_hr(hr) {}
  HRESULT Error() const { return m_hr; }

private:
  const HRESULT m_hr;
};
//---------------------------------------------------------------------------//
inline void setWindowTitle(LPCWSTR p_Text, const std::wstring& p_Title)
{
  std::wstring windowText = p_Title + L": " + p_Text;
  SetWindowText(g_WinHandle, windowText.c_str());
}
//---------------------------------------------------------------------------//
typedef void (*MsgFunction)(void*, HWND, UINT, WPARAM, LPARAM);
inline MsgFunction g_ImguiCallback;
//---------------------------------------------------------------------------//
// TODO: move this to input system:
inline bool isMouseOverWindow(POINT p_MousePos)
{
  bool ret = false;
  if (g_WinHandle != NULL && GetForegroundWindow() == g_WinHandle)
  {
    RECT clientRect;
    GetClientRect(g_WinHandle, &clientRect);
    ret =
        (p_MousePos.x >= 0 && p_MousePos.x < clientRect.right && p_MousePos.y >= 0 &&
         p_MousePos.y < clientRect.bottom);
  }
  return ret;
}
//---------------------------------------------------------------------------//
/// zero or one random int
inline int randInt()
{
  int rndNumInt = std::rand() % 2;
  return rndNumInt;
}
//---------------------------------------------------------------------------//
/// random float between 0 and 1
inline float randFloat()
{
  float ret = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
  return ret;
}
//---------------------------------------------------------------------------//
// mimicking XMVector3TransofrmCoord
// i.e., setting w = 1 for the input and forcing the result to have w = 1
// https://learn.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmvector3transformcoord
inline glm::vec3 _transformVec3Mat4(const glm::vec3& v, const glm::mat4& m)
{
  glm::vec4 v4 = glm::vec4(v.x, v.y, v.z, 1.0f) * m;
  v4 /= v4.w;

  glm::vec3 ret = glm::vec3(v4.x, v4.y, v4.z);
  return ret;
}

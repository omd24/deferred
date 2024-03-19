#include "Utility.hpp"

bool compileShaderFXC(
    const char* p_DbgName,
    const wchar_t* p_ShaderPath,
    const D3D_SHADER_MACRO* p_Defines,
    unsigned int p_CompileFlags,
    ShaderType p_ShaderType,
    const char* p_EntryPoint,
    ID3DBlobPtr& p_OutShader)
{
  ID3DBlobPtr tempShader = nullptr;
  ID3DBlobPtr errorBlob = nullptr;

  uint8_t typeIdx = static_cast<uint8_t>(p_ShaderType);
  const char* typeString = ShaderTypeStringsFXC[typeIdx];

  bool ret = false;

  //while (ret != true)
  {
    HRESULT hr = D3DCompileFromFile(
        p_ShaderPath,
        p_Defines,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        p_EntryPoint,
        typeString,
        p_CompileFlags,
        0,
        &tempShader,
        &errorBlob);
    if (nullptr == tempShader || FAILED(hr))
    {
      char text[256]{};
      sprintf_s(
          text,
          "Failed to load %s shader, "
          "due to the following error messages:\n\n",
          p_DbgName);
      OutputDebugStringA(text);

      if (errorBlob != nullptr)
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());

      //::MessageBoxA(g_WinHandle, text, caption, MB_OK | MB_SETFOREGROUND);
      OutputDebugStringA("\n");
    }
    else
    {
      ret = true;
    }
  }

  if (ret)
  {
    p_OutShader = tempShader;
  }

  return ret;
}
#include "Utility.hpp"

#define USE_FXC 0

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

  // Amp/Mesh shaders not supported with fxc!
  assert(p_ShaderType != ShaderType::Mesh && p_ShaderType != ShaderType::Task);

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
//---------------------------------------------------------------------------//
static bool compileShaderDXC(
    const char* p_DbgName,
    const wchar_t* p_ShaderPath,
    const uint8_t p_NumDefines,
    const D3D_SHADER_MACRO* p_Defines,
    ShaderType p_ShaderType,
    const char* p_EntryPoint,
    ID3DBlobPtr& p_OutShader)
{
  uint8_t typeIdx = static_cast<uint8_t>(p_ShaderType);
  const char* profileString = ShaderTypeStringsDXC[typeIdx];

  IDxcLibrary* library = nullptr;
  D3D_EXEC_CHECKED(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library)));

  IDxcBlobEncoding* sourceCode = nullptr;
  D3D_EXEC_CHECKED(library->CreateBlobFromFile(p_ShaderPath, nullptr, &sourceCode));

  IDxcCompiler* compiler = nullptr;
  D3D_EXEC_CHECKED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));

  // Convert the defines to wide strings
  uint64_t numDefines = 0;
  while (p_Defines && p_Defines[numDefines].Name) // We always set the last define to NULL for this!
    ++numDefines;

  if (p_NumDefines > 0 && p_NumDefines != (numDefines + 1 /*last intentional NULL*/))
  {
    char msg[256]{};
    sprintf_s(msg, "[WARNING] Ignored invalid defines when compiling %s\n", p_DbgName);
    OutputDebugStringA(msg);
  }

  const uint64_t extraDefines = 2; // For two additional shader macros: USE_DXC and USE_SM68
  uint64_t totalNumDefines = numDefines + extraDefines;

  std::vector<DxcDefine> dxcDefines(totalNumDefines);
  std::vector<std::wstring> defineStrings(numDefines * 2);
  for (uint64_t i = 0; i < numDefines; ++i)
  {
    defineStrings[i * 2 + 0] = strToWideStr(p_Defines[i].Name);
    defineStrings[i * 2 + 1] = strToWideStr(p_Defines[i].Definition);
    dxcDefines[i].Name = defineStrings[i * 2 + 0].c_str();
    dxcDefines[i].Value = defineStrings[i * 2 + 1].c_str();
  }

  dxcDefines[numDefines + 0].Name = L"USE_DXC";
  dxcDefines[numDefines + 0].Value = L"1";
  dxcDefines[numDefines + 1].Name = L"USE_SM65";
  dxcDefines[numDefines + 1].Value = L"1";

  WCHAR assetsPath[512];
  getAssetsPath(assetsPath, _countof(assetsPath));
  const std::wstring shaderDirWStr = assetsPath;
  std::wstring shaderDir = shaderDirWStr + L"Shaders";
  wchar_t expandedShaderDir[1024] = {};
  GetFullPathName(
      shaderDir.c_str(),
      arrayCount32(expandedShaderDir),
      expandedShaderDir,
      nullptr);

  const wchar_t* arguments[] = {
    L"/O3",
    L"-all_resources_bound",
    L"-WX",
    L"-I",
    expandedShaderDir,

#if USE_SHADER_DEBUG_INFO
    L"/Zi",
    L"/Qembed_debug",
#endif
  };

  IDxcIncludeHandler* includeHandler = nullptr;
  D3D_EXEC_CHECKED(library->CreateIncludeHandler(&includeHandler));

  IDxcOperationResult* operationResult = nullptr;
  D3D_EXEC_CHECKED(compiler->Compile(
      sourceCode,
      p_ShaderPath,
      strToWideStr(p_EntryPoint).c_str(),
      strToWideStr(profileString).c_str(),
      arguments,
      arrayCount32(arguments),
      dxcDefines.data(),
      uint32_t(dxcDefines.size()),
      includeHandler,
      &operationResult));

  HRESULT hr = S_OK;
  operationResult->GetStatus(&hr);
  if (SUCCEEDED(hr))
    D3D_EXEC_CHECKED(operationResult->GetResult(reinterpret_cast<IDxcBlob**>(&p_OutShader)));

  ID3DBlobPtr errorMessages = nullptr;
  operationResult->GetErrorBuffer(reinterpret_cast<IDxcBlobEncoding**>(&errorMessages));

  operationResult->Release();
  includeHandler->Release();
  compiler->Release();
  sourceCode->Release();
  library->Release();

  // Process errors

  if (FAILED(hr))
  {
    if (errorMessages != nullptr)
    {
      wchar_t message[1024 * 4] = {0};
      char* blobdata = reinterpret_cast<char*>(errorMessages->GetBufferPointer());

      MultiByteToWideChar(
          CP_ACP, 0, blobdata, static_cast<int>(errorMessages->GetBufferSize()), message, 1024);
      std::wstring fullMessage = L"Error compiling shader file \"";
      fullMessage += p_ShaderPath;
      fullMessage += L"\" - ";
      fullMessage += message;

      // Pop up a message box allowing user to retry compilation
      int retVal =
          MessageBoxW(nullptr, fullMessage.c_str(), L"Shader Compilation Error", MB_RETRYCANCEL);
      if (retVal != IDRETRY)
        assert(false);
    }
    else
    {
      assert(false);
    }
  }
  else
  {
    char text[256]{};
    sprintf_s(
        text,
        "[INFO] Compiled %s shader.\n",
        p_DbgName);
    OutputDebugStringA(text);

    if (errorMessages != nullptr)
      OutputDebugStringA((char*)errorMessages->GetBufferPointer());

    //::MessageBoxA(g_WinHandle, text, caption, MB_OK | MB_SETFOREGROUND);
    OutputDebugStringA("\n");
  }

  return hr == S_OK;
}
//---------------------------------------------------------------------------//
bool compileShader(
    const char* p_DbgName,
    const wchar_t* p_ShaderPath,
    const uint8_t p_NumDefines,
    const D3D_SHADER_MACRO* p_Defines,
    unsigned int p_CompileFlags,
    ShaderType p_ShaderType,
    const char* p_EntryPoint,
    ID3DBlobPtr& p_OutShader)
{
#if USE_FXC

  return compileShaderFXC(
      p_DbgName,
      p_ShaderPath,
      p_Defines,
      p_CompileFlags,
      p_ShaderType,
      p_EntryPoint,
      p_OutShader);

#else // USE_DXC

  return compileShaderDXC(
    p_DbgName,
    p_ShaderPath,
    p_NumDefines,
    p_Defines,
    p_ShaderType,
    p_EntryPoint,
    p_OutShader);
#endif

}
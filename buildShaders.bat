@echo off

setlocal
set DXC_COMPILER="C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/dxc.exe"
set DXC_SPIRV_PATH="C:/VulkanSDK/1.3.280.0/Bin/dxc.exe"
set FXC_PATH="C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/fxc.exe"

start "CompileShaders" "_bin/ShaderMake.exe" --config="shaders/shaders.cfg" --out="_bin/_Shaders/vRenderer/dxil" --platform="DXIL" --binaryBlob --outputExt=".bin" --include="C:/Users/VGabriel/Desktop/github/vrenderer/donut/include" --compiler=%DXC_COMPILER% --shaderModel="6_5" --useAPI 

start "CompileShaders" "_bin/ShaderMake.exe" --config="shaders/shaders.cfg" --out="_bin/_Shaders/vRenderer/dxbc" --platform="DXBC" --binaryBlob --outputExt=".bin" --include="C:/Users/VGabriel/Desktop/github/vrenderer/donut/include" --compiler=%FXC_PATH% --useAPI 

start "CompileShaders" "_bin/ShaderMake.exe" --config="shaders/shaders.cfg" --out="_bin/_Shaders/vRenderer/spirv" --platform="SPIRV" --binaryBlob --outputExt=".bin" --include="C:/Users/VGabriel/Desktop/github/vrenderer/donut/include" -D SPIRV --compiler=%DXC_SPIRV_PATH% --vulkanVersion="1.2" --useAPI --tRegShift="0" --sRegShift="128" --bRegShift="256" --uRegShift="384"
# ============================================================
# DemonEngine – Water System Integration Guide
# ============================================================

## Files delivered
```
WaterSystem/
├── WaterComponent.h        – ECS component + WaterSettings struct
├── WaterSystem.h/.cpp      – Manager: owns FFTOcean + WaterRenderer
├── FFTOcean.h/.cpp         – GPU FFT wave simulation (compute)
├── WaterRenderer.h/.cpp    – DX12 render passes
├── WaterPanel.h            – ImGui editor panel (inline impl)
└── Shaders/
    ├── FFTOcean.hlsl        – Compute: Phillips spectrum + FFT butterfly + normal gen
    └── Water.hlsl           – VS/PS: surface + SSR + refraction + foam + underwater
```

## CMakeLists.txt snippet
```cmake
target_sources(DemonEditor PRIVATE
    WaterSystem/WaterSystem.cpp
    WaterSystem/WaterRenderer.cpp
    WaterSystem/FFTOcean.cpp
)
# Copy shaders to output
add_custom_command(TARGET DemonEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/WaterSystem/Shaders"
        "$<TARGET_FILE_DIR:DemonEditor>/Shaders"
)
```

## Integration steps

### 1. Register in Scene/Engine
```cpp
// In Engine.cpp or DemonEditor app init:
m_WaterSystem = DEMON_NEW(WaterSystem);
m_WaterSystem->Initialize(m_Device, initCmd, m_Width, m_Height);

// Create an ocean body:
WaterComponent* ocean = m_WaterSystem->CreateComponent();
ocean->IsOcean    = true;
ocean->WaterLevel = 0.f;
```

### 2. Hook into render loop
```cpp
// BEFORE main scene render:
m_WaterSystem->Update(dt, *m_Camera);
m_WaterSystem->RenderPlanarReflection(cmd, *m_Scene, *m_Camera);

// AFTER opaque scene, BEFORE tonemap:
m_WaterSystem->Render(cmd, *m_Camera,
    m_GBuffer.SceneColorSRV,
    m_GBuffer.SceneDepthSRV);
```

### 3. Scene::RenderForReflection (you need to implement)
```cpp
void Scene::RenderForReflection(CommandList& cmd, const Camera& reflCam, float clipY)
{
    // Set a clip plane in your scene CB: clip objects below waterY
    // Then call your normal opaque render pass with reflCam
    // Skip terrain if it's below water
    RenderOpaqueObjects(cmd, reflCam, clipY);
}
```

### 4. Add WaterPanel to DemonEditor
```cpp
// In EditorLayer::OnImGui():
m_WaterPanel.OnImGui(*m_WaterSystem);
```

## Shader binding (Water.hlsl register layout)
| Register | Resource              |
|----------|-----------------------|
| b0       | WaterCB               |
| t0       | Planar reflection RT  |
| t1       | Detail normal map 0   |
| t2       | Detail normal map 1   |
| t3       | Caustics LUT          |
| t4       | FFT displacement map  |
| t5       | FFT normal map        |
| t6       | FFT foam map          |
| t7       | Scene color           |
| t8       | Scene depth           |
| s0       | Linear wrap sampler   |
| s1       | Linear clamp sampler  |

## Feature matrix vs UE5 Water
| Feature                    | UE5 Water       | DemonEngine      |
|----------------------------|-----------------|------------------|
| FFT ocean simulation       | ✅ GPU          | ✅ GPU compute   |
| Planar reflections         | ✅              | ✅ half-res RT   |
| Screen-space reflections   | ✅              | ✅ 64-step march |
| Refraction                 | ✅              | ✅ depth-distort |
| Subsurface scattering      | ✅ volumetric   | ✅ depth-approx  |
| Foam (Jacobian)            | ✅              | ✅               |
| Shore interaction          | ✅              | ✅ depth fade    |
| Caustics                   | ✅              | ✅ LUT-based     |
| Underwater post-process    | ✅              | ✅               |
| Buoyancy / physics         | ✅              | ❌ (future)      |
| River splines              | ✅              | ❌ (future)      |

## Performance tips
- **FFT 256** instead of 512 at 1080p: barely visible, 4x faster simulate
- **SSR 32 steps** instead of 64 on lower-end: half PS time
- **Half-res planar RT** is already default; go quarter-res for budget builds
- Use `WaterComponent::OverrideSettings` to give calm lake bodies
  cheaper settings (no SSR, no foam)

// Copyright 2016 Benjamin Glatzel
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Precompiled header file
#include "stdafx_vulkan.h"
#include "stdafx.h"

namespace Intrinsic
{
namespace Renderer
{
namespace Vulkan
{
namespace RenderProcess
{
namespace
{
typedef void (*RenderPassRenderFunction)(float);
typedef void (*RenderPassUpdateResDepResFunction)(void);

namespace RenderStepType
{
enum Enum
{
  kImageMemoryBarrier,

  kRenderPassGenericFullscreen,
  kRenderPassGenericMesh,
  kRenderPassGenericBlur,

  kRenderPassDebug,
  kRenderPassPerPixelPicking,
  kRenderPassShadow,
  kRenderPassLighting,
  kRenderPassVolumetricLighting,
  kRenderPassBloom
};
}

_INTR_HASH_MAP(Name, RenderStepType::Enum)
_renderStepTypeMapping = {
    {"RenderPassDebug", RenderStepType::kRenderPassDebug},
    {"RenderPassPerPixelPicking", RenderStepType::kRenderPassPerPixelPicking},
    {"RenderPassShadow", RenderStepType::kRenderPassShadow},
    {"RenderPassLighting", RenderStepType::kRenderPassLighting},
    {"RenderPassVolumetricLighting",
     RenderStepType::kRenderPassVolumetricLighting},
    {"RenderPassBloom", RenderStepType::kRenderPassBloom}};

struct RenderPassInterface
{
  RenderPassRenderFunction render;
  RenderPassUpdateResDepResFunction onReinitRendering;
};

_INTR_HASH_MAP(RenderStepType::Enum, RenderPassInterface)
_renderStepFunctionMapping = {
    {RenderStepType::kRenderPassDebug,
     {RenderPass::Debug::render, RenderPass::Debug::onReinitRendering}},
    {RenderStepType::kRenderPassPerPixelPicking,
     {RenderPass::PerPixelPicking::render,
      RenderPass::PerPixelPicking::onReinitRendering}},
    {RenderStepType::kRenderPassShadow,
     {RenderPass::Shadow::render, RenderPass::Shadow::onReinitRendering}},
    {RenderStepType::kRenderPassLighting,
     {RenderPass::Lighting::render, RenderPass::Lighting::onReinitRendering}},
    {RenderStepType::kRenderPassVolumetricLighting,
     {RenderPass::VolumetricLighting::render,
      RenderPass::VolumetricLighting::onReinitRendering}},
    {RenderStepType::kRenderPassBloom,
     {RenderPass::Bloom::render, RenderPass::Bloom::onReinitRendering}}};

struct RenderStep
{
  RenderStep(uint8_t p_Type, uint8_t p_RenderPassIndex)
  {
    data = (uint32_t)p_Type | (uint32_t)p_RenderPassIndex << 8u;
    resourceName = 0x0u;
  }

  RenderStep(uint8_t p_Type, uint8_t p_SourceLayout, uint8_t p_TargetLayout,
             const Name& p_ResourceName)
  {
    data = (uint32_t)p_Type | (uint32_t)p_SourceLayout << 8u |
           (uint32_t)p_TargetLayout << 16u;
    resourceName = p_ResourceName;
  }

  _INTR_INLINE uint8_t getType() const { return data & 0xFF; }
  _INTR_INLINE uint8_t getIndex() const { return (data >> 8u) & 0xFF; }
  _INTR_INLINE uint8_t getSourceLayout() const { return (data >> 8u) & 0xFF; }
  _INTR_INLINE uint8_t getTargetLayout() const { return (data >> 16u) & 0xFF; }

  Name resourceName;
  uint32_t data;
};

_INTR_ARRAY(RenderPass::GenericFullscreen) _renderPassesGenericFullScreen;
_INTR_ARRAY(RenderPass::GenericBlur) _renderPassesGenericBlur;
_INTR_ARRAY(RenderPass::GenericMesh) _renderPassesGenericMesh;
_INTR_ARRAY(RenderStep) _renderSteps;

_INTR_INLINE void executeRenderSteps(float p_DeltaT)
{
  using namespace Resources;

  for (uint32_t i = 0u; i < _renderSteps.size(); ++i)
  {
    const RenderStep& step = _renderSteps[i];

    switch (step.getType())
    {
    case RenderStepType::kRenderPassGenericFullscreen:
      _renderPassesGenericFullScreen[step.getIndex()].render(p_DeltaT);
      continue;
    case RenderStepType::kRenderPassGenericBlur:
      _renderPassesGenericBlur[step.getIndex()].render(p_DeltaT);
      continue;
    case RenderStepType::kRenderPassGenericMesh:
      _renderPassesGenericMesh[step.getIndex()].render(p_DeltaT);
      continue;
    case RenderStepType::kImageMemoryBarrier:
      ImageManager::insertImageMemoryBarrier(
          ImageManager::_getResourceByName(step.resourceName),
          (VkImageLayout)step.getSourceLayout(),
          (VkImageLayout)step.getTargetLayout());
      continue;
    }

    auto renderPassFunction =
        _renderStepFunctionMapping.find((RenderStepType::Enum)step.getType());
    if (renderPassFunction != _renderStepFunctionMapping.end())
    {
      renderPassFunction->second.render(p_DeltaT);
      continue;
    }

    _INTR_ASSERT(false && "Failed to execute render step");
  }
}
}

// Static members
Dod::RefArray Default::_activeFrustums;
LockFreeStack<Core::Dod::Ref, _INTR_MAX_DRAW_CALL_COUNT>
    RenderProcess::Default::_visibleDrawCallsPerMaterialPass
        [_INTR_MAX_FRUSTUMS_PER_FRAME_COUNT][_INTR_MAX_MATERIAL_PASS_COUNT];
LockFreeStack<Dod::Ref, _INTR_MAX_MESH_COMPONENT_COUNT> RenderProcess::Default::
    _visibleMeshComponents[_INTR_MAX_FRUSTUMS_PER_FRAME_COUNT];

// <-

void Default::loadRendererConfig()
{
  // Destroy render passes
  {
    for (uint32_t i = 0u; i < _renderPassesGenericFullScreen.size(); ++i)
    {
      _renderPassesGenericFullScreen[i].destroy();
    }
    _renderPassesGenericFullScreen.clear();
    for (uint32_t i = 0u; i < _renderPassesGenericMesh.size(); ++i)
    {
      _renderPassesGenericMesh[i].destroy();
    }
    _renderPassesGenericMesh.clear();
  }

  _renderSteps.clear();

  rapidjson::Document rendererConfig;
  {
    const _INTR_STRING rendererConfigFilePath =
        "config/" + Settings::Manager::_rendererConfig;

    FILE* fp = fopen(rendererConfigFilePath.c_str(), "rb");

    if (fp == nullptr)
    {
      _INTR_LOG_WARNING("Failed to load renderer config from file '%s'...",
                        Settings::Manager::_rendererConfig.c_str());
      return;
    }

    char* readBuffer = (char*)Tlsf::MainAllocator::allocate(65536u);
    {
      rapidjson::FileReadStream is(fp, readBuffer, 65536u);
      rendererConfig.ParseStream(is);
      fclose(fp);
    }
    Tlsf::MainAllocator::free(readBuffer);
  }

  _INTR_LOG_INFO("Loading renderer config '%s'...",
                 rendererConfig["name"].GetString());

  const rapidjson::Value& renderSteps = rendererConfig["renderSteps"];
  const rapidjson::Value& uniformBuffers = rendererConfig["uniformBuffers"];

  UniformManager::load(uniformBuffers);

  for (uint32_t i = 0u; i < renderSteps.Size(); ++i)
  {
    const rapidjson::Value& renderStepDesc = renderSteps[i];

    if (renderStepDesc["type"] == "ImageMemoryBarrier")
    {
      _renderSteps.push_back(
          RenderStep(RenderStepType::kImageMemoryBarrier,
                     Helper::mapImageLayout(
                         renderStepDesc["sourceImageLayout"].GetString()),
                     Helper::mapImageLayout(
                         renderStepDesc["targetImageLayout"].GetString()),
                     renderStepDesc["image"].GetString()));
    }
    else if (renderStepDesc["type"] == "RenderPassGenericFullscreen")
    {
      _renderPassesGenericFullScreen.push_back(RenderPass::GenericFullscreen());
      RenderPass::GenericFullscreen& renderPass =
          _renderPassesGenericFullScreen.back();
      renderPass.init(renderStepDesc);

      _renderSteps.push_back(
          RenderStep(RenderStepType::kRenderPassGenericFullscreen,
                     (uint8_t)_renderPassesGenericFullScreen.size() - 1u));
    }
    else if (renderStepDesc["type"] == "RenderPassGenericBlur")
    {
      _renderPassesGenericBlur.push_back(RenderPass::GenericBlur());
      RenderPass::GenericBlur& renderPass = _renderPassesGenericBlur.back();
      renderPass.init(renderStepDesc);

      _renderSteps.push_back(
          RenderStep(RenderStepType::kRenderPassGenericBlur,
                     (uint8_t)_renderPassesGenericBlur.size() - 1u));
    }
    else if (renderStepDesc["type"] == "RenderPassGenericMesh")
    {
      _renderPassesGenericMesh.push_back(RenderPass::GenericMesh());
      RenderPass::GenericMesh& renderPass = _renderPassesGenericMesh.back();
      renderPass.init(renderStepDesc);

      _renderSteps.push_back(
          RenderStep(RenderStepType::kRenderPassGenericMesh,
                     (uint8_t)_renderPassesGenericMesh.size() - 1u));
    }
    else if (_renderStepTypeMapping.find(renderStepDesc["type"].GetString()) !=
             _renderStepTypeMapping.end())
    {
      RenderStep renderStep =
          RenderStep(_renderStepTypeMapping[renderStepDesc["type"].GetString()],
                     (uint8_t)-1);
      _renderStepFunctionMapping[(RenderStepType::Enum)renderStep.getType()]
          .onReinitRendering();
      _renderSteps.push_back(renderStep);
    }
    else
    {
      _INTR_ASSERT(false && "Invalid render step type provided");
    }
  }
}

void Default::renderFrame(float p_DeltaT)
{
  // Resize the swap chain (if necessary)
  RenderSystem::resizeSwapChain();

  RenderSystem::beginFrame();
  {
    _INTR_PROFILE_GPU("Render Frame");
    _INTR_PROFILE_CPU("Render System", "Render Frame");

    // Preparation and culling
    {
      _INTR_PROFILE_CPU("Render System", "Preparation and Culling");

      Components::CameraManager::updateFrustums(
          Components::CameraManager::_activeRefs);
      RenderPass::Shadow::prepareFrustums();
      Core::Resources::FrustumManager::prepareForRendering(
          Core::Resources::FrustumManager::_activeRefs);

      // Update render pass uniform data
      {
        UniformManager::resetAllocator();
        UniformManager::updatePerFrameUniformBufferData(
            World::getActiveCamera());
        UniformManager::updateUniformBuffers();
      }

      _activeFrustums.clear();
      _activeFrustums.push_back(
          Components::CameraManager::_frustum(World::getActiveCamera()));
      _activeFrustums.insert(_activeFrustums.end(),
                             RenderPass::Shadow::_shadowFrustums.begin(),
                             RenderPass::Shadow::_shadowFrustums.end());

      Core::Resources::FrustumManager::cullNodes(_activeFrustums);
    }

    // Collect visible draw calls and mesh components
    {
      Components::MeshManager::collectDrawCallsAndMeshComponents();
      Components::MeshManager::updatePerInstanceData(0u);
    }

    // Execute render steps
    {
      executeRenderSteps(p_DeltaT);
    }
  }

  RenderSystem::endFrame();
}
}
}
}
}

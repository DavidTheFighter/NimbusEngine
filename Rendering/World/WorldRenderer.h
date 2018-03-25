/*
 * MIT License
 * 
 * Copyright (c) 2017 David Allen
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * WorldRenderer.h
 * 
 * Created on: Oct 10, 2017
 *     Author: david
 */

#ifndef RENDERING_WORLD_WORLDRENDERER_H_
#define RENDERING_WORLD_WORLDRENDERER_H_

#define STATIC_OBJECT_STREAMING_BUFFER_SIZE (4 * 1024 * 1024)

#include <common.h>
#include <Rendering/Renderer/RendererEnums.h>
#include <Rendering/Renderer/RendererObjects.h>
#include <World/SortedOctree.h>

class WorldHandler;
class StarlightEngine;
class TerrainRenderer;

struct LevelStaticObject;
struct LevelStaticObjectType;

typedef struct LevelStaticObjectStreamingData
{
		/*
		 * The data that will be rendered. It's a little complex and long winded, and might change.
		 *
		 * In order, it goes: map of material hashes
		 * 						map of mesh hashes
		 * 							vector of mesh lods (size() == mesh lod number, always)
		 * 								vector of objs using this data
		 */
		std::map<size_t, std::map<size_t, std::vector<std::vector<LevelStaticObject> > > > data;
} LevelStaticObjectStreamingData;

class WorldRenderer
{
	public:

		TextureView gbuffer_AlbedoRoughnessView;
		TextureView gbuffer_NormalMetalnessView;
		TextureView gbuffer_DepthView;

		PipelineInputLayout materialPipelineInputLayout;
		Pipeline defaultMaterialPipeline;

		RenderPass gbufferRenderPass;
		Framebuffer gbufferFramebuffer;

		Texture gbuffer_AlbedoRoughness; // RGB - Albedo, A - Roughness
		Texture gbuffer_NormalMetalness; // RGB - World Normal, A - Metalness
		Texture gbuffer_Depth;
		Sampler testSampler;

		StarlightEngine *engine;
		WorldHandler *world;

		TerrainRenderer *terrainRenderer;

		CommandPool testCommandPool;

		glm::mat4 camProjMat;
		glm::mat4 camViewMat;
		glm::vec3 cameraPosition;

		WorldRenderer (StarlightEngine *enginePtr, WorldHandler *worldHandlerPtr);
		virtual ~WorldRenderer ();

		void init (suvec2 gbufferDimensions);
		void destroy ();

		void update ();

		void render3DWorld ();
		void renderWorldStaticMeshes (CommandBuffer &cmdBuffer);

		void setGBufferDimensions (suvec2 gbufferDimensions);
		suvec2 getGBufferDimensions ();

	private:

		void traverseOctreeNode (SortedOctree<LevelStaticObjectType, LevelStaticObject> &node, LevelStaticObjectStreamingData &data);

		void createGBuffer ();
		void destroyGBuffer ();
		void createRenderPasses ();
		void createTestMaterialPipeline ();

		LevelStaticObjectStreamingData getStaticObjStreamingData ();

		bool isDestroyed; // So that when an instance is deleted, destroy() is always called, either by the user or by the destructor

		suvec2 gbufferRenderDimensions;

		size_t worldStreamingBufferOffset;
		Buffer worldStreamingBuffer;
		void *worldStreamingBufferData;
};

#endif /* RENDERING_WORLD_WORLDRENDERER_H_ */

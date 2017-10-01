/*
 * RenderGame.h
 *
 *  Created on: Sep 15, 2017
 *      Author: david
 */

#ifndef RENDERING_RENDERGAME_H_
#define RENDERING_RENDERGAME_H_

#include <common.h>
#include <Rendering/Renderer/Renderer.h>
#include <Resources/ResourceManager.h>
#include <Game/Events/EventHandler.h>

class RenderGame
{
	public:

		Renderer *renderer;
		ResourceManager *resources;

		glm::vec2 gbufferRenderDimensions;

		RenderGame (Renderer *rendererBackend, ResourceManager *rendererResourceManager);
		virtual ~RenderGame ();

		void init ();
		void renderGame ();

		static void gameWindowResizedCallback (const EventWindowResizeData &eventData, void *usrPtr);

	private:

		ResourceMesh testMesh;

		RenderPass testGBufferRenderPass;
		Pipeline defaultMaterialPipeline;
		DescriptorSet testMaterialDescriptorSet;
		Framebuffer testGBufferFramebuffer;

		CommandPool testGBufferCommandPool;
		CommandBuffer testGBufferCommandBuffer;

		Texture gbuffer_AlbedoRoughness;
		Texture gbuffer_NormalMetalness;
		Texture gbuffer_Depth;

		TextureView gbuffer_AlbedoRoughnessView;
		TextureView gbuffer_NormalMetalnessView;
		TextureView gbuffer_DepthView;

		Sampler gbufferSampler;

		void createTestCommandBuffer ();
		void createTestMesh ();
		void createGBuffer ();
		void destroyGBuffer ();
		void createRenderPass ();
		void createGraphicsPipeline ();
		void createDescriptorSets ();
};

#endif /* RENDERING_RENDERERS_RENDERGAME_H_ */

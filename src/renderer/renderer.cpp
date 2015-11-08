#include "renderer.h"

#include "core/array.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/json_serializer.h"
#include "core/lifo_allocator.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec4.h"
#include "debug/allocator.h"
#include "debug/debug.h"
#include "engine.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/model.h"
#include "renderer/model_manager.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/shader_manager.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"
#include "universe/universe.h"
#include <bgfx/bgfx.h>
#include <cstdio>


namespace bx
{

	struct AllocatorI
	{
		virtual ~AllocatorI() = 0;
		virtual void* alloc(size_t _size, size_t _align, const char* _file, uint32_t _line) = 0;
		virtual void free(void* _ptr, size_t _align, const char* _file, uint32_t _line) = 0;
	};

	inline AllocatorI::~AllocatorI()
	{
	}

	struct ReallocatorI : public AllocatorI
	{
		virtual void* realloc(void* _ptr, size_t _size, size_t _align, const char* _file, uint32_t _line) = 0;
	};


} // namespace bx


namespace bgfx
{

struct PlatformData
{
	void* ndt;			//< Native display type
	void* nwh;			//< Native window handle
	void* context;		//< GL context, or D3D device
	void* backBuffer;   //< GL backbuffer, or D3D render target view
	void* backBufferDS; //< Backbuffer depth/stencil.
};


void setPlatformData(const PlatformData& _pd);

} // namespace bgfx


namespace Lumix
{


static const uint32_t GLOBAL_LIGHT_HASH = crc32("global_light");
static const uint32_t POINT_LIGHT_HASH = crc32("point_light");
static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const uint32_t CAMERA_HASH = crc32("camera");


struct BGFXAllocator : public bx::ReallocatorI
{

	BGFXAllocator(Lumix::IAllocator& source)
		: m_source(source)
	{
	}


	virtual ~BGFXAllocator()
	{
	}


	virtual void* alloc(size_t _size, size_t _align, const char* _file, uint32_t _line) override 
	{
		return m_source.allocate(_size);
	}


	virtual void free(void* _ptr, size_t _align, const char* _file, uint32_t _line) override 
	{
		m_source.deallocate(_ptr);
	}


	virtual void* realloc(void* _ptr,
		size_t _size,
		size_t _align,
		const char* _file,
		uint32_t _line) override
	{
		return m_source.reallocate(_ptr, _size);
	}


	Lumix::IAllocator& m_source;

};


struct RendererImpl : public Renderer
{
	struct CallbackStub : public bgfx::CallbackI
	{
		virtual void fatal(bgfx::Fatal::Enum _code, const char* _str) override
		{
			Lumix::g_log_error.log("bgfx") << _str;
			if (bgfx::Fatal::DebugCheck == _code)
			{
				Lumix::Debug::debugBreak();
			}
			else
			{
				abort();
			}
		}


		virtual void traceVargs(const char* _filePath,
			uint16_t _line,
			const char* _format,
			va_list _argList) override
		{
			char tmp[2048];
			vsnprintf(tmp, sizeof(tmp), _format, _argList);
			Lumix::g_log_info.log("bgfx") << _filePath << "(" << _line << ") " << tmp;
		}


		virtual void screenShot(const char*,
			uint32_t,
			uint32_t,
			uint32_t,
			const void*,
			uint32_t,
			bool) override
		{
			ASSERT(false);
		}


		virtual void captureBegin(uint32_t,
			uint32_t,
			uint32_t,
			bgfx::TextureFormat::Enum,
			bool) override
		{
			ASSERT(false);
		}


		virtual uint32_t cacheReadSize(uint64_t) override { return 0; }
		virtual bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
		virtual void cacheWrite(uint64_t, const void*, uint32_t) override {}
		virtual void captureEnd() override { ASSERT(false); }
		virtual void captureFrame(const void*, uint32_t) override { ASSERT(false); }
	};


	RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_texture_manager(m_allocator)
		, m_model_manager(m_allocator, *this)
		, m_material_manager(*this, m_allocator)
		, m_shader_manager(*this, m_allocator)
		, m_shader_binary_manager(*this, m_allocator)
		, m_pipeline_manager(*this, m_allocator)
		, m_passes(m_allocator)
		, m_shader_defines(m_allocator)
		, m_bgfx_allocator(m_allocator)
		, m_frame_allocator(m_allocator, 10 * 1024 * 1024)
	{
		bgfx::PlatformData d;
		if (s_platform_data)
		{
			memset(&d, 0, sizeof(d));
			d.nwh = s_platform_data;
			bgfx::setPlatformData(d);
		}
		bgfx::init(bgfx::RendererType::Count, 0, 0, &m_callback_stub/*, &m_bgfx_allocator*/);
		bgfx::reset(800, 600);
		bgfx::setDebug(BGFX_DEBUG_TEXT);

		ResourceManager& manager = engine.getResourceManager();
		m_texture_manager.create(ResourceManager::TEXTURE, manager);
		m_model_manager.create(ResourceManager::MODEL, manager);
		m_material_manager.create(ResourceManager::MATERIAL, manager);
		m_shader_manager.create(ResourceManager::SHADER, manager);
		m_shader_binary_manager.create(ResourceManager::SHADER_BINARY, manager);
		m_pipeline_manager.create(ResourceManager::PIPELINE, manager);

		m_current_pass_hash = crc32("MAIN");
		m_view_counter = 0;
	}

	~RendererImpl()
	{
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		m_shader_binary_manager.destroy();
		m_pipeline_manager.destroy();

		bgfx::frame();
		bgfx::frame();
		bgfx::shutdown();
	}


	virtual IScene* createScene(UniverseContext& ctx) override
	{
		return RenderScene::createInstance(
			*this, m_engine, *ctx.m_universe, true, m_allocator);
	}


	virtual void destroyScene(IScene* scene) override
	{
		RenderScene::destroyInstance(static_cast<RenderScene*>(scene));
	}


	virtual bool create() override { return true; }


	virtual void destroy() override {}


	virtual const char* getName() const override { return "renderer"; }


	virtual Engine& getEngine() override { return m_engine; }


	virtual const char* getShaderDefine(int define_idx) override
	{
		return m_shader_defines[define_idx];
	}


	virtual int getShaderDefineIdx(const char* define) override
	{
		for (int i = 0; i < m_shader_defines.size(); ++i)
		{
			if (strcmp(m_shader_defines[i], define) == 0)
			{
				return i;
			}
		}

		auto& new_define = m_shader_defines.pushEmpty();
		copyString(new_define, define);
		return m_shader_defines.size() - 1;
	}


	virtual int getPassIdx(const char* pass) override
	{
		for (int i = 0; i < m_passes.size(); ++i)
		{
			if (strcmp(m_passes[i], pass) == 0)
			{
				return i;
			}
		}

		auto& new_pass = m_passes.pushEmpty();
		copyString(new_pass, pass);
		return m_passes.size() - 1;
	}


	virtual void makeScreenshot(const Path& filename) override
	{
		bgfx::saveScreenShot(filename.c_str());
	}


	virtual void resize(int w, int h) override
	{
		bgfx::reset(w, h);
	}


	virtual void frame() override
	{
		PROFILE_FUNCTION();
		bgfx::frame();
		m_view_counter = 0;
	}


	virtual int getViewCounter() const override
	{
		return m_view_counter;
	}


	virtual void viewCounterAdd() override
	{
		++m_view_counter;
	}


	virtual LIFOAllocator& getFrameAllocator() override
	{
		return m_frame_allocator;
	}


	typedef char ShaderDefine[32];


	Engine& m_engine;
	Debug::Allocator m_allocator;
	Array<ShaderCombinations::Pass> m_passes;
	Array<ShaderDefine> m_shader_defines;
	CallbackStub m_callback_stub;
	LIFOAllocator m_frame_allocator;
	TextureManager m_texture_manager;
	MaterialManager m_material_manager;
	ShaderManager m_shader_manager;
	ShaderBinaryManager m_shader_binary_manager;
	ModelManager m_model_manager;
	PipelineManager m_pipeline_manager;
	uint32_t m_current_pass_hash;
	int m_view_counter;
	BGFXAllocator m_bgfx_allocator;

	static void* s_platform_data;
};


void* RendererImpl::s_platform_data = nullptr;


void Renderer::setInitData(void* data)
{
	RendererImpl::s_platform_data = data;
}


extern "C"
{
	LUMIX_RENDERER_API IPlugin* createPlugin(Engine& engine)
	{
		RendererImpl* r = engine.getAllocator().newObject<RendererImpl>(engine);
		if (r->create())
		{
			return r;
		}
		engine.getAllocator().deleteObject(r);
		return nullptr;
	}
}


} // ~namespace Lumix




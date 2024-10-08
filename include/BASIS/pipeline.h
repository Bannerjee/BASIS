#pragma once

#include <BASIS/interfaces.h>

#include <span>
#include <vector>
#include <cstdint>
#include <string_view>

namespace BASIS
{
	
struct Buffer;
struct Shader : public IGLObject
{
	explicit Shader(ShaderType type,std::string_view src,std::string_view name="");

	Shader(Shader&&) noexcept;
	Shader& operator=(Shader&&) noexcept;
	~Shader();
};
struct VertexBinding
{
	
	std::uint32_t 	location{};
	std::uint32_t 	binding{};
	std::uint32_t 	offset{};
	Format			fmt{};
};
using VertexInputState = std::vector<VertexBinding>;

struct PipelineInfo
{
	PrimitiveMode mode{PrimitiveMode::TRIANGLES};
	std::vector<VertexBinding> vertexState{};
};
struct PipelineCreateInfo : public PipelineInfo
{
	const Shader* vertex{};
	const Shader* fragment{};
	const Shader* tesselationControl{};
	const Shader* tesselationEvaluation{};
};
struct Pipeline : public IGLObject
{
	explicit Pipeline(const PipelineCreateInfo& info,std::string_view name="");
	~Pipeline();
	
	Pipeline(Pipeline&&) noexcept;
	Pipeline& operator=(Pipeline&&) noexcept;
	
	const PipelineInfo& info() const noexcept { return m_info; }
	private:
	PipelineInfo m_info{};
};
struct ComputePipeline : public IGLObject
{
	explicit ComputePipeline(const Shader& computeShader,std::string_view name="");
	~ComputePipeline();
	
	ComputePipeline(ComputePipeline&&) noexcept;
	ComputePipeline& operator=(ComputePipeline&&) noexcept;
	
};
	
	
}

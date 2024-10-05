#include <BASIS/buffer.h>
#include <BASIS/manager.h>
#include <BASIS/texture.h>
#include <BASIS/exception.h>

#include <utility>
#include <cassert>
#include <cstddef>
#include <algorithm>
#include <filesystem>

#include <glad/gl.h>

#include <stb_image.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

namespace fg = fastgltf;
namespace bs = BASIS;

namespace BASIS
{
std::shared_ptr<Texture> Manager::getTexture(std::uint64_t uniqueHash)
{
	if(auto it = m_textures.find(uniqueHash);it != m_textures.end()) return it->second;
	throw AssetException("getTexture() can't return texture with such hash as it doesn't exist");
}
std::shared_ptr<Texture> Manager::getTexture(std::uint64_t uniqueHash,std::string_view path,bool SRGB)
{
	if(auto it = m_textures.find(uniqueHash);it != m_textures.end()) return it->second;
	
	return m_textures.insert({uniqueHash,std::make_shared<Texture>(loadTexture(path,SRGB))}).first->second;
}
std::shared_ptr<Texture> Manager::getTexture(std::uint64_t uniqueHash,const std::byte* px,std::size_t size,bool SRGB)
{
	if(auto it = m_textures.find(uniqueHash);it != m_textures.end()) return it->second;
	return m_textures.insert({uniqueHash,std::make_shared<Texture>(loadTexture(px,size,SRGB))}).first->second;
}
std::shared_ptr<Texture> Manager::getTexture(std::uint64_t uniqueHash,const std::uint8_t* px,std::size_t size,bool SRGB)
{
	return getTexture(uniqueHash,reinterpret_cast<const std::byte*>(px),size,SRGB);	
}
static size_t hashSamplerInfo(const SamplerInfo& inf)
{
	// maybe unsafe ?
	size_t totalHash{};
	auto tuple = std::make_tuple(
	inf.lodBias,inf.minLod,inf.maxLod,
	inf.compareEnable,inf.minFilter,inf.magFilter,
	inf.mipmapFilter,inf.anisotropy,inf.addressModeU,
	inf.addressModeV,inf.addressModeW,inf.compareMode);

	hash_combine(totalHash,hash<decltype(tuple)>{}(tuple));

	return totalHash;
}
Sampler Manager::getSampler(const SamplerInfo& inf) noexcept
{
	//TODO: add border color selection
	size_t iHash = hashSamplerInfo(inf);
	if(auto it = m_samplers.find(iHash);it != m_samplers.end()) return it->second;

	uint32_t m_id;
	glCreateSamplers(1, &m_id);

    glSamplerParameteri(m_id,GL_TEXTURE_COMPARE_MODE,inf.compareEnable ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
    glSamplerParameteri(m_id, GL_TEXTURE_COMPARE_FUNC,enumToGL(inf.compareMode));

    glSamplerParameteri(m_id, GL_TEXTURE_MAG_FILTER, inf.magFilter == Filter::LINEAR ? GL_LINEAR : GL_NEAREST);
    glSamplerParameteri(m_id, GL_TEXTURE_MIN_FILTER, enumToGL(inf.minFilter));

    glSamplerParameteri(m_id, GL_TEXTURE_WRAP_S, enumToGL(inf.addressModeU));
    glSamplerParameteri(m_id, GL_TEXTURE_WRAP_T, enumToGL(inf.addressModeV));
    glSamplerParameteri(m_id, GL_TEXTURE_WRAP_R, enumToGL(inf.addressModeW));

    glSamplerParameterf(m_id,GL_TEXTURE_MAX_ANISOTROPY,static_cast<float>(enumToGL(inf.anisotropy)));
    glSamplerParameterf(m_id, GL_TEXTURE_LOD_BIAS, inf.lodBias);
    glSamplerParameterf(m_id, GL_TEXTURE_MIN_LOD, inf.minLod);
    glSamplerParameterf(m_id, GL_TEXTURE_MAX_LOD, inf.maxLod);
    return m_samplers.insert({iHash,Sampler(m_id)}).second;
}

static void primitiveToVertices(
const fg::Asset& asset,
const fg::Primitive& primitive,
std::vector<Vertex>& vertices)
{
    std::size_t offset = vertices.size();
    assert(primitive.indicesAccessor);
	auto* posAttribute = primitive.findAttribute("POSITION");
	assert(posAttribute != primitive.attributes.end() && "Primitive must contain POSITION attribute");
	auto& positionAccessor = asset.accessors[posAttribute->accessorIndex];
	vertices.resize(offset + positionAccessor.count);
	fg::iterateAccessorWithIndex<glm::vec3>(asset,positionAccessor,
	[&](glm::vec3 position, std::size_t idx) 
	{ 
		vertices[offset+idx].pos = position; 
	});

	if (auto* attr = primitive.findAttribute("TEXCOORD_0");attr != primitive.attributes.end())
	{
		auto& accessor = asset.accessors[attr->accessorIndex];
		fg::iterateAccessorWithIndex<glm::vec2>(asset,accessor,
		[&](glm::vec2 uv, std::size_t idx)
		{ 
			vertices[offset+idx].uv = uv; 
		});
	}
	if (auto* attr = primitive.findAttribute("NORMAL");attr != primitive.attributes.end())
	{
		auto& accessor = asset.accessors[attr->accessorIndex];
		fg::iterateAccessorWithIndex<glm::vec3>(asset,accessor,
		[&](glm::vec3 norm, std::size_t idx)
		{ 
			vertices[offset+idx].normal = norm; 
		});
	}
	if (auto* attr = primitive.findAttribute("COLOR_0");attr != primitive.attributes.end())
	{
		auto& accessor = asset.accessors[attr->accessorIndex];
		if(accessor.type == fastgltf::AccessorType::Vec3)
		{
			fg::iterateAccessorWithIndex<glm::vec3>(asset,accessor,
			[&](glm::vec3 color, std::size_t idx)
			{ 
				vertices[offset+idx].color = glm::vec4(color,1.f); 
			});
		}
		else if(accessor.type == fastgltf::AccessorType::Vec4)
		{
			fg::iterateAccessorWithIndex<glm::vec4>(asset,accessor,
			[&](glm::vec4 color, std::size_t idx)
			{ 
				vertices[offset+idx].color = color;
			});
		}
	}
	
}
static void primitiveToIndices(
const fg::Asset& asset, 
const fg::Primitive& primitive,
std::vector<std::uint32_t>& indices,
std::size_t vStart)
{
	size_t offset = indices.size();
	auto& accessor = asset.accessors[primitive.indicesAccessor.value()];
	indices.resize(offset + accessor.count);
	/*fastgltf::copyFromAccessor<std::uint32_t>(asset,accessor,indices.data() + offset);
	std::for_each(std::execution::par,indices.begin() + offset,indices.end(),
	[&](std::uint32_t& idx) { idx += vStart; });*/
	
	fastgltf::iterateAccessorWithIndex<std::uint32_t>(asset, accessor, 
	[&](std::uint32_t index, std::size_t idx) 
	{ 
		indices[offset+idx] = index + vStart; 
	});
}
static void loadNode(
std::size_t nodeIdx, 
const fg::Asset& asset,
std::vector<Node>& nodes,
std::vector<Vertex>& vBuf,
std::vector<std::uint32_t>& iBuf)
{
	const auto& inNode = asset.nodes[nodeIdx];
	auto& outNode = nodes[nodeIdx];
	
	// replace with std::visit(?)
	if (auto* trs = std::get_if<fg::TRS>(&inNode.transform))
	{
		outNode.matrix = glm::translate(glm::mat4(1.f),glm::make_vec3(trs->translation.data()));
		outNode.matrix = glm::scale(outNode.matrix,glm::make_vec3(trs->scale.data()));
		// ugly thingy, look for possible replacement
		outNode.matrix *= glm::mat4_cast(glm::make_quat(trs->rotation.value_ptr()));
	}
	else if (auto* mat = std::get_if<fg::math::fmat4x4>(&inNode.transform))
	{
		outNode.matrix = glm::make_mat4(mat->data());
	}

	if (inNode.meshIndex)
	{
		const auto& inMesh = asset.meshes[inNode.meshIndex.value()];
		outNode.mesh.primitives.resize(inMesh.primitives.size());
	
		for (auto it = inMesh.primitives.begin(); it != inMesh.primitives.end(); ++it) 
		{
			Primitive primitive;
			if (it->materialIndex.has_value()) 
			{
				primitive.materialIdx = it->materialIndex.value() + 1;// default material
			}
			
			std::size_t vStart = vBuf.size();
			primitiveToVertices(asset,*it,vBuf);						 
			primitive.firstIdx = iBuf.size();
			
			primitiveToIndices(asset,*it,iBuf,vStart);
			primitive.idxCount = iBuf.size() - primitive.firstIdx;
			outNode.mesh.primitives.emplace_back(std::move(primitive));
		}
	}
	outNode.children.resize(inNode.children.size());
	std::copy(inNode.children.begin(),inNode.children.end(),outNode.children.begin());
	std::for_each(inNode.children.begin(),inNode.children.end(),[&](auto childIdx)
	{
		nodes[childIdx].parent = nodeIdx;
	});
}
fg::Error loadGltf(std::filesystem::path path,fg::Asset* asset) 
{
    if (!std::filesystem::exists(path)) return fg::Error::InvalidPath;

    static constexpr auto supportedExtensions =
        fg::Extensions::KHR_mesh_quantization |
        fg::Extensions::KHR_texture_transform;

    fg::Parser parser(supportedExtensions);

    constexpr auto gltfOptions =
        fg::Options::DontRequireValidAssetMember |
        fg::Options::AllowDouble |
        fg::Options::LoadExternalBuffers |
        fg::Options::LoadExternalImages |
        fg::Options::GenerateMeshIndices;
    auto gltfFile = fg::MappedGltfFile::FromPath(path);
    auto res = parser.loadGltf(gltfFile.get(), path.parent_path(), gltfOptions);
    if (res.error() != fg::Error::None) return res.error();
    *asset = std::move(res.get());
    return fg::Error::None;
}
static std::vector<Texture> loadImages(const fg::Asset& asset,bool SRGB) 
{
	std::vector<Texture> images;
	images.reserve(asset.images.size());
	// std::transform does not change size and can't resize() cause no default ctor
	for(const auto& image : asset.images)
	{
		std::optional<Texture> t;
		if (auto* path = std::get_if<fg::sources::URI>(&image.data)) 
		{
			assert(path->fileByteOffset == 0);
			assert(path->uri.isLocalPath());
			t = bs::loadTexture(path->uri.path(),SRGB);
		} 
		else if (auto* vector = std::get_if<fg::sources::Array>(&image.data)) 
		{
			t = bs::loadTexture(vector->bytes.data(), vector->bytes.size(),SRGB);
		} 
		else if (auto* view = std::get_if<fg::sources::BufferView>(&image.data)) 
		{
			auto& bufferView = asset.bufferViews[view->bufferViewIndex];
			auto& buffer = asset.buffers[bufferView.bufferIndex];
			if (auto* vector = std::get_if<fg::sources::Array>(&buffer.data)) 
			{
				t = bs::loadTexture(vector->bytes.data() + bufferView.byteOffset, bufferView.byteLength,SRGB);
			}
		}
		else
		{
			throw bs::AssetException(image.name," unknown texture source(this should not happen at all)");
		}
		t->mipmap();
		
		images.push_back(std::move(*t));
	}
	return images;
}
static std::vector<GltfTexture> loadTextures(const fg::Asset& asset)
{
	std::vector<GltfTexture> textures;
	textures.reserve(asset.textures.size());
	std::for_each(asset.textures.begin(),asset.textures.end(),
	[&](const fg::Texture& t)
	{
		size_t idx{};
		if(t.webpImageIndex)
		{
			idx = t.webpImageIndex.value();
		}
		else if(t.ddsImageIndex)
		{
			idx = t.ddsImageIndex.value();
		}
		else if(t.basisuImageIndex)
		{
			 idx = t.basisuImageIndex.value();
		}
		else
		{
			idx = t.imageIndex.value();
		}
		
		textures.emplace_back(idx,t.samplerIndex.value());
	});
	return textures;
}
static std::vector<Sampler> loadSamplers(const fg::Asset& asset,Manager& manager)
{
	std::vector<Sampler> samplers;
	samplers.reserve(asset.samplers.size());
	std::for_each(asset.samplers.begin(),asset.samplers.end(),
	[&](const fg::Sampler& src)
	{
		SamplerInfo inf = {};
		inf.minFilter = src.minFilter ? static_cast<bs::Filter>(*src.minFilter) : bs::Filter::LINEAR;
		inf.magFilter = src.magFilter ? static_cast<bs::Filter>(*src.magFilter) : bs::Filter::LINEAR;
		inf.addressModeU = static_cast<bs::AddressMode>(src.wrapS);
		inf.addressModeV = static_cast<bs::AddressMode>(src.wrapT);
		samplers.push_back(manager.getSampler(inf));
	});
	return samplers;
}
static std::vector<Material> loadMaterials(const fg::Asset& asset,GLTFModel& model)
{
	std::vector<Material> materials;
	materials.reserve(asset.materials.size() + 1);
	materials.emplace_back(Material{.baseColorFactor = glm::vec4(1.f)}); // default material
	bs::Material temp{};
	for(const auto& mat : asset.materials)
	{
		temp.baseColorFactor = glm::make_vec4(mat.pbrData.baseColorFactor.data());
		switch(mat.alphaMode)
		{
			case fg::AlphaMode::Opaque : temp.alphaMask = 0.f; break;
			case fg::AlphaMode::Mask : temp.alphaMask = 1.f; break;
			case fg::AlphaMode::Blend : temp.alphaMask = 2.f; break;
		}
		temp.alphaCutoff = mat.alphaCutoff;
		temp.metallicFactor = mat.pbrData.metallicFactor;
		temp.roughnessFactor = mat.pbrData.roughnessFactor;
		if (mat.pbrData.baseColorTexture) 
		{
			temp.flags |= MaterialFlags::HasBaseColorTexture;
			auto gltfTexture = model.textures[mat.pbrData.baseColorTexture.value().textureIndex];
			auto& image = model.images[gltfTexture.imageIdx];
			temp.baseColorTexture = image.makeBindless(model.samplers[gltfTexture.samplerIdx]);
		}
		if (mat.pbrData.metallicRoughnessTexture) 
		{
			temp.flags |= MaterialFlags::HasMetallicRoughnessTexture;
			auto gltfTexture = model.textures[mat.pbrData.metallicRoughnessTexture.value().textureIndex];
			auto& image = model.images[gltfTexture.imageIdx];
			temp.metallicRoughnessTexture = image.makeBindless(model.samplers[gltfTexture.samplerIdx]);
		}
		materials.push_back(std::move(temp));
	}
	return materials;
}
std::shared_ptr<GLTFModel> Manager::getModel(std::uint64_t uniqueHash)
{
	if(auto it = m_models.find(uniqueHash);it != m_models.end()) return it->second;
	throw AssetException("getModel() can't return model with such hash as it doesn't exist");
}
std::shared_ptr<GLTFModel> Manager::getModel(std::uint64_t uniqueHash,std::string_view path,bool SRGB)
{
	if(auto it = m_models.find(uniqueHash);it != m_models.end()) return it->second;
	
	assert(materialUploadCallback && "Material upload callback not set");
	if(!std::filesystem::exists(path)) throw FileException(path," does not exist");
	
	fastgltf::Asset asset;
	if (auto err = loadGltf(path,&asset);err != fg::Error::None)
	{
		throw AssetException("Failed to load model ",path,"\nReason:",fg::getErrorMessage(err));
	}

	GLTFModel outModel;
	outModel.images = loadImages(asset,SRGB);
	outModel.textures = loadTextures(asset);
	outModel.samplers = loadSamplers(asset,*this);
	outModel.materials = loadMaterials(asset,outModel);
	outModel.materialBuffer = materialUploadCallback(outModel.materials);
	outModel.nodes.resize(asset.nodes.size());
    std::vector<uint32_t> iBuf;
	std::vector<Vertex> vBuf;
	// do not try to reduce amount of arguments
	// nodeIdx is needed because s doesn't tell us the index of processed node
	// and it can't be removed, because we set child indexes inside loadNode()
	// so parent node needs to be initialized
	for (size_t nodeIdx{};nodeIdx < asset.nodes.size();nodeIdx++) 
	{
		loadNode(nodeIdx, asset,outModel.nodes, vBuf,iBuf);
	}
	outModel.vertexBuffer = BASIS::Buffer(std::span<Vertex>(vBuf),0);
	outModel.idxBuffer = BASIS::Buffer(std::span<uint32_t>(iBuf),0);
	return m_models.insert({uniqueHash,std::make_shared<GLTFModel>(std::move(outModel))}).first->second;	
}
void Manager::insertModel(std::uint64_t uniqueHash,const std::shared_ptr<GLTFModel>& model)
{
	if(m_models.contains(uniqueHash)) throw AssetException("insertModel failed, such hash already exists");
	m_models.insert({uniqueHash,model});
}
void Manager::insertTexture(std::uint64_t uniqueHash,const std::shared_ptr<Texture>& tex)
{
	if(m_textures.contains(uniqueHash)) throw AssetException("insertTexture failed, such hash already exists");
	m_textures.insert({uniqueHash,tex});
}
Manager::~Manager()
{
	for(const auto& [_,s] : m_samplers)
	{
		glDeleteSamplers(1,&s.m_id);
	};
}
};

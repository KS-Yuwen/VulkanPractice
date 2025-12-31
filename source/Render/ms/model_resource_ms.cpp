#include "model_resource_ms.h"
#include "../../core/asset_path.h"
#include "../../core/texture_loader.h"

#include "meshoptimizer.h"

namespace Render::ms
{
    void ModelResource::Cleanup()
    {
        m_vertexAttribs.clear();
        m_indexBuffer.reset();
        m_textureImages.clear();
        m_samplers.clear();
        m_textureBindings.clear();

        m_materials.clear();
        m_meshes.clear();
        m_nodes.clear();
    }

    std::shared_ptr<ModelResource> ModelResource::CreateFromModelData(const ModelData& modelData, ResourceUploader& uploader)
    {
        auto resource = std::make_shared<ModelResource>();
        auto accessMode = StorageBuffer::AccessMode::GPUOnlyAccess;

        // 頂点バッファ
        resource->m_vertexAttribs.resize(int(ModelResource::VertexAttributeIndex::Count));
        for (int i = 0; i< int(resource->m_vertexAttribs.size()); ++i)
        {
            const auto& src = modelData.vertexBuffers[i];
            if (src.data.empty())
            {
                continue;
            }
            auto& dst = resource->m_vertexAttribs[i];
            dst.format = src.format;
            dst.stride = src.stride;
            dst.buffer = StorageBuffer::Create(src.data.size(), accessMode);
            if (!dst.buffer)
            {
                throw std::runtime_error("failed to create StorageBuffer(VertexAttrib)");
            }

            uploader.UploadBuffer(dst.buffer.get(), src.data.data(), src.data.size(), VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
        }

        // インデックスバッファ
        if (!modelData.indexBuffers.empty())
        {
            const auto& indexData = modelData.indexBuffers[0];
            auto size = sizeof(uint32_t) * indexData.indices.size();
            resource->m_indexBuffer = StorageBuffer::Create(size, accessMode);

            auto& dst = resource->m_indexBuffer;
            if (!dst)
            {
                throw std::runtime_error("failed to create StorageBuffer(IndexBuffer)");
            }
            uploader.UploadBuffer(dst.get(), indexData.indices.data(), size, VK_ACCESS_INDEX_READ_BIT);
        }

        // メッシュ構造のコピー
        for (const auto& meshData : modelData.meshes)
        {
            resource->m_meshes.push_back(meshData);
        }
        // マテリアルのコピー
        resource->m_materials.reserve(modelData.materials.size());
        for (const auto& material : modelData.materials)
        {
            resource->m_materials.push_back(material);
        }

        // テクスチャのコピー
        for (const auto& src : modelData.textureImages)
        {
            auto [texture, uploadRequest] = loader::LoadTexture2DFromMemory(src.imageData.data(), src.imageData.size(), true);
            uploader.UploadImage(texture, uploadRequest);
            resource->m_textureImages.push_back(texture);
        }

        // サンプラのコピー
        for (const auto& src : modelData.samplers)
        {
            auto sampler = Sampler::Create();
            sampler->Initialize(
                src.minFilter, src.magFilter, src.mipmapMode,
                src.addressModeU, src.addressModeV
            );
            resource->m_samplers.push_back(std::move(sampler));
        }
        // テクスチャバインディングのコピー
        for (const auto& src : modelData.textures)
        {
            TextureBinding texBinding{};
            texBinding.sampler = resource->m_samplers[src.samplerIndex];
            texBinding.texture = resource->m_textureImages[src.imageIndex];
            texBinding.textureImageIndex = src.imageIndex;
            resource->m_textureBindings.push_back(std::move(texBinding));
        }

        // ノードのコピー
        for (const auto& nodeData : modelData.nodes)
        {
            resource->m_nodes.push_back(nodeData);
        }

        // メッシュレットの作成
        for (auto& mesh : resource->m_meshes)
        {
            using VertexAttributeIndex = ModelResource::VertexAttributeIndex;
            const size_t maxVertices = 64;
            const size_t maxTriangles = 124;
            const auto& vertexData = modelData.vertexBuffers[int(VertexAttributeIndex::Position)];
            const auto& indexData = modelData.indexBuffers[0];

            for (const auto& primitive : mesh.primitives)
            {
                auto materialIndex = primitive.material;
                auto maxMeshlets = meshopt_buildMeshletsBound(primitive.indexCount, maxVertices, maxTriangles);
                std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
                std::vector<uint32_t> meshletVertices(maxMeshlets * maxVertices);
                std::vector<uint8_t> meshletTriangles(maxMeshlets * maxTriangles * 3);

                // 構築
                const auto vertexBufferOffset = primitive.vertexOffset;
                auto* indexDataPtr = indexData.indices.data() + primitive.firstIndex;
                auto* vertexDataPtr = reinterpret_cast<const glm::vec3*>(vertexData.data.data()) + vertexBufferOffset;
                auto meshletCount = meshopt_buildMeshlets(
                    meshlets.data(), meshletVertices.data(), meshletTriangles.data(),
                    indexDataPtr, primitive.indexCount,
                    &vertexDataPtr[0].x, primitive.vertexCount, vertexData.stride,
                    maxVertices, maxTriangles, 0.0f);

                if (meshlets.size())
                {
                    const meshopt_Meshlet& last = meshlets[meshletCount - 1];
                    int vertexCountPrev = int(meshletVertices.size()), triangleCountPrev = int(meshletTriangles.size());
                    meshletVertices.resize(last.vertex_offset + last.vertex_count);
                    meshletTriangles.resize(last.triangle_offset + last.triangle_count * 3);
                }

                MeshletPrimitive meshletPrimitive{
                    .meshletVertexIndexBuffer = {.data = std::move(meshletVertices) },
                    .meshletPrimitiveIndexBuffer = {.data = std::move(meshletTriangles) },
                    .meshletCount = meshletCount,
                    .materialIndex = materialIndex,
                };
                meshletPrimitive.meshletInfo.data.reserve(meshletCount);

                for (uint32_t i = 0; i < meshletCount; ++i)
                {
                    auto& srcMeshlet = meshlets[i];
                    MeshletPrimitive::MeshletInfo dstMeshlet{
                        .vertexOffset = srcMeshlet.vertex_offset,
                        .triangleOffset = srcMeshlet.triangle_offset,
                        .vertexCount = srcMeshlet.vertex_count,
                        .triangleCount = srcMeshlet.triangle_count,
                        .materialIndex = materialIndex,
                    };
                    auto CalcBufferAddress = [&](VertexAttributeIndex attribIndex) {
                        auto& vb = resource->m_vertexAttribs[int(attribIndex)];
                        auto addr = vb.buffer->GetDeviceAddress() + vb.stride * vertexBufferOffset;
                        return addr;
                        };
                    dstMeshlet.positionBufferAddress = CalcBufferAddress(VertexAttributeIndex::Position);
                    dstMeshlet.normalBufferAddress = CalcBufferAddress(VertexAttributeIndex::Normal);
                    dstMeshlet.uv0BufferAddress = CalcBufferAddress(VertexAttributeIndex::Texcoord0);
                    dstMeshlet.tangentBufferAddress = CalcBufferAddress(VertexAttributeIndex::Tangent);
                    dstMeshlet.binormalBufferAddress = CalcBufferAddress(VertexAttributeIndex::Binormal);
                    dstMeshlet.materialIndex = materialIndex;

                    meshletPrimitive.meshletInfo.data.push_back(std::move(dstMeshlet));
                }

                // GPUにデータを転送
                auto createBufferAndWrite = [&](auto& srcCpuData) {
                    using ElementType = std::decay_t<decltype(srcCpuData[0])>;
                    auto bufferSize = sizeof(ElementType) * srcCpuData.size();
                    auto buffer = StorageBuffer::Create(bufferSize, StorageBuffer::AccessMode::GPUOnlyAccess);
                    uploader.UploadBuffer(buffer.get(), srcCpuData.data(), bufferSize, VK_ACCESS_SHADER_READ_BIT);
                    return buffer;
                    };

                meshletPrimitive.meshletInfo.buffer = createBufferAndWrite(meshletPrimitive.meshletInfo.data);
                meshletPrimitive.meshletVertexIndexBuffer.buffer = createBufferAndWrite(meshletPrimitive.meshletVertexIndexBuffer.data);
                meshletPrimitive.meshletPrimitiveIndexBuffer.buffer = createBufferAndWrite(meshletPrimitive.meshletPrimitiveIndexBuffer.data);

                mesh.meshletPrimitives.push_back(std::move(meshletPrimitive));
            }
        }

        return resource;
    }

}
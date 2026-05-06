#include "BistroScene.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <meshoptimizer.h>

#include <Windows.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <stdexcept>

using namespace DirectX;

namespace
{
    std::wstring ToWide(const std::string& value)
    {
        if (value.empty())
        {
            return std::wstring();
        }

        const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (size <= 0)
        {
            return std::wstring(value.begin(), value.end());
        }

        std::wstring result(static_cast<size_t>(size - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
        return result;
    }

    std::wstring CleanTextureName(const aiString& path)
    {
        std::filesystem::path p(ToWide(path.C_Str()));
        return p.filename().wstring();
    }

    bool Exists(const std::filesystem::path& path)
    {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    std::wstring ResolveTextureByName(const std::filesystem::path& textureRoot, const std::wstring& name, const wchar_t* suffix)
    {
        if (name.empty())
        {
            return std::wstring();
        }

        const std::wstring candidateStem = name + suffix;
        for (const wchar_t* extension : { L".dds", L".DDS", L".tga", L".TGA" })
        {
            const std::filesystem::path candidate = textureRoot / (candidateStem + extension);
            if (Exists(candidate))
            {
                return candidate.wstring();
            }
        }

        return std::wstring();
    }

    std::wstring ResolveTexturePath(const std::filesystem::path& assetRoot, const aiMaterial* material, aiTextureType type, const wchar_t* suffix)
    {
        aiString rawPath;
        if (material->GetTexture(type, 0, &rawPath) == AI_SUCCESS)
        {
            const std::wstring fileName = CleanTextureName(rawPath);
            const std::filesystem::path candidate = assetRoot / L"Textures" / fileName;
            if (Exists(candidate))
            {
                return candidate.wstring();
            }
        }

        aiString materialName;
        material->Get(AI_MATKEY_NAME, materialName);
        std::wstring name = ToWide(materialName.C_Str());
        if (name.rfind(L"MASTER_", 0) == 0)
        {
            name = name.substr(7);
        }

        std::wstring resolved = ResolveTextureByName(assetRoot / L"Textures", name, suffix);
        if (!resolved.empty())
        {
            return resolved;
        }

        return ResolveTextureByName(assetRoot / L"Textures", L"MASTER_" + name, suffix);
    }

    void ProcessNode(const aiScene* aiScene, const aiNode* node, const XMMATRIX& parentTransform, Bistro::Scene& scene)
    {
        const aiMatrix4x4& t = node->mTransformation;
        XMMATRIX local = XMMatrixSet(
            t.a1, t.b1, t.c1, t.d1,
            t.a2, t.b2, t.c2, t.d2,
            t.a3, t.b3, t.c3, t.d3,
            t.a4, t.b4, t.c4, t.d4);
        XMMATRIX world = local * parentTransform;
        XMMATRIX normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

        for (uint32_t meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex)
        {
            const aiMesh* mesh = aiScene->mMeshes[node->mMeshes[meshIndex]];
            const uint32_t baseVertex = static_cast<uint32_t>(scene.vertices.size());
            const uint32_t startIndex = static_cast<uint32_t>(scene.indices.size());

            for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
            {
                aiVector3D p = mesh->mVertices[i];
                aiVector3D n = mesh->HasNormals() ? mesh->mNormals[i] : aiVector3D(0.0f, 1.0f, 0.0f);
                aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][i] : aiVector3D(0.0f, 0.0f, 0.0f);
                aiVector3D tangent = mesh->HasTangentsAndBitangents() ? mesh->mTangents[i] : aiVector3D(1.0f, 0.0f, 0.0f);

                XMVECTOR position = XMVector3Transform(XMVectorSet(p.x, p.y, p.z, 1.0f), world);
                XMVECTOR normal = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(n.x, n.y, n.z, 0.0f), normalMatrix));
                XMVECTOR tng = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(tangent.x, tangent.y, tangent.z, 0.0f), normalMatrix));

                Bistro::Vertex vertex{};
                XMStoreFloat3(&vertex.position, position);
                XMStoreFloat3(&vertex.normal, normal);
                XMStoreFloat4(&vertex.tangent, XMVectorSet(XMVectorGetX(tng), XMVectorGetY(tng), XMVectorGetZ(tng), 1.0f));
                vertex.texcoord = XMFLOAT2(uv.x, uv.y);
                scene.vertices.push_back(vertex);

                scene.boundsMin.x = (std::min)(scene.boundsMin.x, vertex.position.x);
                scene.boundsMin.y = (std::min)(scene.boundsMin.y, vertex.position.y);
                scene.boundsMin.z = (std::min)(scene.boundsMin.z, vertex.position.z);
                scene.boundsMax.x = (std::max)(scene.boundsMax.x, vertex.position.x);
                scene.boundsMax.y = (std::max)(scene.boundsMax.y, vertex.position.y);
                scene.boundsMax.z = (std::max)(scene.boundsMax.z, vertex.position.z);
            }

            for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
            {
                const aiFace& face = mesh->mFaces[faceIndex];
                if (face.mNumIndices == 3)
                {
                    scene.indices.push_back(baseVertex + face.mIndices[0]);
                    scene.indices.push_back(baseVertex + face.mIndices[1]);
                    scene.indices.push_back(baseVertex + face.mIndices[2]);
                }
            }

            Bistro::DrawItem draw{};
            draw.indexCount = static_cast<uint32_t>(scene.indices.size()) - startIndex;
            draw.startIndex = startIndex;
            draw.baseVertex = 0;
            draw.materialIndex = mesh->mMaterialIndex;
            if (draw.indexCount > 0)
            {
                scene.draws.push_back(draw);
            }
        }

        for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
        {
            ProcessNode(aiScene, node->mChildren[childIndex], world, scene);
        }
    }

    uint32_t PackMeshletTriangle(const unsigned char* triangle)
    {
        return static_cast<uint32_t>(triangle[0]) |
            (static_cast<uint32_t>(triangle[1]) << 8) |
            (static_cast<uint32_t>(triangle[2]) << 16);
    }

    void AppendDispatchRange(Bistro::Scene& scene, uint32_t materialIndex, uint32_t firstMeshlet, uint32_t meshletCount)
    {
        if (meshletCount == 0)
        {
            return;
        }

        if (!scene.meshletDispatchRanges.empty())
        {
            Bistro::MeshletDispatchRange& previous = scene.meshletDispatchRanges.back();
            if (previous.materialIndex == materialIndex && previous.firstMeshlet + previous.meshletCount == firstMeshlet)
            {
                previous.meshletCount += meshletCount;
                return;
            }
        }

        Bistro::MeshletDispatchRange range{};
        range.materialIndex = materialIndex;
        range.firstMeshlet = firstMeshlet;
        range.meshletCount = meshletCount;
        scene.meshletDispatchRanges.push_back(range);
    }

    void BuildMeshlets(Bistro::Scene& scene)
    {
        scene.meshlets.clear();
        scene.meshletVertices.clear();
        scene.meshletTriangles.clear();
        scene.meshletBounds.clear();
        scene.meshletDispatchRanges.clear();

        const float* vertexPositions = scene.vertices.empty() ? nullptr : reinterpret_cast<const float*>(&scene.vertices[0].position);
        const size_t vertexCount = scene.vertices.size();

        for (const Bistro::DrawItem& draw : scene.draws)
        {
            if (draw.indexCount == 0)
            {
                continue;
            }

            const uint32_t firstMeshlet = static_cast<uint32_t>(scene.meshlets.size());
            const size_t indexCount = draw.indexCount;
            std::vector<unsigned int> drawIndices(indexCount);
            for (size_t i = 0; i < indexCount; ++i)
            {
                drawIndices[i] = static_cast<unsigned int>(scene.indices[draw.startIndex + i]);
            }

            const size_t meshletBound = meshopt_buildMeshletsBound(indexCount, Bistro::MaxMeshletVertices, Bistro::MaxMeshletTriangles);
            std::vector<meshopt_Meshlet> meshlets(meshletBound);
            std::vector<unsigned int> meshletVertices(meshletBound * Bistro::MaxMeshletVertices);
            std::vector<unsigned char> meshletTriangles(meshletBound * Bistro::MaxMeshletTriangles * 3);

            const size_t meshletCount = meshopt_buildMeshlets(
                meshlets.data(),
                meshletVertices.data(),
                meshletTriangles.data(),
                drawIndices.data(),
                indexCount,
                vertexPositions,
                vertexCount,
                sizeof(Bistro::Vertex),
                Bistro::MaxMeshletVertices,
                Bistro::MaxMeshletTriangles,
                0.25f);

            for (size_t meshletIndex = 0; meshletIndex < meshletCount; ++meshletIndex)
            {
                const meshopt_Meshlet& source = meshlets[meshletIndex];
                unsigned int* localVertices = meshletVertices.data() + source.vertex_offset;
                unsigned char* localTriangles = meshletTriangles.data() + source.triangle_offset;
                meshopt_optimizeMeshlet(localVertices, localTriangles, source.triangle_count, source.vertex_count);

                const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                    localVertices,
                    localTriangles,
                    source.triangle_count,
                    vertexPositions,
                    vertexCount,
                    sizeof(Bistro::Vertex));

                const uint32_t vertexOffset = static_cast<uint32_t>(scene.meshletVertices.size());
                const uint32_t triangleOffset = static_cast<uint32_t>(scene.meshletTriangles.size());
                const uint32_t boundsIndex = static_cast<uint32_t>(scene.meshletBounds.size());

                for (uint32_t i = 0; i < source.vertex_count; ++i)
                {
                    scene.meshletVertices.push_back(static_cast<uint32_t>(localVertices[i]));
                }

                for (uint32_t i = 0; i < source.triangle_count; ++i)
                {
                    scene.meshletTriangles.push_back(PackMeshletTriangle(localTriangles + i * 3));
                }

                Bistro::MeshletBounds gpuBounds{};
                gpuBounds.sphere = XMFLOAT4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
                gpuBounds.coneApex = XMFLOAT4(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2], 0.0f);
                gpuBounds.coneAxis = XMFLOAT4(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff);
                scene.meshletBounds.push_back(gpuBounds);

                Bistro::MeshletRecord gpuMeshlet{};
                gpuMeshlet.vertexOffset = vertexOffset;
                gpuMeshlet.vertexCount = source.vertex_count;
                gpuMeshlet.triangleOffset = triangleOffset;
                gpuMeshlet.triangleCount = source.triangle_count;
                gpuMeshlet.boundsIndex = boundsIndex;
                scene.meshlets.push_back(gpuMeshlet);
            }

            AppendDispatchRange(scene, draw.materialIndex, firstMeshlet, static_cast<uint32_t>(scene.meshlets.size()) - firstMeshlet);
        }

        if (scene.meshlets.empty())
        {
            throw std::runtime_error("BistroExterior.fbx did not produce meshlets.");
        }
    }
}

namespace Bistro
{
    std::wstring GetRepoRootFromExecutable(uint32_t levelsFromExeToRepoRoot)
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path path(exePath);
        path = path.parent_path();
        for (uint32_t i = 0; i < levelsFromExeToRepoRoot; ++i)
        {
            path = path.parent_path();
        }
        return path.wstring();
    }

    std::wstring FindAssetRoot()
    {
        wchar_t envPath[32767] = {};
        const DWORD envSize = GetEnvironmentVariableW(L"BISTRO_ASSET_ROOT", envPath, static_cast<DWORD>(std::size(envPath)));
        if (envSize > 0 && envSize < std::size(envPath))
        {
            const std::filesystem::path candidate(envPath);
            if (Exists(candidate / L"BistroExterior.fbx"))
            {
                return candidate.wstring();
            }
        }

        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path searchRoot = std::filesystem::path(exePath).parent_path();
        for (int i = 0; i < 10 && !searchRoot.empty(); ++i)
        {
            for (const auto& candidate : { searchRoot / L"Bistro_v5_2", searchRoot / L"Samples" / L"BistroExteriorMeshShader" / L"Assets" / L"Bistro" })
            {
                if (Exists(candidate / L"BistroExterior.fbx"))
                {
                    return candidate.wstring();
                }
            }
            searchRoot = searchRoot.parent_path();
        }

        throw std::runtime_error("BistroExterior.fbx was not found. Set BISTRO_ASSET_ROOT or place Bistro_v5_2 at the repository root.");
    }

    Scene LoadScene(const std::wstring& assetRoot)
    {
        std::filesystem::path root(assetRoot);
        std::filesystem::path fbxPath = root / L"BistroExterior.fbx";
        if (!Exists(fbxPath))
        {
            throw std::runtime_error("BistroExterior.fbx was not found in the selected asset root.");
        }

        Assimp::Importer importer;
        const uint32_t flags =
            aiProcess_Triangulate |
            aiProcess_ConvertToLeftHanded |
            aiProcess_CalcTangentSpace |
            aiProcess_GenSmoothNormals |
            aiProcess_ImproveCacheLocality |
            aiProcess_OptimizeMeshes |
            aiProcess_SortByPType;

        const aiScene* aiScene = importer.ReadFile(fbxPath.string(), flags);
        if (!aiScene || !aiScene->mRootNode)
        {
            throw std::runtime_error(importer.GetErrorString());
        }

        Scene scene;
        scene.assetRoot = assetRoot;
        scene.boundsMin = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
        scene.boundsMax = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        scene.materials.resize((std::max)(1u, aiScene->mNumMaterials));
        for (uint32_t i = 0; i < aiScene->mNumMaterials; ++i)
        {
            const aiMaterial* source = aiScene->mMaterials[i];
            Material& material = scene.materials[i];

            aiString name;
            source->Get(AI_MATKEY_NAME, name);
            material.name = ToWide(name.C_Str());
            material.textures[TextureSlotBaseColor] = ResolveTexturePath(root, source, aiTextureType_BASE_COLOR, L"_BaseColor");
            if (material.textures[TextureSlotBaseColor].empty())
            {
                material.textures[TextureSlotBaseColor] = ResolveTexturePath(root, source, aiTextureType_DIFFUSE, L"_BaseColor");
            }
            material.textures[TextureSlotNormal] = ResolveTexturePath(root, source, aiTextureType_NORMALS, L"_Normal");
            if (material.textures[TextureSlotNormal].empty())
            {
                material.textures[TextureSlotNormal] = ResolveTexturePath(root, source, aiTextureType_HEIGHT, L"_Normal");
            }
            material.textures[TextureSlotSpecular] = ResolveTexturePath(root, source, aiTextureType_SPECULAR, L"_Specular");
            material.textures[TextureSlotEmissive] = ResolveTexturePath(root, source, aiTextureType_EMISSIVE, L"_Emissive");

            aiColor4D baseColor{};
            if (aiGetMaterialColor(source, AI_MATKEY_BASE_COLOR, &baseColor) == AI_SUCCESS ||
                aiGetMaterialColor(source, AI_MATKEY_COLOR_DIFFUSE, &baseColor) == AI_SUCCESS)
            {
                material.baseColorFactor = XMFLOAT4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
                material.alphaMasked = baseColor.a < 0.99f;
            }
        }

        ProcessNode(aiScene, aiScene->mRootNode, XMMatrixIdentity(), scene);
        if (scene.vertices.empty() || scene.indices.empty())
        {
            throw std::runtime_error("BistroExterior.fbx did not contain renderable triangle meshes.");
        }
        BuildMeshlets(scene);

        return scene;
    }
}
